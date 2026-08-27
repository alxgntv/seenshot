import { readPngSize } from "./watermark";

// ─── Ariadne's Thread [AT-0220] ─────────────────────
// What: Bind SCREENSHOT_RATE and SCREENSHOT_FLOOD on the API Env
// Why:  /screenshot and /s on this Worker use the Rate Limiting API
// Date: 2026-08-27
// Related: [AT-0218] wrangler.toml:[[ratelimits]], [AT-0219] backend/src/index.ts:allowScreenshotTraffic
// ─────────────────────────────────────────────────────
export interface Env {
  DB: D1Database;
  BUCKET: R2Bucket;
  QUOTA: DurableObjectNamespace;
  FIREBASE_PROJECT_ID: string;
  PUBLIC_BASE_URL: string;
  R2_PUBLIC_BASE_URL: string;
  R2_ACCOUNT_ID: string;
  R2_ACCESS_KEY_ID: string;
  R2_SECRET_ACCESS_KEY: string;
  R2_BUCKET: string;
  POLAR_PRODUCT_ID: string;
  POLAR_ORGANIZATION_ID: string;
  POLAR_ACCESS_TOKEN?: string;
  POLAR_WEBHOOK_SECRET?: string;
  ABUSE_EMAIL: string;
  SCREENSHOT_RATE: RateLimit;
  SCREENSHOT_FLOOD: RateLimit;
}

export const FREE_QUOTA_BYTES = 10 * 1024 * 1024;
export const MEMBER_QUOTA_BYTES = 1024 * 1024 * 1024;

export function isMemberQuota(user: { plan: string; grace_ends_at: number | null }): boolean {
  if (user.plan === "pro") {
    return true;
  }
  if (user.plan === "grace" && user.grace_ends_at && user.grace_ends_at > Date.now()) {
    return true;
  }
  return false;
}

export function quotaLimitBytes(user: { plan: string; grace_ends_at: number | null } | null | undefined): number {
  if (user && isMemberQuota(user)) {
    return MEMBER_QUOTA_BYTES;
  }
  return FREE_QUOTA_BYTES;
}

// ─── Ariadne's Thread [AT-0185] ─────────────────────
// What: Public URL is /screenshot/{fileId} from the local PNG name
// Why:  SeenShot-date-12hex.png and the share link must use the same id
// Date: 2026-08-27
// Related: [AT-0184] AnnotateWindow.cpp:makeShotFileId, seenshot-web→src/index.ts:serveShare
// ─────────────────────────────────────────────────────
function publicShareUrl(base: string, publicId: string): string {
  const url = `${base}/screenshot/${publicId}`;
  console.log(`QuotaShard: publicShareUrl ${url}`);
  return url;
}

export function isShotFileId(value: string): boolean {
  const ok = /^[0-9a-f]{12}$/.test(value);
  console.log(`QuotaShard: isShotFileId value=${value} ok=${ok}`);
  return ok;
}

// ─── Ariadne's Thread [AT-0211] ─────────────────────
// What: R2 key for PUT byte progress so /screenshot can poll real upload speed
// Why:  Wait page must not fake 0-90% on a timer; bytes already flow through putInbox
// Date: 2026-08-27
// Related: [AT-0170] backend/src/index.ts:putInbox, [AT-0055] seenshot-web→src/index.ts:uploadProgress
// ─────────────────────────────────────────────────────
export function uploadProgressKey(shotId: string): string {
  return `progress/${shotId}.json`;
}

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
      case "setPlan":
        return this.setPlan(
          payload.uid,
          String(payload.plan || ""),
          typeof payload.polarCustomer === "string" ? payload.polarCustomer : null,
          typeof payload.graceEndsAt === "number" ? payload.graceEndsAt : null,
        );
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

  // ─── Ariadne's Thread [AT-0278] ─────────────────────
  // What: Set plan to pro or grace inside the per-uid Durable Object lock
  // Why:  Polar webhooks must not race confirm() reading a stale 10 MB cap
  // Date: 2026-08-27
  // Related: [AT-0276] backend→polar.ts:applyPolarCustomerState, [AT-0279] backend→quota.ts:evictUntilFits
  // ─────────────────────────────────────────────────────
  private async setPlan(
    uid: string,
    plan: string,
    polarCustomer: string | null,
    graceEndsAt: number | null,
  ): Promise<Response> {
    const user = await this.ensureUser(uid);
    console.log(
      `QuotaShard: setPlan uid=${uid} from=${user.plan} to=${plan} polarCustomer=${polarCustomer ?? ""}` +
        ` graceEndsAt=${graceEndsAt ?? ""}`,
    );
    if (plan === "pro") {
      await this.env.DB.prepare(
        "UPDATE users SET plan = 'pro', grace_ends_at = NULL, polar_customer = COALESCE(?, polar_customer) WHERE uid = ?",
      )
        .bind(polarCustomer, uid)
        .run();
      console.log(`QuotaShard: plan pro uid=${uid} polarCustomer=${polarCustomer ?? ""}`);
      return Response.json({ ok: true, plan: "pro" });
    }
    if (plan === "grace") {
      if (user.plan !== "pro") {
        console.log(`QuotaShard: skip grace uid=${uid} plan=${user.plan}`);
        return Response.json({ ok: true, plan: user.plan });
      }
      const ends = typeof graceEndsAt === "number" ? graceEndsAt : Date.now() + 7 * 24 * 60 * 60 * 1000;
      await this.env.DB.prepare("UPDATE users SET plan = 'grace', grace_ends_at = ? WHERE uid = ?").bind(ends, uid).run();
      console.log(`QuotaShard: grace started uid=${uid} ends=${ends}`);
      return Response.json({ ok: true, plan: "grace" });
    }
    console.warn(`QuotaShard: setPlan unknown plan=${plan} uid=${uid}`);
    return Response.json({ code: "UNKNOWN_ERROR", message: "Unknown plan" }, { status: 400 });
  }

  // ─── Ariadne's Thread [AT-0279] ─────────────────────
  // What: Evict against Free 10 MB or Member 1 GB from the user's plan
  // Why:  Polar Member must actually raise screenshot storage, not keep 10 MB
  // Date: 2026-08-27
  // Related: [AT-0071] seenshot-web→src/index.ts:me, [AT-0278] backend→quota.ts:setPlan
  // ─────────────────────────────────────────────────────
  private async evictUntilFits(
    uid: string,
    user: { used_bytes: number; plan: string; grace_ends_at: number | null },
    incoming: number,
  ): Promise<string[]> {
    const evicted: string[] = [];
    let current = user.used_bytes;
    const cap = quotaLimitBytes(user);
    console.log(
      `QuotaShard: evictUntilFits uid=${uid} plan=${user.plan} used=${current} incoming=${incoming} cap=${cap}`,
    );
    while (current + incoming > cap) {
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
    await this.env.BUCKET.delete(uploadProgressKey(shotId));
    await this.env.DB.prepare("DELETE FROM shots WHERE shot_id = ?").bind(shotId).run();
    console.log(`QuotaShard: deleted shot=${shotId} visibility=${visibility} progressKey=${uploadProgressKey(shotId)}`);
  }

  // ─── Ariadne's Thread [AT-0216] ─────────────────────
  // What: Confirm copies inbox PNG as-is; watermarked stays 0
  // Why:  Mac already encoded the public file; pngjs rewrite changed size and stamped SeenShot
  // Date: 2026-08-27
  // Related: [AT-0214] app→CloudPngEncoder.cpp:encode, [AT-0031] backend/src/watermark.ts:applyWatermark
  // ─────────────────────────────────────────────────────
  private async confirm(uid: string, shotId: string, publish: boolean): Promise<Response> {
    const existing = await this.env.DB.prepare(
      "SELECT shot_id, public_id, visibility FROM shots WHERE shot_id = ? AND uid = ?",
    )
      .bind(shotId, uid)
      .first<{ shot_id: string; public_id: string | null; visibility: string }>();
    if (existing) {
      console.log(`QuotaShard: confirm idempotent shot=${shotId}`);
      await this.env.BUCKET.delete(uploadProgressKey(shotId));
      const publicUrl =
        existing.visibility === "public" && existing.public_id
          ? publicShareUrl(this.env.PUBLIC_BASE_URL, existing.public_id)
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
      await this.env.BUCKET.delete(uploadProgressKey(shotId));
      return Response.json(
        { code: "CLOUD_IMAGE_REJECTED", message: "The screenshot was rejected by the server. Try capturing again." },
        { status: 400 },
      );
    }
    const user = await this.ensureUser(uid);
    const evicted = await this.evictUntilFits(uid, user, bytes.byteLength);
    const publicId = shotId;
    const now = Date.now();
    const stored = new Uint8Array(bytes);
    const watermarked = 0;
    let visibility = "private";
    console.log(
      `QuotaShard: confirm copy inbox as-is shot=${shotId} bytes=${stored.byteLength} watermarked=${watermarked} publish=${publish}`,
    );
    if (publish) {
      const clash = await this.env.DB.prepare("SELECT shot_id FROM shots WHERE public_id = ?")
        .bind(publicId)
        .first<{ shot_id: string }>();
      if (clash && clash.shot_id !== shotId) {
        console.warn(`QuotaShard: public_id clash publicId=${publicId} existing=${clash.shot_id}`);
        return Response.json(
          { code: "UNKNOWN_ERROR", message: "Something went wrong. Try again." },
          { status: 500 },
        );
      }
      visibility = "public";
      await this.env.BUCKET.put(`public/${publicId}.png`, stored, {
        httpMetadata: {
          contentType: "image/png",
          cacheControl: "public, max-age=31536000, immutable",
        },
      });
    } else {
      // ─── Ariadne's Thread [AT-0221] ─────────────────────
      // What: private/{uid}/{id}.png Cache-Control private, max-age=86400
      // Why:  Cabinet img hits this object; 60s GET override made every revisit a full R2 read
      // Date: 2026-08-27
      // Related: [AT-0070] seenshot-web→src/index.ts:shotImage, [AT-0216] backend/src/quota.ts:confirm
      // ─────────────────────────────────────────────────────
      await this.env.BUCKET.put(`private/${uid}/${shotId}.png`, stored, {
        httpMetadata: {
          contentType: "image/png",
          cacheControl: "private, max-age=86400",
        },
      });
    }
    await this.env.BUCKET.delete(inboxKey);
    await this.env.BUCKET.delete(uploadProgressKey(shotId));
    console.log(`QuotaShard: confirm deleted progress shot=${shotId} key=${uploadProgressKey(shotId)}`);
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
    const publicUrl = publish ? publicShareUrl(this.env.PUBLIC_BASE_URL, publicId) : "";
    console.log(
      `QuotaShard: confirm ok shot=${shotId} publish=${publish} publicId=${publicId} used=${usedBytes} bytes=${stored.byteLength} watermarked=${watermarked} evicted=${evicted.length}`,
    );
    return Response.json({ shotId, publicUrl, usedBytes, evictedIds: evicted });
  }

  // ─── Ariadne's Thread [AT-0217] ─────────────────────
  // What: publishExisting copies private PNG to public as-is; watermarked=0
  // Why:  Re-share must not re-encode or stamp; same bytes as Mac PUT
  // Date: 2026-08-27
  // Related: [AT-0216] backend/src/quota.ts:confirm, [AT-0031] backend/src/watermark.ts:applyWatermark
  // ─────────────────────────────────────────────────────
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
        publicUrl: publicShareUrl(this.env.PUBLIC_BASE_URL, row.public_id),
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
    const stored = new Uint8Array(bytes);
    const watermarked = 0;
    const publicId = shotId;
    console.log(
      `QuotaShard: publishExisting copy private as-is shot=${shotId} bytes=${stored.byteLength} watermarked=${watermarked}`,
    );
    const clash = await this.env.DB.prepare("SELECT shot_id FROM shots WHERE public_id = ?")
      .bind(publicId)
      .first<{ shot_id: string }>();
    if (clash && clash.shot_id !== shotId) {
      console.warn(`QuotaShard: publish clash publicId=${publicId} existing=${clash.shot_id}`);
      return Response.json(
        { code: "UNKNOWN_ERROR", message: "Something went wrong. Try again." },
        { status: 500 },
      );
    }
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
    console.log(
      `QuotaShard: published existing shot=${shotId} public=${publicId} bytes=${stored.byteLength} watermarked=${watermarked}`,
    );
    return Response.json({
      shotId,
      publicUrl: publicShareUrl(this.env.PUBLIC_BASE_URL, publicId),
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
