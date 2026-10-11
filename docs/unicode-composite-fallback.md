# v0.2 source patch: Unicode composite fallback

Target PDFium revision:

`2358b16c1947eff67f0732754af6b3c4e1715ff9` (`chromium/8037_57`, used by Edge/Chromium 154 family)

## What changes

The old workaround creates a simple one-byte Windows-1250 fallback. That is sufficient for Czech characters such as `ě č ř ď ť ň`, but it cannot represent a symbol such as `☹` (`U+2639`).

The v0.2 source patch instead creates a real embedded PDF composite font:

- `/Subtype /Type0`
- `/Encoding /Identity-H`
- `CIDFontType2` descendant
- embedded TrueType `/FontFile2`
- widths indexed by CID/glyph id
- `/ToUnicode` map

The construction deliberately mirrors PDFium's existing composite-font loader used by `FPDFText_LoadFont(..., cid=true)`.

It also keeps the earlier FreeText resources fix: the same appearance `/Resources` dictionary is passed into `CPVT_FontMap` before text layout, so any fallback font selected during layout is present when `/AP` is emitted.

## Font selection

On Linux the patch tries a broad installed TrueType font in this order:

1. DejaVu Sans
2. Noto Sans
3. Arial Unicode MS

The candidate must contain both `U+011B` (`ě`) and `U+2639` (`☹`) and expose embeddable raw font bytes through `CFX_Font::GetFontSpan()`.

The generated font is cached in the document AcroForm DR as `FallbackUnicode` so multiple FreeText annotations reuse it.

## What `☹️` means

`☹️` is two code points: `U+2639` plus variation selector `U+FE0F`. The PDFium FreeText path in this revision operates on UTF-16 code units. With this patch, `U+2639` can render through the composite font. `U+FE0F` normally has no standalone glyph, so it contributes zero visible output. The result is a monochrome `☹` appearance while the annotation `/Contents` still retains the original Unicode string.

This is **not** full color-emoji support and does not solve ZWJ sequences or supplementary-plane emoji generally.

## Apply

From this repository:

```bash
./scripts/apply-pdfium-m154-source-patch.sh /path/to/pdfium
```

The script refuses to touch any revision except the exact M154 PDFium commit and refuses a checkout with tracked local changes.

## Test string

Use:

```text
abc ěščřžýáíéďťň ☹ ☹️ ☺ ✓ ✔ ★ → ← ↑ ↓ XYZ
```

Verify:

1. visible during editing;
2. visible after clicking outside;
3. visible after save and reopen;
4. `/Contents` still contains the original Unicode text;
5. `/AP/Resources/Font/FallbackUnicode` is a Type0 font and has `/ToUnicode`.

## Installed Edge

This patch is a **source-level PDFium fix**. Applying it to a PDFium checkout does not alter `/opt/microsoft/msedge/msedge`. Microsoft Edge must contain a rebuilt/adopted PDFium implementation for the source patch to affect the installed browser. The existing `LD_PRELOAD` runtime workaround remains a separate Edge-build-specific layer.
