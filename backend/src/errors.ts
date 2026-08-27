// ─── Ariadne's Thread [AT-0028] ─────────────────────
// What: JSON error helper with English messages
// Why:  Same codes as the Qt ErrorCatalog
// Date: 2026-08-25
// Related: client/src/errors/ErrorCatalog.cpp
// ─────────────────────────────────────────────────────

const MESSAGES: Record<string, string> = {
  AUTH_REFRESH_FAILED: "Could not refresh your sign-in. Check your internet connection and try again.",
  AUTH_DISPOSABLE_EMAIL: "Please enter your permanent email address.",
  CLOUD_IMAGE_REJECTED: "The screenshot was rejected by the server. Try capturing again.",
  PUBLISH_FAILED: "Could not publish this screenshot. Try again in a moment.",
  QUOTA_EVICTED: "Oldest cloud screenshots were removed to free storage.",
  PRO_GRACE_ENDED: "Pro ended. Cloud screenshots were removed. Local files are kept.",
  ACCOUNT_DELETED: "Your SeenShot account and cloud data were deleted.",
  INBOX_EXPIRED: "The upload expired before it was confirmed. Upload the screenshot again.",
  STORAGE_NEED_SIGN_IN: "Sign in to save to the cloud or share a link.",
  UPLOAD_FAILED: "Could not upload the screenshot. Check your connection and try again.",
  EXPORT_FAILED: "Could not export your data.",
  DELETE_ACCOUNT_FAILED: "Could not delete your account. Try again.",
  UNKNOWN_ERROR: "Something went wrong. Try again.",
};

export function jsonError(code: string, status = 400): Response {
  const message = MESSAGES[code] ?? MESSAGES.UNKNOWN_ERROR;
  console.log(`errors: code=${code} status=${status} message=${message}`);
  return Response.json({ code, message }, { status });
}
