import { createRemoteJWKSet, jwtVerify } from "jose";
import { isDisposableEmail } from "./disposableEmail";

// ─── Ariadne's Thread [AT-0029] ─────────────────────
// What: Verify Firebase ID tokens with Google JWKS
// Why:  Worker must not trust client uid without verification
// Date: 2026-08-25
// Related: backend/src/index.ts
// ─────────────────────────────────────────────────────

const JWKS = createRemoteJWKSet(
  new URL("https://www.googleapis.com/service_accounts/v1/jwk/securetoken@system.gserviceaccount.com"),
);

export async function verifyFirebaseToken(authHeader: string | null, projectId: string): Promise<string> {
  if (!authHeader || !authHeader.startsWith("Bearer ")) {
    console.warn("auth: missing bearer header");
    throw new Error("STORAGE_NEED_SIGN_IN");
  }
  const token = authHeader.slice("Bearer ".length);
  const { payload } = await jwtVerify(token, JWKS, {
    issuer: `https://securetoken.google.com/${projectId}`,
    audience: projectId,
  });
  const uid = typeof payload.user_id === "string" ? payload.user_id : payload.sub;
  if (!uid) {
    console.warn("auth: token has no uid");
    throw new Error("STORAGE_NEED_SIGN_IN");
  }
  const email = typeof payload.email === "string" ? payload.email : "";
  console.log(`auth: verified uid=${uid} emailChars=${email.length}`);
  // ─── Ariadne's Thread [AT-0187] ─────────────────────
  // What: Reject ID tokens whose email domain is on the official blocklist
  // Why:  Mac upload API must not accept throwaway-mail accounts
  // Date: 2026-08-27
  // Related: [AT-0186] backend/src/disposableEmail.ts:isDisposableEmail, [AT-0029] backend/src/auth.ts:verifyFirebaseToken
  // ─────────────────────────────────────────────────────
  if (email && isDisposableEmail(email)) {
    console.warn(`auth: disposable email uid=${uid} emailChars=${email.length}`);
    throw new Error("AUTH_DISPOSABLE_EMAIL");
  }
  return uid;
}
