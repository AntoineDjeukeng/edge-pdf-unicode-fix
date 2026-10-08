#!/usr/bin/env bash
set -u

show() {
  local p="$1"
  if [[ -e "$p" ]]; then
    du -sh "$p" 2>/dev/null || true
  fi
}

echo "Development/build trees (review before deleting):"
show "$HOME/Desktop/chromium_unicode_fix"
show "$HOME/Desktop/pdfium_unicode_fix"
show "$HOME/Desktop/m154_edge_machine_diff_bundle"
show "$HOME/Desktop/m154_edge_machine_diff_bundle.tar.gz"

echo
echo "Installed runtime fix (KEEP while using the fix):"
show "$HOME/.local/lib/edge-pdf-unicode"
show "$HOME/.local/bin/msedge-pdf-fixed"
show "$HOME/.local/share/applications/microsoft-edge.desktop"
