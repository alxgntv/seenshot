// ─── Ariadne's Thread [AT-0030] ─────────────────────
// What: AWS SigV4 presigned PUT for R2 inbox objects
// Why:  Client uploads PNG without buffering it in the Worker
// Date: 2026-08-25
// Related: backend/src/index.ts
// ─────────────────────────────────────────────────────

async function sha256Hex(data: string): Promise<string> {
  const bytes = new TextEncoder().encode(data);
  const hash = await crypto.subtle.digest("SHA-256", bytes);
  return [...new Uint8Array(hash)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

async function hmac(key: ArrayBuffer | Uint8Array, data: string): Promise<ArrayBuffer> {
  const raw = key instanceof Uint8Array ? key : new Uint8Array(key);
  const cryptoKey = await crypto.subtle.importKey("raw", raw, { name: "HMAC", hash: "SHA-256" }, false, ["sign"]);
  return crypto.subtle.sign("HMAC", cryptoKey, new TextEncoder().encode(data));
}

export async function presignPut(opts: {
  accountId: string;
  accessKeyId: string;
  secretAccessKey: string;
  bucket: string;
  objectKey: string;
  expiresSeconds?: number;
}): Promise<string> {
  const expires = opts.expiresSeconds ?? 900;
  const host = `${opts.accountId}.r2.cloudflarestorage.com`;
  const now = new Date();
  const amzDate = now.toISOString().replace(/[-:]/g, "").replace(/\.\d+Z$/, "Z");
  const dateStamp = amzDate.slice(0, 8);
  const credential = `${opts.accessKeyId}/${dateStamp}/auto/s3/aws4_request`;
  const canonicalUri = `/${opts.bucket}/${opts.objectKey.split("/").map(encodeURIComponent).join("/")}`;
  const signedHeaders = "host";
  const query = [
    ["X-Amz-Algorithm", "AWS4-HMAC-SHA256"],
    ["X-Amz-Credential", credential],
    ["X-Amz-Date", amzDate],
    ["X-Amz-Expires", String(expires)],
    ["X-Amz-SignedHeaders", signedHeaders],
  ]
    .sort(([a], [b]) => a.localeCompare(b))
    .map(([k, v]) => `${encodeURIComponent(k)}=${encodeURIComponent(v)}`)
    .join("&");
  const canonical = `PUT\n${canonicalUri}\n${query}\nhost:${host}\n\n${signedHeaders}\nUNSIGNED-PAYLOAD`;
  const canonicalHash = await sha256Hex(canonical);
  const stringToSign = `AWS4-HMAC-SHA256\n${amzDate}\n${dateStamp}/auto/s3/aws4_request\n${canonicalHash}`;
  const kDate = await hmac(new TextEncoder().encode(`AWS4${opts.secretAccessKey}`), dateStamp);
  const kRegion = await hmac(kDate, "auto");
  const kService = await hmac(kRegion, "s3");
  const kSigning = await hmac(kService, "aws4_request");
  const sigBuf = await hmac(kSigning, stringToSign);
  const signature = [...new Uint8Array(sigBuf)].map((b) => b.toString(16).padStart(2, "0")).join("");
  const url = `https://${host}${canonicalUri}?${query}&X-Amz-Signature=${signature}`;
  console.log(`presign: key=${opts.objectKey} expires=${expires}`);
  return url;
}
