CREATE TABLE IF NOT EXISTS users (
  uid TEXT PRIMARY KEY,
  used_bytes INTEGER NOT NULL DEFAULT 0,
  plan TEXT NOT NULL DEFAULT 'free',
  grace_ends_at INTEGER,
  stripe_customer TEXT,
  created_at INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS shots (
  shot_id TEXT PRIMARY KEY,
  uid TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  bytes INTEGER NOT NULL,
  visibility TEXT NOT NULL,
  public_id TEXT,
  watermarked INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX IF NOT EXISTS shots_uid_created ON shots (uid, created_at);
CREATE INDEX IF NOT EXISTS shots_public ON shots (public_id);
CREATE INDEX IF NOT EXISTS users_grace ON users (grace_ends_at);

CREATE TABLE IF NOT EXISTS takedowns (
  public_id TEXT PRIMARY KEY,
  reason TEXT,
  created_at INTEGER NOT NULL
);

-- ─── Ariadne's Thread [AT-0190] ─────────────────────
-- What: One-time PKCE authorization codes for the Mac app
-- Why:  seenshot.app is the OAuth AS; codes must not live in Worker RAM
-- Date: 2026-08-27
-- Related: [AT-0037] seenshot-web→src/oauth.ts, [AT-0193] app→AuthSession.cpp
-- ─────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS oauth_authorization_codes (
  code TEXT PRIMARY KEY,
  client_id TEXT NOT NULL,
  redirect_uri TEXT NOT NULL,
  code_challenge TEXT NOT NULL,
  uid TEXT NOT NULL,
  email TEXT NOT NULL DEFAULT '',
  created_at INTEGER NOT NULL,
  expires_at INTEGER NOT NULL,
  consumed_at INTEGER
);

CREATE INDEX IF NOT EXISTS oauth_codes_expires ON oauth_authorization_codes (expires_at);
