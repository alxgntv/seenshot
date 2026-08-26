import Stripe from "stripe";
import { verifyFirebaseToken } from "./auth";
import { jsonError } from "./errors";
import { sharePage, tosPage } from "./html";
import { presignPut } from "./presign";
import { Env, QuotaShard, quotaStub } from "./quota";

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

async function uidFrom(request: Request, env: Env): Promise<string> {
  return verifyFirebaseToken(request.headers.get("Authorization"), env.FIREBASE_PROJECT_ID);
}

// ─── Ariadne's Thread [AT-0033] ─────────────────────
// What: HTTP API, share HTML, cron grace/inbox, Stripe webhooks
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
  if (url.pathname.startsWith("/s/")) {
    return serveShare(url.pathname.slice(3), env);
  }
  if (url.pathname === "/v1/uploads/presign" && request.method === "POST") {
    return presign(request, env);
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
  if (url.pathname === "/v1/stripe/webhook" && request.method === "POST") {
    return stripeWebhook(request, env);
  }
  if (url.pathname === "/v1/abuse" && (request.method === "GET" || request.method === "POST")) {
    return abuse(request, env);
  }
  return jsonError("UNKNOWN_ERROR", 404);
}

async function serveShare(publicId: string, env: Env): Promise<Response> {
  const id = publicId.replace(/\.png$/, "");
  const banned = await env.DB.prepare("SELECT public_id FROM takedowns WHERE public_id = ?").bind(id).first();
  const row = banned
    ? null
    : await env.DB.prepare("SELECT shot_id FROM shots WHERE public_id = ? AND visibility = 'public'")
        .bind(id)
        .first();
  const imageUrl = `${env.PUBLIC_BASE_URL}/p/${id}.png`;
  const html = sharePage({
    publicId: id,
    imageUrl,
    missing: !row,
    abuseEmail: env.ABUSE_EMAIL,
  });
  console.log(`index: share ${id} missing=${!row}`);
  return new Response(html, {
    status: row ? 200 : 404,
    headers: { "content-type": "text/html; charset=utf-8", "cache-control": "public, max-age=60" },
  });
}

async function presign(request: Request, env: Env): Promise<Response> {
  const uid = await uidFrom(request, env);
  const body = (await request.json()) as { bytes?: number };
  if (!body.bytes || body.bytes > 8 * 1024 * 1024) {
    return jsonError("CLOUD_IMAGE_REJECTED");
  }
  const shotId = crypto.randomUUID();
  const objectKey = `inbox/${uid}/${shotId}.png`;
  const uploadUrl = await presignPut({
    accountId: env.R2_ACCOUNT_ID,
    accessKeyId: env.R2_ACCESS_KEY_ID,
    secretAccessKey: env.R2_SECRET_ACCESS_KEY,
    bucket: env.R2_BUCKET,
    objectKey,
  });
  console.log(`index: presign uid=${uid} shot=${shotId} bytes=${body.bytes}`);
  return Response.json({ shotId, uploadUrl });
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
  return Response.json({
    usedBytes: row?.used_bytes ?? 0,
    plan,
    graceEndsAt: row?.grace_ends_at ?? null,
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
  if (env.STRIPE_SECRET_KEY) {
    const row = await env.DB.prepare("SELECT stripe_customer FROM users WHERE uid = ?")
      .bind(uid)
      .first<{ stripe_customer: string | null }>();
    if (row?.stripe_customer) {
      const stripe = new Stripe(env.STRIPE_SECRET_KEY);
      try {
        await stripe.customers.del(row.stripe_customer);
        console.log(`index: stripe customer deleted ${row.stripe_customer}`);
      } catch (error) {
        console.warn("index: stripe delete failed", error);
      }
    }
  }
  console.log(`index: account deleted uid=${uid}`);
  return Response.json({ code: "ACCOUNT_DELETED", message: "Your SeenShot account and cloud data were deleted." });
}

async function checkout(request: Request, env: Env): Promise<Response> {
  const uid = await uidFrom(request, env);
  if (!env.STRIPE_SECRET_KEY) {
    return jsonError("UNKNOWN_ERROR", 500);
  }
  const stripe = new Stripe(env.STRIPE_SECRET_KEY);
  const session = await stripe.checkout.sessions.create({
    mode: "subscription",
    line_items: [{ price: env.STRIPE_PRICE_ID, quantity: 1 }],
    automatic_tax: { enabled: true },
    success_url: `${env.PUBLIC_BASE_URL}/tos`,
    cancel_url: `${env.PUBLIC_BASE_URL}/tos`,
    client_reference_id: uid,
    metadata: { firebase_uid: uid },
    subscription_data: { metadata: { firebase_uid: uid } },
  });
  console.log(`index: checkout uid=${uid} session=${session.id}`);
  return Response.json({ url: session.url });
}

async function stripeWebhook(request: Request, env: Env): Promise<Response> {
  if (!env.STRIPE_SECRET_KEY || !env.STRIPE_WEBHOOK_SECRET) {
    return jsonError("UNKNOWN_ERROR", 500);
  }
  const stripe = new Stripe(env.STRIPE_SECRET_KEY);
  const signature = request.headers.get("stripe-signature");
  if (!signature) {
    return jsonError("UNKNOWN_ERROR", 400);
  }
  const event = stripe.webhooks.constructEvent(await request.text(), signature, env.STRIPE_WEBHOOK_SECRET);
  console.log(`index: stripe event ${event.type}`);
  if (event.type === "invoice.payment_failed") {
    console.log("index: payment_failed keeps current plan");
    return Response.json({ ok: true });
  }
  if (event.type === "checkout.session.completed" || event.type === "customer.subscription.updated") {
    const obj = event.data.object as { metadata?: { firebase_uid?: string }; client_reference_id?: string; customer?: string; status?: string };
    const uid = obj.metadata?.firebase_uid || obj.client_reference_id;
    if (uid && (event.type === "checkout.session.completed" || obj.status === "active")) {
      await env.DB.prepare(
        "INSERT INTO users (uid, used_bytes, plan, stripe_customer, created_at) VALUES (?, 0, 'pro', ?, ?) ON CONFLICT(uid) DO UPDATE SET plan='pro', grace_ends_at=NULL, stripe_customer=COALESCE(excluded.stripe_customer, users.stripe_customer)",
      )
        .bind(uid, typeof obj.customer === "string" ? obj.customer : null, Date.now())
        .run();
      console.log(`index: plan pro uid=${uid}`);
    }
  }
  if (event.type === "customer.subscription.deleted") {
    const obj = event.data.object as { metadata?: { firebase_uid?: string } };
    const uid = obj.metadata?.firebase_uid;
    if (uid) {
      const grace = Date.now() + 7 * 24 * 60 * 60 * 1000;
      await env.DB.prepare("UPDATE users SET plan = 'grace', grace_ends_at = ? WHERE uid = ?").bind(grace, uid).run();
      console.log(`index: grace started uid=${uid} ends=${grace}`);
    }
  }
  return Response.json({ ok: true });
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
  let cursor: string | undefined;
  do {
    const listed = await env.BUCKET.list({ prefix: "inbox/", cursor });
    for (const obj of listed.objects) {
      if (obj.uploaded.getTime() < cutoff) {
        await env.BUCKET.delete(obj.key);
        console.log(`index: inbox expired ${obj.key}`);
      }
    }
    cursor = listed.truncated ? listed.cursor : undefined;
  } while (cursor);
}
