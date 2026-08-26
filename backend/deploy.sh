#!/bin/zsh
set -euo pipefail
cd "$(dirname "$0")"
echo "deploy: npm install"
npm install
echo "deploy: wrangler deploy"
npx wrangler deploy
echo "deploy: set Cloudflare billing alerts at \$50 and \$80 in the dashboard"
echo "deploy: Cache Rules on the R2 custom domain: Cache Everything + Ignore query string for /p/* and /s/*"
