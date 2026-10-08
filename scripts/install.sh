#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/edge_pdf_unicode_fix.cpp"

LIBDIR="$HOME/.local/lib/edge-pdf-unicode"
BINDIR="$HOME/.local/bin"
APPDIR="$HOME/.local/share/applications"
SO="$LIBDIR/libedge_pdf_unicode_fix.so"
WRAPPER="$BINDIR/msedge-pdf-fixed"

mkdir -p "$LIBDIR" "$BINDIR" "$APPDIR"

echo "[1/4] Building the runtime PDF fix..."
g++ -std=c++17 -shared -fPIC -O2 \
  -Wall -Wextra -fcf-protection=branch \
  "$SRC" -ldl -o "$SO"

echo "[2/4] Installing Edge wrapper..."
cat > "$WRAPPER" <<EOF
#!/usr/bin/env bash
set -e
SO="\$HOME/.local/lib/edge-pdf-unicode/libedge_pdf_unicode_fix.so"
if [[ -n "\${LD_PRELOAD:-}" ]]; then
  export LD_PRELOAD="\$SO:\$LD_PRELOAD"
else
  export LD_PRELOAD="\$SO"
fi
exec /opt/microsoft/msedge/msedge "\$@"
EOF
chmod +x "$WRAPPER"

echo "[3/4] Creating a per-user override of the normal Edge desktop icon..."

SYSTEM_DESKTOP=""
for f in \
  /usr/share/applications/microsoft-edge.desktop \
  /usr/share/applications/microsoft-edge-stable.desktop \
  /usr/share/applications/com.microsoft.Edge.desktop \
  /usr/share/applications/com.microsoft.Edge.Stable.desktop
do
  if [[ -f "$f" ]]; then
    SYSTEM_DESKTOP="$f"
    break
  fi
done

if [[ -z "$SYSTEM_DESKTOP" ]]; then
  while IFS= read -r f; do
    if grep -Eq '^Exec=.*(microsoft-edge|/opt/microsoft/msedge/msedge)' "$f" 2>/dev/null; then
      SYSTEM_DESKTOP="$f"
      break
    fi
  done < <(find /usr/share/applications -maxdepth 1 -type f -iname '*edge*.desktop' -print 2>/dev/null)
fi

if [[ -n "$SYSTEM_DESKTOP" ]]; then
  BASENAME="$(basename "$SYSTEM_DESKTOP")"
  LOCAL_DESKTOP="$APPDIR/$BASENAME"

  # Preserve a pre-existing user override once.
  if [[ -f "$LOCAL_DESKTOP" && ! -f "$LOCAL_DESKTOP.edge-pdf-unicode-owned" ]]; then
    cp -a "$LOCAL_DESKTOP" "$LOCAL_DESKTOP.edge-pdf-unicode-backup"
  fi

  cp -a "$SYSTEM_DESKTOP" "$LOCAL_DESKTOP"

  python3 - "$LOCAL_DESKTOP" "$WRAPPER" <<'PY'
from pathlib import Path
import sys

p = Path(sys.argv[1])
wrapper = sys.argv[2]
out = []

for line in p.read_text(errors="replace").splitlines():
    if line.startswith("Exec="):
        cmd = line[5:]
        # Desktop Exec syntax here is simple: replace only the executable token
        # and keep all existing flags/placeholders such as --inprivate and %U.
        parts = cmd.split(maxsplit=1)
        suffix = (" " + parts[1]) if len(parts) == 2 else ""
        line = f"Exec={wrapper}{suffix}"
    out.append(line)

p.write_text("\n".join(out) + "\n")
PY

  touch "$LOCAL_DESKTOP.edge-pdf-unicode-owned"
  echo "Desktop override: $LOCAL_DESKTOP"
else
  LOCAL_DESKTOP="$APPDIR/microsoft-edge-pdf-fixed.desktop"
  cat > "$LOCAL_DESKTOP" <<EOF
[Desktop Entry]
Version=1.0
Name=Microsoft Edge (PDF Unicode Fix)
GenericName=Web Browser
Exec=$WRAPPER %U
Terminal=false
Type=Application
Icon=microsoft-edge
Categories=Network;WebBrowser;
MimeType=text/html;application/xhtml+xml;application/pdf;x-scheme-handler/http;x-scheme-handler/https;
StartupNotify=true
EOF
  touch "$LOCAL_DESKTOP.edge-pdf-unicode-owned"
  echo "No system Edge .desktop file was found; created: $LOCAL_DESKTOP"
fi

command -v update-desktop-database >/dev/null 2>&1 && \
  update-desktop-database "$APPDIR" >/dev/null 2>&1 || true

echo "[4/4] Installed."
echo
echo "IMPORTANT:"
echo "  1. Close ALL Edge windows/processes before testing."
echo "  2. Then launch Edge from the normal application icon/menu."
echo "  3. Test the PDF Add Text editor with: aěčřďťňb"
echo
echo "Why closing Edge matters: Chromium reuses an already-running browser process."
echo "If a normal unpatched Edge process is already running, a new launcher cannot retrofit it."
echo
echo "Installed wrapper: $WRAPPER"
echo "Installed library: $SO"
