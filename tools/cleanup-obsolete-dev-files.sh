#!/usr/bin/env bash
set -euo pipefail

MODE="${1:-dry-run}"
if [[ "$MODE" != "dry-run" && "$MODE" != "--apply" ]]; then
  echo "Usage: $0 [dry-run|--apply]" >&2
  exit 2
fi

# Only artifacts from the diagnostic/reverse-engineering iterations.
# The installed runtime fix under ~/.local is deliberately NOT touched.
FILES=(
  "$HOME/Downloads/build_m154_edge_machine_diff.sh"
  "$HOME/Downloads/build_m154_edge_machine_diff_v2.sh"
  "$HOME/Downloads/build_m154_edge_machine_diff_v3.sh"
  "$HOME/Downloads/edge_pdf_unicode_diag.cpp"
  "$HOME/Downloads/run_edge_pdf_unicode_diag.sh"
  "$HOME/Downloads/EDGE_PDF_UNICODE_DIAG_README.txt"
  "$HOME/Downloads/edge_pdf_unicode_diag_v2.cpp"
  "$HOME/Downloads/run_edge_pdf_unicode_diag_v2.sh"
  "$HOME/Downloads/edge_pdf_unicode_final_candidate.cpp"
  "$HOME/Downloads/run_edge_pdf_unicode_final_candidate.sh"
  "$HOME/Downloads/libedge_pdf_unicode_diag.so"
  "$HOME/Downloads/libedge_pdf_unicode_diag_v2.so"
  "$HOME/Downloads/libedge_pdf_unicode_final_candidate.so"
  "$HOME/Desktop/m154_edge_machine_diff_bundle.tar.gz"
)

DIRS=(
  "$HOME/Desktop/m154_edge_machine_diff_bundle"
  "/tmp/edge-pdf-unicode-diag"
  "/tmp/edge-pdf-unicode-diag-v2"
  "/tmp/edge-pdf-unicode-final-test"
)

remove_one() {
  local p="$1"
  [[ -e "$p" ]] || return 0
  if [[ "$MODE" == "--apply" ]]; then
    rm -rf -- "$p"
    echo "removed: $p"
  else
    echo "would remove: $p"
  fi
}

for p in "${FILES[@]}"; do remove_one "$p"; done
for p in "${DIRS[@]}"; do remove_one "$p"; done

echo
if [[ "$MODE" == "dry-run" ]]; then
  echo "Dry run only. If the list is correct, run:"
  echo "  $0 --apply"
else
  echo "Obsolete diagnostic artifacts removed."
fi

echo
echo "NOT removed automatically:"
echo "  ~/Desktop/chromium_unicode_fix"
echo "  ~/Desktop/pdfium_unicode_fix"
echo "Those can be very large, but review them with tools/audit-space.sh first."
