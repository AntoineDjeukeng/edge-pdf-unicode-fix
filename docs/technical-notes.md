# Technical notes

## Verified behavior

The failing Edge-generated annotation stored the correct UTF-16 text in `/Contents`, while the generated `/AP` stream contained only the characters representable by WinAnsi Helvetica. This localized the bug to appearance generation rather than input handling or PDF serialization.

## PDFium source-level repair

The working repair used two changes.

### `core/fpdfdoc/cpdf_generateap.cpp`

For the FreeText path, create/reuse the annotation appearance resource dictionary before text layout and pass it to `CPVT_FontMap` instead of `nullptr`. Reuse the same resource dictionary when finalizing the annotation appearance.

Conceptually:

```cpp
// before text layout
// graphics_state_dict = ...
// resource_font_dict = ...
// resource_dict = GenerateResourcesDict(...)

CPVT_FontMap map(doc, resource_dict.Get(), std::move(default_font), font_name);
```

The same `resource_dict` must later be supplied to the AP-generation code so fallback fonts added by `CPVT_FontMap` are available to the generated appearance stream.

### `core/fpdfdoc/cpvt_fontmap.cpp`

In `CPVT_FontMap::SetupAnnotSysPDFFont()`:

1. Try `CPDF_InteractiveForm::AddNativeInteractiveFormFont()` normally.
2. On Linux, if it returns null, create a `CFX_Font` fallback for Central European text (`FX_Charset::kMSWin_EasternEuropean`, charset byte `0xEE`, Windows-1250).
3. Load the substitute face.
4. Add it through `CPDF_DocPageData::AddFont()`.
5. Set the alias to `FallbackEE` so it can be inserted into the annotation resource dictionary.

## Exact PDFium revision used for Edge 154 matching

```text
2358b16c1947eff67f0732754af6b3c4e1715ff9
```

## Edge 154 runtime mapping used by this workaround

Verified call sites / helpers in the analysed Linux Edge binary:

```text
GenerateFreeTextAP -> CPVT_FontMap constructor call: 0x0d2b0c51
CPVT_FontMap constructor:                         0x0d2c4470
CPVT_FontMap::SetupAnnotSysPDFFont:               0x0d2c46a0
Setup -> AddNativeInteractiveFormFont call:        0x0d2c46d3
AddNativeInteractiveFormFont:                     0x0d2b8b30
CFX_Font::LoadSubstFace:                          0x0d1224d0
CPDF_DocPageData::AddFont:                        0x0d0e9600
```

These addresses are build-specific and are guarded by byte checks in the runtime patch.

## Important limitation

This was tested as a Central-European fallback, not as a complete Unicode font-selection system. A production upstream patch should generalize charset/font fallback selection and have PDFium unit tests covering multiple scripts.
