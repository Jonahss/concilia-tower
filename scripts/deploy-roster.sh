#!/usr/bin/env bash
# Deploy web/roster-worker.js to Cloudflare (no wrangler — plain API upload).
# Token read from ~/.claude-agent/secrets/cloudflare.md ("api token:" line).
set -euo pipefail
cd "$(dirname "$0")/.."

ACC=81cbdeb7e3beb962e747502fde5a4b89
KV_TOWERS=a58cb0816b9f455bb823eb297037ac1f
NAME=conciliatower-roster
TOK=$(sed -n 's/^api token: //p' ~/.claude-agent/secrets/cloudflare.md)

META=$(cat <<EOF
{"main_module":"worker.js",
 "compatibility_date":"2026-08-01",
 "bindings":[{"type":"kv_namespace","name":"TOWERS","namespace_id":"$KV_TOWERS"}]}
EOF
)

curl -sf -X PUT \
  -H "Authorization: Bearer $TOK" \
  -F "metadata=$META;type=application/json" \
  -F "worker.js=@web/roster-worker.js;type=application/javascript+module;filename=worker.js" \
  "https://api.cloudflare.com/client/v4/accounts/$ACC/workers/scripts/$NAME" \
  | python3 -c "import json,sys;d=json.load(sys.stdin);print('deployed' if d['success'] else d['errors'])"

# Ensure the workers.dev URL is enabled (idempotent).
curl -sf -X POST \
  -H "Authorization: Bearer $TOK" -H "Content-Type: application/json" \
  -d '{"enabled":true,"previews_enabled":false}' \
  "https://api.cloudflare.com/client/v4/accounts/$ACC/workers/scripts/$NAME/subdomain" \
  | python3 -c "import json,sys;d=json.load(sys.stdin);print('workers.dev on' if d['success'] else d['errors'])"

echo "https://$NAME.jonahss.workers.dev/"
