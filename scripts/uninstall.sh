#!/usr/bin/env bash
set -euo pipefail

LIBDIR="$HOME/.local/lib/edge-pdf-unicode"
WRAPPER="$HOME/.local/bin/msedge-pdf-fixed"
APPDIR="$HOME/.local/share/applications"

rm -f "$WRAPPER"
rm -rf "$LIBDIR"

for marker in "$APPDIR"/*.desktop.edge-pdf-unicode-owned; do
  [[ -e "$marker" ]] || continue
  desktop="${marker%.edge-pdf-unicode-owned}"
  backup="$desktop.edge-pdf-unicode-backup"

  if [[ -f "$backup" ]]; then
    mv -f "$backup" "$desktop"
  else
    rm -f "$desktop"
  fi
  rm -f "$marker"
done

command -v update-desktop-database >/dev/null 2>&1 && \
  update-desktop-database "$APPDIR" >/dev/null 2>&1 || true

echo "Removed the per-user Edge PDF Unicode launcher fix."
