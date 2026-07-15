# imagedecoder.svg

An SVG image decoder add-on for Kodi, using [lunasvg](https://github.com/sammycage/lunasvg)
(MIT licensed) for parsing/rasterization.

## Why this exists

This is a companion project to [`plex-for-kodi`](https://github.com/pannal/plex-for-kodi) /
[`skin.plextuary`](https://github.com/pannal/skin.plextuary)'s icon-font investigation (see
Goal 2 in that project's `CLAUDE.md`). Kodi has no native SVG support — skin `<texture>` tags
can only reference raster formats. But Kodi *does* have a generic, pluggable image-decoder
extension point (`kodi.imagedecoder` / `kodi::addon::CInstanceImageDecoder`) already used in
production by `imagedecoder.heif`, `imagedecoder.raw`, and `imagedecoder.mpo` — and confirmed
(by reading Kodi's actual source) that the skin texture loading path
(`CTexture::LoadFromFileInternal` → `ImageFactory::CreateLoaderFromMimeType`) already checks
this registry before falling back to the built-in FFmpeg-based decoder. Kodi's `Mime.cpp` also
already maps `.svg` → `image/svg+xml`. So the only missing piece is an actual add-on
implementing the interface — this repo.

Once installed and enabled (regardless of which skin is active — image decoders are looked up
globally by add-on type, unlike fonts, which are tied to the active skin), any `<texture>` tag
anywhere that references an `.svg` file should render through this decoder automatically, with
no core Kodi changes and no skin-side registration required.

## Status: untested scaffold (2026-07-15)

This has **not been built or run against a real Kodi instance yet**. The decoder logic
(`src/SvgPicture.cpp`) is written against the real, current `CInstanceImageDecoder` interface
and the real `lunasvg` API (both verified against their actual upstream source, not guessed),
and mirrors the structure of `imagedecoder.heif` (a real, shipped Kodi add-on) as closely as
possible. But it needs an actual CMake build against Kodi's dev-kit headers and a real lunasvg
install to confirm it compiles and works — that hasn't happened yet. IDE errors about missing
`kodi/addon-instance/ImageDecoder.h` or `lunasvg.h` are expected until that build environment
exists; they are not code bugs.

### What's implemented

- `SupportsFile()`: cheap sniff for a `<svg`/`<?xml` tag in the first 512 bytes. Real validation
  happens via lunasvg's own parser in `LoadImageFromMemory()`.
- `LoadImageFromMemory()`: parses the SVG via `lunasvg::Document::loadFromData()` and stores the
  parsed document. If Kodi passes a non-zero requested width/height, that's honored as-is
  (SVG is vector data — there's no "native" pixel size to report back, unlike a raster decoder);
  otherwise falls back to the SVG's own intrinsic width/height.
- `Decode()`: rasterizes the *already-parsed* document at exactly the width/height Kodi asks
  for via `Document::renderToBitmap()`, then converts lunasvg's ARGB32-premultiplied output to
  plain RGBA via `Bitmap::convertToRGBA()` and copies it into Kodi's output buffer. Only
  `ADDON_IMG_FMT_RGBA8` is implemented for now — that's the format Kodi's skin texture path
  needs; the other three `ADDON_IMG_FMT_*` variants can be added later if something else needs
  them.
- `ReadTag()` (EXIF-style metadata) is intentionally not implemented — not relevant for icon
  SVGs, and the interface treats it as optional (default returns `false`).

### What's not done yet

- No actual build attempted — `CMakeLists.txt`/`FindLunaSVG.cmake` follow the same pattern as
  `imagedecoder.heif`'s real, working build files, but haven't been run.
- No CI (`imagedecoder.heif` has Jenkins/Azure Pipelines configs tied to Kodi's own official
  multi-platform build farm, which this repo doesn't have access to).
- No `resources/language/resource.language.en_gb/strings.po` — the `addon.xml.in` uses a plain
  literal `<description>` instead of a numbered string reference for that reason.
- No icon/fanart assets.
- Not tested against real `.svg` files, real Kodi skin `<texture>` references, or a real Kodi
  build at all.

## Building (once a real attempt happens)

Same general shape as any other out-of-tree Kodi binary add-on:

1. A `find_package(Kodi REQUIRED)`-compatible Kodi dev-kit checkout (typically built alongside
   Kodi's own `cmake/addons` tooling, or via Kodi's binary-addons build scripts).
2. A `lunasvg` install discoverable via `pkg-config` or `find_library`/`find_path` (see
   `FindLunaSVG.cmake`).
3. Standard CMake out-of-tree build:
   ```
   mkdir build && cd build
   cmake -DADDONS_TO_BUILD=imagedecoder.svg \
         -DADDON_SRC_PREFIX=../.. \
         -DCMAKE_BUILD_TYPE=Debug \
         -DPACKAGE_ZIP=1 <path-to-kodi>/cmake/addons
   make
   ```
   (This mirrors `imagedecoder.heif`'s own build instructions — adjust once this is actually
   tried against a real Kodi checkout.)

## License

GPL-2.0-or-later, matching Kodi's own `imagedecoder.*` add-ons and `plex-for-kodi`. `lunasvg`
itself is MIT licensed.
