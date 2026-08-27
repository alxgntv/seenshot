#!/bin/zsh
set -euo pipefail
cd "$(dirname "$0")"
echo "deploy: npm install"
npm install
echo "deploy: wrangler deploy"
npx wrangler deploy
echo "deploy: workers.dev https://seenshot-api.codemarket.workers.dev"
echo "deploy: public png https://seenshot.app/public/{id}.png"
