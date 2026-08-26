import { applyWatermark, readPngSize } from "./watermark";

export interface Env {
  DB: D1Database;
  BUCKET: R2Bucket;
  QUOTA: DurableObjectNamespace;
  FIREBASE_PROJECT_ID: string;
  PUBLIC_BASE_URL: string;
  R2_ACCOUNT_ID: string;
  R2_ACCESS_KEY_ID: string;
  R2_SECRET_ACCESS_KEY: string;
  R2_BUCKET: string;
  STRIPE_PRICE_ID: string;
  STRIPE_SECRET_KEY?: string;
  STRIPE_WEBHOOK_SECRET?: string;
  ABUSE_EMAIL: string;
}

const QUOTA_BYTES = 10 * 1024 * 1024;

function shardName(uid: string): string {
  let hash = 0;
  for (let i = 0; i < uid.length; i += 1) {
    hash = (hash * 31 + uid.charCodeAt(i)) >>> 0;
  }
  return `QuotaShard-${hash % 64}`;
}

export function quotaStub(env: Env, uid: string): DurableObjectStub {
  return env.QUOTA.get(env.QUOTA.idFromName(shardName(uid)));
}

type Job = () => Promise<unknown>;

// ─── Ariadne's Thread [AT-0032] ─────────────────────
// What: 64-shard Durable Object with per-uid lock and 10 MB eviction
// Why:  Avoid 500k DOs and two-upload races
// Date: 2026-08-25
// Related: backend/src/index.ts
// ─────────────────────────────────────────────────────
export class QuotaShard {
  private locks = new Map<string, Promise<void>>();

  constructor(
    private readonly state: DurableObjectState,
    private readonly env: Env,
  ) {}

  private async withUidLock<T>(uid: string, job: () => Promise<T>): Promise<T> {
    const previous = this.locks.get(uid) ?? Promise.resolve();
    let release!: () => void;
    const gate = new Promise<void>((resolve) => {
      release = resolve;
    });
    this.locks.set(
      uid,
      previous.then(() => gate),
    );
    await previous;
    try {
      return await job();
    } finally {
      release();
      if (this.locks.get(uid) === gate) {
        this.locks.delete(uid);
      }
    }
  }

  async fetch(request: Request): Promise<Response> {
    const payload = (await request.json()) as { action: string; uid: string; [k: string]: unknown };
    console.log(`QuotaShard: action=${payload.action} uid=${payload.uid}`);
    return this.withUidLock(payload.uid, () => this.dispatch(payload));
  }

  private async dispatch(payload: { action: string; uid: string; [k: string]: unknown }): Promise<Response> {
    switch (payload.action) {
      case "confirm":
        return this.confirm(payload.uid, String(payload.shotId), Boolean(payload.publish));
      case "publish":
        return this.publishExisting(payload.uid, String(payload.shotId));
      case "deleteAccount":
        return this.deleteAccount(payload.uid);
      case "purgeCloud":
        return this.purgeCloud(payload.uid);
      default:
        return Response.json({ code: "UNKNOWN_ERROR", message: "Unknown shard action" }, { status: 400 });
    }
  }

  private async ensureUser(uid: string): Promise<{ used_bytes: number; plan: string; grace_ends_at: number | null }> {
    const row = await this.env.DB.prepare("SELECT used_bytes, plan, grace_ends_at FROM users WHERE uid = ?")
      .bind(uid)
      .first<{ used_bytes: number; plan: string; grace_ends_at: number | null }>();
    if (row) {
      return row;
    }
    const now = Date.now();
    await this.env.DB.prepare("INSERT INTO users (uid, used_bytes, plan, created_at) VALUES (?, 0, 'free', ?)")
      .bind(uid, now)
      .run();
    return { used_bytes: 0, plan: "free", grace_ends_at: null };
  }

  private isPro(user: { plan: string; grace_ends_at: number | null }): boolean {
    if (user.plan === "pro") {
      return true;
    }
    if (user.plan === "grace" && user.grace_ends_at && user.grace_ends_at > Date.now()) {
      return true;
    }
    return false;
  }

  private async evictUntilFits(uid: string, used: number, incoming: number): Promise<string[]> {
    const evicted: string[] = [];
    let current = used;
    while (current + incoming > QUOTA_BYTES) {
      const oldest = await this.env.DB.prepare(
        "SELECT shot_id, bytes, public_id, visibility FROM shots WHERE uid = ? ORDER BY created_at ASC LIMIT 1",
      )
        .bind(uid)
        .first<{ shot_id: string; bytes: number; public_id: string | null; visibility: string }>();
      if (!oldest) {
        break;
      }
      await this.deleteShot(uid, oldest.shot_id, oldest.public_id, oldest.visibility);
      current -= oldest.bytes;
      evicted.push(oldest.shot_id);
      console.log(`QuotaShard: evicted ${oldest.shot_id} bytes=${oldest.bytes}`);
    }
    await this.env.DB.prepare("UPDATE users SET used_bytes = ? WHERE uid = ?").bind(Math.max(0, current), uid).run();
    return evicted;
  }

  private async deleteShot(uid: string, shotId: string, publicId: string | null, visibility: string) {
    await this.env.BUCKET.delete(`private/${uid}/${shotId}.png`);
    if (publicId) {
      await this.env.BUCKET.delete(`public/${publicId}.png`);
    }
    await this.env.DB.prepare("DELETE FROM shots WHERE shot_id = ?").bind(shotId).run();
    console.log(`QuotaShard: deleted shot=${shotId} visibility=${visibility}`);
  }

  private async confirm(uid: string, shotId: string, publish: boolean): Promise<Response> {
    const existing = await this.env.DB.prepare(
      "SELECT shot_id, public_id, visibility FROM shots WHERE shot_id = ? AND uid = ?",
    )
      .bind(shotId, uid)
      .first<{ shot_id: string; public_id: string | null; visibility: string }>();
    if (existing) {
      console.log(`QuotaShard: confirm idempotent shot=${shotId}`);
      const publicUrl =
        existing.visibility === "public" && existing.public_id
          ? `${this.env.PUBLIC_BASE_URL}/s/${existing.public_id}`
          : "";
      const used = await this.env.DB.prepare("SELECT used_bytes FROM users WHERE uid = ?")
        .bind(uid)
        .first<{ used_bytes: number }>();
      return Response.json({
        shotId,
        publicUrl,
        usedBytes: used?.used_bytes ?? 0,
        evictedIds: [],
      });
    }
    const inboxKey = `inbox/${uid}/${shotId}.png`;
    const object = await this.env.BUCKET.head(inboxKey);
    if (!object) {
      console.warn(`QuotaShard: inbox missing ${inboxKey}`);
      return Response.json(
        { code: "INBOX_EXPIRED", message: "The upload expired before it was confirmed. Upload the screenshot again." },
        { status: 404 },
      );
    }
    const body = await this.env.BUCKET.get(inboxKey);
    if (!body) {
      return Response.json(
        { code: "INBOX_EXPIRED", message: "The upload expired before it was confirmed. Upload the screenshot again." },
        { status: 404 },
      );
    }
    const bytes = await body.arrayBuffer();
    try {
      readPngSize(bytes);
    } catch {
      await this.env.BUCKET.delete(inboxKey);
      return Response.json(
        { code: "CLOUD_IMAGE_REJECTED", message: "The screenshot was rejected by the server. Try capturing again." },
        { status: 400 },
      );
    }
    const user = await this.ensureUser(uid);
    const evicted = await this.evictUntilFits(uid, user.used_bytes, bytes.byteLength);
    const publicId = crypto.randomUUID().replace(/-/g, "");
    const now = Date.now();
    let stored: Uint8Array = new Uint8Array(bytes);
    let visibility = "private";
    let watermarked = 0;
    if (publish) {
      visibility = "public";
      if (!this.isPro(user)) {
        stored = new Uint8Array(applyWatermark(bytes));
        watermarked = 1;
      }
      await this.env.BUCKET.put(`public/${publicId}.png`, stored, {
        httpMetadata: {
          contentType: "image/png",
          cacheControl: "public, max-age=31536000, immutable",
        },
      });
    } else {
      await this.env.BUCKET.put(`private/${uid}/${shotId}.png`, stored, {
        httpMetadata: { contentType: "image/png" },
      });
    }
    await this.env.BUCKET.delete(inboxKey);
    await this.env.DB.prepare(
      "INSERT INTO shots (shot_id, uid, created_at, bytes, visibility, public_id, watermarked) VALUES (?, ?, ?, ?, ?, ?, ?)",
    )
      .bind(shotId, uid, now, stored.byteLength, visibility, publish ? publicId : null, watermarked)
      .run();
    const usedRow = await this.env.DB.prepare("SELECT COALESCE(SUM(bytes),0) as used FROM shots WHERE uid = ?")
      .bind(uid)
      .first<{ used: number }>();
    const usedBytes = usedRow?.used ?? stored.byteLength;
    await this.env.DB.prepare("UPDATE users SET used_bytes = ? WHERE uid = ?").bind(usedBytes, uid).run();
    const publicUrl = publish ? `${this.env.PUBLIC_BASE_URL}/s/${publicId}` : "";
    console.log(`QuotaShard: confirm ok shot=${shotId} publish=${publish} used=${usedBytes} evicted=${evicted.length}`);
    return Response.json({ shotId, publicUrl, usedBytes, evictedIds: evicted });
  }

  private async publishExisting(uid: string, shotId: string): Promise<Response> {
    const row = await this.env.DB.prepare(
      "SELECT bytes, visibility, public_id, watermarked FROM shots WHERE shot_id = ? AND uid = ?",
    )
      .bind(shotId, uid)
      .first<{ bytes: number; visibility: string; public_id: string | null; watermarked: number }>();
    if (!row) {
      return Response.json(
        { code: "PUBLISH_FAILED", message: "Could not publish this screenshot. Try again in a moment." },
        { status: 404 },
      );
    }
    if (row.visibility === "public" && row.public_id) {
      return Response.json({
        shotId,
        publicUrl: `${this.env.PUBLIC_BASE_URL}/s/${row.public_id}`,
        usedBytes: row.bytes,
        evictedIds: [],
      });
    }
    const privateKey = `private/${uid}/${shotId}.png`;
    const object = await this.env.BUCKET.get(privateKey);
    if (!object) {
      return Response.json(
        { code: "PUBLISH_FAILED", message: "Could not publish this screenshot. Try again in a moment." },
        { status: 404 },
      );
    }
    const bytes = await object.arrayBuffer();
    const user = await this.ensureUser(uid);
    let stored = new Uint8Array(bytes);
    let watermarked = 0;
    if (!this.isPro(user)) {
      stored = new Uint8Array(applyWatermark(bytes));
      watermarked = 1;
    }
    const publicId = crypto.randomUUID().replace(/-/g, "");
    await this.env.BUCKET.put(`public/${publicId}.png`, stored, {
      httpMetadata: {
        contentType: "image/png",
        cacheControl: "public, max-age=31536000, immutable",
      },
    });
    await this.env.BUCKET.delete(privateKey);
    await this.env.DB.prepare(
      "UPDATE shots SET visibility = 'public', public_id = ?, watermarked = ?, bytes = ? WHERE shot_id = ?",
    )
      .bind(publicId, watermarked, stored.byteLength, shotId)
      .run();
    const usedRow = await this.env.DB.prepare("SELECT COALESCE(SUM(bytes),0) as used FROM shots WHERE uid = ?")
      .bind(uid)
      .first<{ used: number }>();
    await this.env.DB.prepare("UPDATE users SET used_bytes = ? WHERE uid = ?")
      .bind(usedRow?.used ?? stored.byteLength, uid)
      .run();
    console.log(`QuotaShard: published existing shot=${shotId} public=${publicId}`);
    return Response.json({
      shotId,
      publicUrl: `${this.env.PUBLIC_BASE_URL}/s/${publicId}`,
      usedBytes: usedRow?.used ?? stored.byteLength,
      evictedIds: [],
    });
  }

  private async deleteAccount(uid: string): Promise<Response> {
    const shots = await this.env.DB.prepare("SELECT shot_id, public_id, visibility FROM shots WHERE uid = ?")
      .bind(uid)
      .all<{ shot_id: string; public_id: string | null; visibility: string }>();
    for (const shot of shots.results ?? []) {
      await this.deleteShot(uid, shot.shot_id, shot.public_id, shot.visibility);
    }
    const listed = await this.env.BUCKET.list({ prefix: `inbox/${uid}/` });
    for (const obj of listed.objects) {
      await this.env.BUCKET.delete(obj.key);
    }
    await this.env.DB.prepare("DELETE FROM users WHERE uid = ?").bind(uid).run();
    console.log(`QuotaShard: deleted account uid=${uid}`);
    return Response.json({ ok: true });
  }

  private async purgeCloud(uid: string): Promise<Response> {
    const shots = await this.env.DB.prepare("SELECT shot_id, public_id, visibility FROM shots WHERE uid = ?")
      .bind(uid)
      .all<{ shot_id: string; public_id: string | null; visibility: string }>();
    for (const shot of shots.results ?? []) {
      await this.deleteShot(uid, shot.shot_id, shot.public_id, shot.visibility);
    }
    await this.env.DB.prepare("UPDATE users SET used_bytes = 0, plan = 'free', grace_ends_at = NULL WHERE uid = ?")
      .bind(uid)
      .run();
    console.log(`QuotaShard: purged cloud uid=${uid}`);
    return Response.json({ ok: true });
  }
}
