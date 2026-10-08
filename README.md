# Edge PDF FreeText Unicode Fix (Linux)

Runtime workaround for a PDFium FreeText appearance-generation bug observed in **Microsoft Edge 154.0.4258.37 on Linux**.

## Symptom

Edge's built-in PDF **Add Text** editor accepts Czech text while the editor is active, but some Central-European characters disappear after the annotation is committed. A minimal reproducer is:

```text
aěčřďťňb
```

The PDF still contains the Unicode text in `/Contents`, but the generated appearance stream loses characters that cannot be represented by the default WinAnsi Helvetica font.

## Root cause

For FreeText annotations, PDFium constructs a `CPVT_FontMap` without the annotation appearance resource dictionary. On Linux, `AddNativeInteractiveFormFont()` can then fail to provide a suitable fallback font. The appearance stream is consequently generated with a WinAnsi Type1 font and unsupported characters are dropped.

The working source-level fix has two parts:

1. Pass the FreeText appearance `resource_dict` into `CPVT_FontMap` instead of `nullptr`.
2. If the native interactive-form font lookup fails, create a Central-European fallback font using Windows-1250 / charset `0xEE`, add it to `CPDF_DocPageData`, and expose it to the annotation resource dictionary under `FallbackEE`.

## What this repository contains

This repository implements the same behavior as a **runtime patch** for the verified Edge build. It does **not** replace or edit `/opt/microsoft/msedge/msedge`.

The installer compiles a small shared library, installs it under your home directory, and creates a per-user desktop launcher override so the normal Edge icon starts Edge with the fix loaded.

## Install

```bash
chmod +x scripts/install.sh
./scripts/install.sh
pkill -f /opt/microsoft/msedge/msedge || true
```

Then start Edge normally from the application menu/icon and test `aěčřďťňb` in the PDF Add Text editor.

## Uninstall

```bash
chmod +x scripts/uninstall.sh
./scripts/uninstall.sh
```

## Safety / version scope

The runtime patch is tied to the verified machine layout of **Edge 154.0.4258.37**. Before modifying code in memory it checks the original bytes at the relevant call sites. If a future Edge build no longer matches, the patch refuses to install rather than patching unknown instructions.

The runtime implementation is therefore a workaround, not a generally portable PDFium patch. For a maintainable upstream solution, apply the source-level fix described in `docs/technical-notes.md` to the matching PDFium revision and rebuild normally.

## Repository status

The Czech/Central-European test path is proven for `ě č ř ď ť ň` and ordinary Latin characters. It should not be presented as a complete general-Unicode solution without additional testing.
