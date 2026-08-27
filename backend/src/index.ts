import { verifyFirebaseToken, verifyFirebaseUser } from "./auth";
import { jsonError } from "./errors";
import { sharePage, tosPage } from "./html";
import { createPolarCheckoutUrl, deletePolarCustomer, handlePolarWebhook } from "./polar";
import { Env, QuotaShard, isShotFileId, quotaLimitBytes, quotaStub, uploadProgressKey } from "./quota";

export { QuotaShard };

const CORS = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers": "Authorization, Content-Type",
  "Access-Control-Allow-Methods": "GET,POST,DELETE,OPTIONS",
};

function withCors(response: Response): Response {
  const headers = new Headers(response.headers);
  for (const [k, v] of Object.entries(CORS)) {
    headers.set(k, v);
  }
  return new Response(response.body, { status: response.status, headers });
}

// ─── Ariadne's Thread [AT-0219] ─────────────────────
// What: Workers Rate Limiting on /screenshot/* and /s/*
// Why:  Same official pathname + flood keys as seenshot-web share pages
// Date: 2026-08-27
// Related: [AT-0218] wrangler.toml:[[ratelimits]], [AT-0065] seenshot-web→src/index.ts:allowScreenshotTraffic
// ─────────────────────────────────────────────────────
async function allowScreenshotTraffic(request: Request, env: Env): Promise<Response | null> {
  const pathname = new URL(request.url).pathname;
  const pathLimit = await env.SCREENSHOT_RATE.limit({ key: pathname });
  const floodLimit = await env.SCREENSHOT_FLOOD.limit({ key: "screenshot" });
  console.log(
    `index: screenshot rate pathname=${pathname} pathOk=${pathLimit.success} floodOk=${floodLimit.success}`,
  );
  if (pathLimit.success && floodLimit.success) {
    return null;
  }
  console.warn(
    `index: screenshot rate exceeded pathname=${pathname} pathOk=${pathLimit.success} floodOk=${floodLimit.success}`,
  );
  return new Response("Too many requests", {
    status: 429,
    headers: {
      "retry-after": "60",
      "cache-control": "no-store",
      "content-type": "text/plain; charset=utf-8",
    },
  });
}

async function uidFrom(request: Request, env: Env): Promise<string> {
  return verifyFirebaseToken(request.headers.get("Authorization"), env.FIREBASE_PROJECT_ID);
}

// ─── Ariadne's Thread [AT-0033] ─────────────────────
// What: HTTP API, share HTML, cron grace/inbox, Polar webhooks
// Why:  Cheap Cloudflare edge backend from the plan
// Date: 2026-08-25
// Related: [AT-0032] quota.ts:QuotaShard, [AT-0029] auth.ts
// ─────────────────────────────────────────────────────
export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    try {
      const response = await handle(request, env);
      return withCors(response);
    } catch (error) {
      const code = error instanceof Error ? error.message : "UNKNOWN_ERROR";
      console.error(`index: unhandled ${code}`, error);
      if (code === "STORAGE_NEED_SIGN_IN" || code === "AUTH_REFRESH_FAILED") {
        return withCors(jsonError(code, 401));
      }
      if (code === "AUTH_DISPOSABLE_EMAIL") {
        return withCors(jsonError(code, 403));
      }
      return withCors(jsonError(code === "CLOUD_IMAGE_REJECTED" ? code : "UNKNOWN_ERROR", 500));
    }
  },

  async scheduled(event: ScheduledEvent, env: Env): Promise<void> {
    console.log(`index: cron ${event.cron}`);
    if (event.cron === "0 * * * *") {
      await cleanupInbox(env);
      return;
    }
    await purgeExpiredGrace(env);
  },
};

async function handle(request: Request, env: Env): Promise<Response> {
  const url = new URL(request.url);
  if (request.method === "OPTIONS") {
    return new Response(null, { headers: CORS });
  }
  if (url.pathname === "/health") {
    return Response.json({ ok: true });
  }
  if (url.pathname === "/tos") {
    return new Response(tosPage(env.ABUSE_EMAIL), { headers: { "content-type": "text/html; charset=utf-8" } });
  }
  if (url.pathname === "/robots.txt") {
    return new Response("User-agent: *\nDisallow: /\n", { headers: { "content-type": "text/plain" } });
  }
  if (url.pathname.startsWith("/screenshot/") || url.pathname.startsWith("/s/")) {
    const limited = await allowScreenshotTraffic(request, env);
    if (limited) {
      return limited;
    }
    return serveShare(url, env);
  }
  if (url.pathname === "/v1/uploads/presign" && request.method === "POST") {
    return presign(request, env);
  }
  if (url.pathname.startsWith("/v1/uploads/put/") && request.method === "PUT") {
    return putInbox(request, env);
  }
  if (url.pathname.startsWith("/v1/uploads/progress/") && request.method === "POST") {
    return putProgress(request, env);
  }
  if (url.pathname === "/v1/uploads/confirm" && request.method === "POST") {
    return confirm(request, env);
  }
  if (url.pathname === "/v1/shots/publish" && request.method === "POST") {
    return publishExisting(request, env);
  }
  if (url.pathname === "/v1/quota" && request.method === "GET") {
    return quota(request, env);
  }
  if (url.pathname === "/v1/account/export" && request.method === "GET") {
    return exportAccount(request, env);
  }
  if (url.pathname === "/v1/account" && request.method === "DELETE") {
    return deleteAccount(request, env);
  }
  if (url.pathname === "/v1/billing/checkout" && request.method === "POST") {
    return checkout(request, env);
  }
  if (url.pathname === "/v1/polar/webhook" && request.method === "POST") {
    return handlePolarWebhook(request, env);
  }
  if (url.pathname === "/v1/abuse" && (request.method === "GET" || request.method === "POST")) {
    return abuse(request, env);
  }
  return jsonError("UNKNOWN_ERROR", 404);
}

async function serveShare(url: URL, env: Env): Promise<Response> {
  const prefix = url.pathname.startsWith("/screenshot/") ? "/screenshot/" : "/s/";
  const id = decodeURIComponent(url.pathname.slice(prefix.length)).replace(/\.png$/, "");
  const banned = await env.DB.prepare("SELECT public_id FROM takedowns WHERE public_id = ?").bind(id).first();
  const row = banned
    ? null
    : await env.DB.prepare(
        "SELECT shot_id FROM shots WHERE visibility = 'public' AND (public_id = ? OR shot_id = ?)",
      )
        .bind(id, id)
        .first();
  // ─── Ariadne's Thread [AT-0208] ─────────────────────
  // What: Load the share PNG from seenshot.app/public/{id}.png
  // Why:  Apex is the site Worker; R2 keys stay public/{id}.png, not r2.dev
  // Date: 2026-08-27
  // Related: [AT-0167] backend/wrangler.toml, [AT-0050] seenshot-web→src/index.ts:publicPng
  // ─────────────────────────────────────────────────────
  const imageUrl = `${env.R2_PUBLIC_BASE_URL}/public/${id}.png`;
  console.log(`index: share imageUrl=${imageUrl} r2PublicBase=${env.R2_PUBLIC_BASE_URL} id=${id}`);
  const html = sharePage({
    publicId: id,
    imageUrl,
    missing: !row,
    abuseEmail: env.ABUSE_EMAIL,
  });
  console.log(`index: share prefix=${prefix} id=${id} missing=${!row} banned=${Boolean(banned)}`);
  return new Response(html, {
    status: row ? 200 : 404,
    headers: { "content-type": "text/html; charset=utf-8", "cache-control": "public, max-age=60" },
  });
}

// ─── Ariadne's Thread [AT-0167] ─────────────────────
// What: Return a Worker PUT URL for inbox instead of an R2 S3 presign
// Why:  No custom domain and no R2 S3 tokens; write via the BUCKET binding
// Date: 2026-08-26
// Related: [AT-0033] backend/src/index.ts:handle, [AT-0032] backend/src/quota.ts:confirm
// ─────────────────────────────────────────────────────
async function presign(request: Request, env: Env): Promise<Response> {
  const uid = await uidFrom(request, env);
  const body = (await request.json()) as { bytes?: number; fileId?: string };
  if (!body.bytes || body.bytes > 8 * 1024 * 1024) {
    return jsonError("CLOUD_IMAGE_REJECTED");
  }
  const fileId = typeof body.fileId === "string" ? body.fileId : "";
  const shotId = isShotFileId(fileId) ? fileId : crypto.randomUUID();
  const origin = new URL(request.url).origin;
  const uploadUrl = `${origin}/v1/uploads/put/${encodeURIComponent(uid)}/${encodeURIComponent(shotId)}`;
  console.log(
    `index: presign uid=${uid} shot=${shotId} fileId=${fileId} fileIdOk=${isShotFileId(fileId)} bytes=${body.bytes} uploadUrl=${uploadUrl}`,
  );
  return Response.json({ shotId, uploadUrl });
}

// ─── Ariadne's Thread [AT-0170] ─────────────────────
// What: Stream the PUT body into R2 instead of buffering arrayBuffer
// Why:  Official BUCKET.put accepts request.body; 3.8 MB arrayBuffer delayed 204 past the client timer
// Date: 2026-08-26
// Related: [AT-0167] backend/src/index.ts:presign, [AT-0170] CloudClient.cpp:presignAndPut
// ─────────────────────────────────────────────────────
async function putInbox(request: Request, env: Env): Promise<Response> {
  const parts = new URL(request.url).pathname.split("/");
  const uid = decodeURIComponent(parts[4] || "");
  const shotId = decodeURIComponent(parts[5] || "");
  if (!uid || !shotId) {
    console.warn(`index: putInbox missing uid or shotId path=${request.url}`);
    return jsonError("UPLOAD_FAILED", 400);
  }
  const declared = Number(request.headers.get("content-length") || "0");
  console.log(`index: putInbox uid=${uid} shot=${shotId} contentLength=${declared}`);
  if (declared > 8 * 1024 * 1024) {
    return jsonError("CLOUD_IMAGE_REJECTED");
  }
  const objectKey = `inbox/${uid}/${shotId}.png`;
  const stored = await env.BUCKET.put(objectKey, request.body, {
    httpMetadata: { contentType: "image/png" },
  });
  if (!stored || stored.size < 1 || stored.size > 8 * 1024 * 1024) {
    if (stored) {
      await env.BUCKET.delete(objectKey);
    }
    await env.BUCKET.delete(uploadProgressKey(shotId));
    console.warn(`index: putInbox rejected size=${stored?.size ?? -1} key=${objectKey}`);
    return jsonError("CLOUD_IMAGE_REJECTED");
  }
  console.log(`index: putInbox stored ${objectKey} bytes=${stored.size}`);
  return new Response(null, { status: 204 });
}

// ─── Ariadne's Thread [AT-0213] ─────────────────────
// What: POST PUT byte counts into progress/{shotId}.json
// Why:  putInbox must keep streaming request.body; Mac already has uploadProgress
// Date: 2026-08-27
// Related: [AT-0212] CloudClient.cpp:postUploadProgress, [AT-0211] backend/src/quota.ts:uploadProgressKey
// ─────────────────────────────────────────────────────
async function putProgress(request: Request, env: Env): Promise<Response> {
  const shotId = decodeURIComponent(new URL(request.url).pathname.slice("/v1/uploads/progress/".length));
  if (!shotId) {
    console.warn(`index: putProgress missing shotId path=${request.url}`);
    return jsonError("UPLOAD_FAILED", 400);
  }
  const body = (await request.json()) as { sent?: number; total?: number; done?: boolean };
  const sent = Number(body.sent);
  const total = Number(body.total);
  const done = Boolean(body.done);
  if (!Number.isFinite(sent) || !Number.isFinite(total) || sent < 0 || total < 1 || sent > 8 * 1024 * 1024 || total > 8 * 1024 * 1024) {
    console.warn(`index: putProgress bad numbers shot=${shotId} sent=${sent} total=${total}`);
    return jsonError("UPLOAD_FAILED", 400);
  }
  const payload = JSON.stringify({ sent, total, done });
  const key = uploadProgressKey(shotId);
  await env.BUCKET.put(key, payload, {
    httpMetadata: { contentType: "application/json", cacheControl: "no-store" },
  });
  console.log(`index: putProgress shot=${shotId} sent=${sent} total=${total} done=${done} key=${key}`);
  return new Response(null, { status: 204 });
}

async function confirm(request: Request, env: Env): Promise<Response> {
  const uid = await uidFrom(request, env);
  const body = (await request.json()) as { shotId: string; publish?: boolean };
  const stub = quotaStub(env, uid);
  return stub.fetch("https://quota/confirm", {
    method: "POST",
    body: JSON.stringify({ action: "confirm", uid, shotId: body.shotId, publish: Boolean(body.publish) }),
  });
}

async function publishExisting(request: Request, env: Env): Promise<Response> {
  const uid = await uidFrom(request, env);
  const body = (await request.json()) as { shotId: string };
  const stub = quotaStub(env, uid);
  return stub.fetch("https://quota/publish", {
    method: "POST",
    body: JSON.stringify({ action: "publish", uid, shotId: body.shotId }),
  });
}

async function quota(request: Request, env: Env): Promise<Response> {
  const uid = await uidFrom(request, env);
  const row = await env.DB.prepare("SELECT used_bytes, plan, grace_ends_at FROM users WHERE uid = ?")
    .bind(uid)
    .first<{ used_bytes: number; plan: string; grace_ends_at: number | null }>();
  const plan = row?.plan ?? "free";
  if (plan === "grace" && row?.grace_ends_at && row.grace_ends_at <= Date.now()) {
    return jsonError("PRO_GRACE_ENDED", 410);
  }
  const user = { plan, grace_ends_at: row?.grace_ends_at ?? null };
  const limitBytes = quotaLimitBytes(user);
  const usedBytes = row?.used_bytes ?? 0;
  console.log(
    `index: quota uid=${uid} plan=${plan} used=${usedBytes} limit=${limitBytes} member=${plan === "pro" || plan === "grace"}`,
  );
  return Response.json({
    usedBytes,
    plan,
    graceEndsAt: row?.grace_ends_at ?? null,
    limitBytes,
  });
}

async function exportAccount(request: Request, env: Env): Promise<Response> {
  const uid = await uidFrom(request, env);
  const user = await env.DB.prepare("SELECT uid, used_bytes, plan, created_at FROM users WHERE uid = ?")
    .bind(uid)
    .first();
  const shots = await env.DB.prepare(
    "SELECT shot_id, created_at, bytes, visibility, public_id FROM shots WHERE uid = ?",
  )
    .bind(uid)
    .all();
  console.log(`index: export uid=${uid} shots=${shots.results?.length ?? 0}`);
  return Response.json({ user, shots: shots.results ?? [] });
}

async function deleteAccount(request: Request, env: Env): Promise<Response> {
  const uid = await uidFrom(request, env);
  const stub = quotaStub(env, uid);
  const deleted = await stub.fetch("https://quota/delete", {
    method: "POST",
    body: JSON.stringify({ action: "deleteAccount", uid }),
  });
  if (!deleted.ok) {
    return jsonError("DELETE_ACCOUNT_FAILED", 500);
  }
  await deletePolarCustomer(env, uid);
  console.log(`index: account deleted uid=${uid}`);
  return Response.json({ code: "ACCOUNT_DELETED", message: "Your SeenShot account and cloud data were deleted." });
}

// ─── Ariadne's Thread [AT-0277] ─────────────────────
// What: POST /v1/billing/checkout creates a Polar Checkout session
// Why:  Member upgrade must use Polar, not Stripe
// Date: 2026-08-27
// Related: [AT-0276] backend→polar.ts:createPolarCheckoutUrl, app→CloudClient.cpp:createCheckoutUrl
// ─────────────────────────────────────────────────────
async function checkout(request: Request, env: Env): Promise<Response> {
  const user = await verifyFirebaseUser(request.headers.get("Authorization"), env.FIREBASE_PROJECT_ID);
  console.log(`index: checkout uid=${user.uid} emailChars=${user.email.length}`);
  try {
    const url = await createPolarCheckoutUrl(request, env, user.uid, user.email);
    console.log(`index: checkout urlChars=${url.length} uid=${user.uid}`);
    return Response.json({ url });
  } catch (error) {
    console.error(`index: checkout failed uid=${user.uid}`, error);
    return jsonError("UNKNOWN_ERROR", 500);
  }
}

async function abuse(request: Request, env: Env): Promise<Response> {
  const url = new URL(request.url);
  const id = url.searchParams.get("id") || ((await request.json().catch(() => ({}))) as { id?: string }).id;
  if (!id) {
    return jsonError("UNKNOWN_ERROR", 400);
  }
  await env.DB.prepare("INSERT OR REPLACE INTO takedowns (public_id, reason, created_at) VALUES (?, 'report', ?)")
    .bind(id, Date.now())
    .run();
  await env.BUCKET.delete(`public/${id}.png`);
  await env.DB.prepare("UPDATE shots SET visibility = 'private' WHERE public_id = ?").bind(id).run();
  console.log(`index: abuse takedown ${id}`);
  return Response.json({ ok: true });
}

async function purgeExpiredGrace(env: Env): Promise<void> {
  const now = Date.now();
  const rows = await env.DB.prepare("SELECT uid FROM users WHERE plan = 'grace' AND grace_ends_at <= ? LIMIT 100")
    .bind(now)
    .all<{ uid: string }>();
  for (const row of rows.results ?? []) {
    const stub = quotaStub(env, row.uid);
    await stub.fetch("https://quota/purge", {
      method: "POST",
      body: JSON.stringify({ action: "purgeCloud", uid: row.uid }),
    });
    console.log(`index: grace purged uid=${row.uid}`);
  }
}

async function cleanupInbox(env: Env): Promise<void> {
  const cutoff = Date.now() - 60 * 60 * 1000;
  async function expirePrefix(prefix: string) {
    let cursor: string | undefined;
    do {
      const listed = await env.BUCKET.list({ prefix, cursor });
      for (const obj of listed.objects) {
        if (obj.uploaded.getTime() < cutoff) {
          await env.BUCKET.delete(obj.key);
          console.log(`index: expired ${obj.key} prefix=${prefix}`);
        }
      }
      cursor = listed.truncated ? listed.cursor : undefined;
    } while (cursor);
  }
  await expirePrefix("inbox/");
  await expirePrefix("progress/");
}
