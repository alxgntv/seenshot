import blocklistText from "./disposable_email_blocklist.conf";

// ─── Ariadne's Thread [AT-0186] ─────────────────────
// What: Load disposable_email_blocklist.conf and match domain suffixes
// Why:  Same official list as seenshot-web; Mac API must reject throwaway mail
// Date: 2026-08-27
// Related: [AT-0029] backend/src/auth.ts:verifyFirebaseToken, seenshot-web→src/disposableEmail.ts:isDisposableEmail
// ─────────────────────────────────────────────────────

function parseBlocklist(text: string): Set<string> {
  const set = new Set<string>();
  const lines = text.split(/\r?\n/);
  for (const raw of lines) {
    const line = raw.trim();
    if (!line || line.startsWith("#") || line.startsWith("//")) {
      continue;
    }
    set.add(line.toLowerCase());
  }
  console.log(`disposableEmail: loaded domains=${set.size}`);
  return set;
}

const BLOCKLIST = parseBlocklist(blocklistText);

export function isDisposableEmail(email: string): boolean {
  const at = email.indexOf("@");
  if (at < 0) {
    console.warn("disposableEmail: missing @");
    return false;
  }
  const domain = email.slice(at + 1).trim().toLowerCase();
  const domainParts = domain.split(".");
  for (let i = 0; i < domainParts.length - 1; i += 1) {
    const candidate = domainParts.slice(i).join(".");
    if (BLOCKLIST.has(candidate)) {
      console.warn(`disposableEmail: blocked domain=${candidate} domainParts=${domainParts.length}`);
      return true;
    }
  }
  console.log(`disposableEmail: permanent domainParts=${domainParts.length}`);
  return false;
}
