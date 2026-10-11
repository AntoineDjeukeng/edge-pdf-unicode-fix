#!/usr/bin/env bash
set -euo pipefail

EXPECTED_REV="2358b16c1947eff67f0732754af6b3c4e1715ff9"
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PATCH_FILE="$PROJECT_ROOT/patches/pdfium-m154-unicode-composite-fallback.patch"
PDFIUM_DIR="${1:-}"

if [[ -z "$PDFIUM_DIR" ]]; then
  echo "Usage: $0 /path/to/pdfium"
  exit 2
fi

cd "$PDFIUM_DIR"
[[ -d .git ]] || { echo "Not a git checkout: $PDFIUM_DIR"; exit 2; }

HEAD_REV="$(git rev-parse HEAD)"
if [[ "$HEAD_REV" != "$EXPECTED_REV" ]]; then
  echo "Refusing to patch the wrong PDFium revision."
  echo "Expected: $EXPECTED_REV"
  echo "Current : $HEAD_REV"
  exit 3
fi

if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
  echo "Refusing to patch a checkout with tracked modifications."
  git status --short --untracked-files=no
  exit 4
fi

git apply --check "$PATCH_FILE"
git apply "$PATCH_FILE"

echo
echo "Applied Unicode composite fallback patch to PDFium M154."
echo "Changed files:"
git status --short

echo
echo "If this checkout already has an out/UnicodeFix build directory, test with:"
echo "  autoninja -C out/UnicodeFix pdfium_test -j 4"
echo
echo "This patches PDFium source only; it does not modify an installed Microsoft Edge binary."
