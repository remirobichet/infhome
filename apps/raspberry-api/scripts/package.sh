#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
version=$(date -u +%Y%m%dT%H%M%SZ)
mkdir -p "$root/releases"
archive="$root/releases/infhome-api-$version.tar.gz"
tar -czf "$archive" -C "$root" dist deploy .env.example README.md
printf '%s\n' "$archive"
