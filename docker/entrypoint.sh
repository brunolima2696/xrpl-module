#!/usr/bin/env bash
set -euo pipefail

config_file="${XRPLD_CONFIG_FILE:-/etc/xrpld/xrpld.cfg}"

if [ "$#" -gt 0 ]; then
    exec /usr/local/bin/xrpld "$@"
fi

exec /usr/local/bin/xrpld \
    --standalone \
    --conf="$config_file"
