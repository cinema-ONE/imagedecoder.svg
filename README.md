# imagedecoder.svg addon for Kodi

This is a [Kodi](https://kodi.tv) image decoder addon for SVG (Scalable Vector Graphics)
images, rasterizing via [lunasvg](https://github.com/sammycage/lunasvg) (MIT licensed).

Kodi has no built-in SVG support — a skin `<texture>` tag can only reference raster formats.
This addon plugs into Kodi's existing, generic `kodi.imagedecoder` extension point (the same
one `imagedecoder.heif`, `imagedecoder.raw` and `imagedecoder.mpo` use), so once it is
installed and enabled, any `<texture>` reference to a `.svg` file renders through it
automatically — no core Kodi change and no skin-side registration required. Image decoders are
looked up globally by add-on type and MIME type, independent of which skin is active.

Because SVG is vector data, each texture is rasterized at the size Kodi actually asks for
rather than being scaled up from a fixed-size bitmap, so a single `.svg` stays sharp at any
resolution instead of needing a per-resolution PNG ladder.

## Status

Built, cross-compiled for aarch64, and confirmed working end-to-end on real hardware
(Ugoos SK1 running CoreELEC), rendering SVG textures through the normal Kodi skin texture path
with correct transparency.

> [!NOTE]
> Correct **transparency** additionally requires a core Kodi fix: `CImageDecoder` never set
> `IImage::m_hasAlpha`, so every `kodi.imagedecoder` addon's output was treated as fully
> opaque. Without that fix SVG textures render with a black background. The fix is generic
> (it benefits `imagedecoder.heif`/`.raw`/`.mpo` equally) and is being upstreamed separately
> to `xbmc/xbmc`.

## Implementation notes

Details that were established by reading Kodi's actual source and confirming on hardware, and
that are easy to get wrong if this addon is ever extended:

- **Two MIME types are declared, not one.** For a bare `<texture>foo.svg</texture>` skin
  reference, Kodi's texture loader builds the MIME type as `"image/" + <file extension>` —
  literally `image/svg` — rather than consulting its own `CMime` table, which would correctly
  give `image/svg+xml`. Both are declared in `addon.xml.in` so real skin texture loading
  actually matches.
- **The requested pixel format is `ADDON_IMG_FMT_A8R8G8B8`,** not `RGBA8` as might be assumed.
  lunasvg's native premultiplied ARGB32 bitmap is already byte-identical to this, so that path
  is a direct copy with no conversion.
- **Kodi's width/height argument is an upper bound, not a request.** The skin texture path
  passes the GPU's `maxTextureSize` (16383 observed) when the control has no explicit ideal
  size. Echoing that back as the image's "native" size causes a runaway allocation; using the
  SVG's own export-time `viewBox` (often 24x24) instead causes a blurry GPU upscale. This
  addon reports a fixed 512px long side and hard-caps implausible hints.
- **Decoding is supersampled 4x and box-averaged down.** plutovg (lunasvg's rasterizer) always
  antialiases, but its coverage-to-alpha mapping is linear, unlike FreeType's gamma-tuned text
  path, so edges otherwise read as harder than a matching font glyph. Box-averaging is exact
  here because lunasvg's bitmap is premultiplied.

## Build instructions

When building the addon you have to use the correct branch depending on which version of Kodi
you are building against. For example, if you are building the `master` branch of Kodi you
should checkout the `master` branch of this repository.

### Linux

The following instructions assume you will have built Kodi already in the `kodi-build`
directory suggested by the Kodi README.

1. `git clone https://github.com/xbmc/xbmc.git`
2. `git clone https://github.com/cinema-ONE/imagedecoder.svg.git`
3. `cd imagedecoder.svg && mkdir build && cd build`
4. `cmake -DADDONS_TO_BUILD=imagedecoder.svg -DADDON_SRC_PREFIX=../.. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=../../xbmc/kodi-build/addons -DPACKAGE_ZIP=1 ../../xbmc/cmake/addons`
5. `make`

The addon files will be placed in `../../xbmc/kodi-build/addons`, so if you build Kodi from
source and run it directly the addon will be available as a system addon.

`lunasvg` is pulled and built automatically by Kodi's addon dependency system from
`depends/common/lunasvg/`, and is linked statically, so the built addon has no external
lunasvg/plutovg runtime dependency.

## License

GPL-2.0-or-later, matching Kodi's own `imagedecoder.*` addons. `lunasvg` and `plutovg` are MIT
licensed.

`resources/icon.png` is the [W3C SVG logo](https://www.w3.org/Graphics/SVG/), designed by
Harvey Rayner for the 2006 SVG Logo Contest and adopted by W3C in 2009. Taken from
[Wikimedia Commons](https://commons.wikimedia.org/wiki/File:SVG_Logo.svg), where it is tagged
both public domain (below the threshold of originality) and
[CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/); the only change made was
rasterizing it to a 256x256 PNG. W3C's own
[SVG logo terms](https://www.w3.org/2009/08/svg-logos.html) explicitly encourage its use by
software that renders SVG natively without plug-ins, which is what this addon does.
