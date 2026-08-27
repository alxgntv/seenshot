import { Polar } from "@polar-sh/sdk";
import { validateEvent, WebhookVerificationError } from "@polar-sh/sdk/webhooks";
import type { CustomerState } from "@polar-sh/sdk/models/components/customerstate.js";
import type { Env } from "./quota";
import { quotaStub } from "./quota";

// ─── Ariadne's Thread [AT-0276] ─────────────────────
// What: Polar SDK client for checkout, customer.state_changed, and customer delete
// Why:  Member billing replaces Stripe; Polar is the official checkout and webhook API
// Date: 2026-08-27
// Related: [AT-0277] backend→index.ts:checkout, https://polar.sh/docs/features/checkout/session, https://polar.sh/docs/integrate/customer-state
// ─────────────────────────────────────────────────────

function polarClient(env: Env): Polar {
  if (!env.POLAR_ACCESS_TOKEN) {
    console.error("polar: POLAR_ACCESS_TOKEN missing");
    throw new Error("UNKNOWN_ERROR");
  }
  return new Polar({
    accessToken: env.POLAR_ACCESS_TOKEN,
    server: "production",
  });
}

export function memberProductActive(state: CustomerState, productId: string): boolean {
  if (state.deletedAt) {
    console.log(`polar: customer deleted id=${state.id} uid=${state.externalId ?? ""}`);
    return false;
  }
  const hit = state.activeSubscriptions.some((sub) => sub.productId === productId);
  console.log(
    `polar: memberProductActive customer=${state.id} uid=${state.externalId ?? ""}` +
      ` product=${productId} subs=${state.activeSubscriptions.length} hit=${hit}`,
  );
  return hit;
}

export async function createPolarCheckoutUrl(
  request: Request,
  env: Env,
  uid: string,
  email: string,
): Promise<string> {
  let body: { customer_ip_address?: string } = {};
  try {
    const parsed = (await request.json()) as { customer_ip_address?: string };
    if (parsed && typeof parsed === "object") {
      body = parsed;
    }
  } catch (error) {
    console.log("polar: checkout empty body, use CF-Connecting-IP");
  }
  const fromBody = typeof body.customer_ip_address === "string" ? body.customer_ip_address.trim() : "";
  const fromCf = request.headers.get("CF-Connecting-IP") || "";
  const customerIpAddress = fromBody || fromCf || undefined;
  console.log(
    `polar: checkout uid=${uid} emailChars=${email.length} product=${env.POLAR_PRODUCT_ID}` +
      ` ipChars=${(customerIpAddress || "").length} ipSource=${fromBody ? "body" : fromCf ? "cf" : "none"}`,
  );
  if (!email) {
    console.error(`polar: checkout missing email uid=${uid}`);
    throw new Error("UNKNOWN_ERROR");
  }
  const polar = polarClient(env);
  const checkout = await polar.checkouts.create({
    products: [env.POLAR_PRODUCT_ID],
    externalCustomerId: uid,
    customerEmail: email,
    customerIpAddress,
    successUrl: `${env.PUBLIC_BASE_URL}/space/`,
    returnUrl: `${env.PUBLIC_BASE_URL}/space/`,
    metadata: { firebase_uid: uid },
    customerMetadata: { firebase_uid: uid },
  });
  console.log(
    `polar: checkout created uid=${uid} checkout=${checkout.id} status=${checkout.status}` +
      ` urlChars=${checkout.url.length}`,
  );
  return checkout.url;
}

export async function applyPolarCustomerState(env: Env, state: CustomerState): Promise<void> {
  const uid = state.externalId;
  if (!uid) {
    console.warn(`polar: state missing externalId customer=${state.id}`);
    return;
  }
  const member = memberProductActive(state, env.POLAR_PRODUCT_ID);
  const plan = member ? "pro" : "grace";
  console.log(
    `polar: apply state customer=${state.id} uid=${uid} member=${member} plan=${plan}` +
      ` subs=${state.activeSubscriptions.length}`,
  );
  const stub = quotaStub(env, uid);
  const response = await stub.fetch("https://quota/setPlan", {
    method: "POST",
    body: JSON.stringify({
      action: "setPlan",
      uid,
      plan,
      polarCustomer: state.id,
      graceEndsAt: member ? null : Date.now() + 7 * 24 * 60 * 60 * 1000,
    }),
  });
  const text = await response.text();
  console.log(`polar: setPlan uid=${uid} status=${response.status} bodyChars=${text.length}`);
  if (!response.ok) {
    throw new Error("UNKNOWN_ERROR");
  }
}

export async function handlePolarWebhook(request: Request, env: Env): Promise<Response> {
  if (!env.POLAR_WEBHOOK_SECRET) {
    console.error("polar: POLAR_WEBHOOK_SECRET missing");
    return Response.json({ code: "UNKNOWN_ERROR", message: "Something went wrong. Try again." }, { status: 500 });
  }
  const body = await request.text();
  const headers = {
    "webhook-id": request.headers.get("webhook-id") || "",
    "webhook-timestamp": request.headers.get("webhook-timestamp") || "",
    "webhook-signature": request.headers.get("webhook-signature") || "",
  };
  console.log(
    `polar: webhook bytes=${body.length} idChars=${headers["webhook-id"].length}` +
      ` tsChars=${headers["webhook-timestamp"].length} sigChars=${headers["webhook-signature"].length}`,
  );
  let event;
  try {
    event = validateEvent(body, headers, env.POLAR_WEBHOOK_SECRET);
  } catch (error) {
    if (error instanceof WebhookVerificationError) {
      console.warn(`polar: webhook signature rejected ${error.message}`);
      return new Response("", { status: 403 });
    }
    console.error("polar: webhook validate failed", error);
    return Response.json({ code: "UNKNOWN_ERROR", message: "Something went wrong. Try again." }, { status: 400 });
  }
  console.log(`polar: webhook type=${event.type}`);
  if (event.type === "customer.state_changed") {
    await applyPolarCustomerState(env, event.data);
  } else {
    console.log(`polar: ignore event ${event.type}`);
  }
  return new Response("", { status: 202 });
}

export async function deletePolarCustomer(env: Env, uid: string): Promise<void> {
  if (!env.POLAR_ACCESS_TOKEN) {
    console.log(`polar: skip customer delete, no token uid=${uid}`);
    return;
  }
  try {
    const polar = polarClient(env);
    await polar.customers.deleteExternal({ externalId: uid, anonymize: true });
    console.log(`polar: customer deleted uid=${uid}`);
  } catch (error) {
    console.warn(`polar: customer delete failed uid=${uid}`, error);
  }
}
