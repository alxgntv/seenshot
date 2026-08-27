#!/bin/zsh
set -euo pipefail
cd "$(dirname "$0")"
echo "deploy: npm install"
npm install
echo "deploy: wrangler deploy"
npx wrangler deploy
echo "deploy: workers.dev https://seenshot-api.codemarket.workers.dev"
echo "deploy: r2.dev https://pub-453d6a740dac45a2b8df5cb9e77df3b2.r2.dev"
