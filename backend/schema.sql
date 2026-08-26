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
