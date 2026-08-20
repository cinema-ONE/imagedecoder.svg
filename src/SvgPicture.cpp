/*
 *  Copyright (C) 2026 cinemaONE
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "SvgPicture.h"

#include <kodi/Filesystem.h>

#include <algorithm>
#include <cstring>

SvgPicture::SvgPicture(const kodi::addon::IInstanceInfo& instance)
  : CInstanceImageDecoder(instance)
{
}

bool SvgPicture::SupportsFile(const std::string& file)
{
  // Cheap sniff: real validation happens in LoadImageFromMemory() via
  // lunasvg's own parser. We only rule out files that can't possibly be SVG
  // (too small to contain a "<svg" tag, or missing it within a sane header
  // window) to avoid claiming support for arbitrary XML files that aren't SVG.
  kodi::vfs::CFile fileData;
  if (!fileData.OpenFile(file))
    return false;

  char header[512] = {};
  const ssize_t read = fileData.Read(header, sizeof(header) - 1);
  if (read <= 0)
    return false;

  return std::strstr(header, "<svg") != nullptr || std::strstr(header, "<?xml") != nullptr;
}

namespace
{
// Kodi's skin texture path (CTexture::LoadIImage) always calls
// LoadImageFromMemory with its GPU's maxTextureSize (e.g. 16383) as the
// in/out width/height - confirmed via kodi.log showing exactly that value
// requested for a 90px-tall control. That's a generic upper-bound cap, not a
// real per-control size hint: when the control itself has no explicit ideal
// size to pass down, Kodi falls back to whatever this function reports as
// the image's "native" size and decodes at that. Echoing the cap straight
// back (as if it were a genuine request) is what caused a later runaway
// supersampled allocation; echoing the SVG's own tiny export-time viewBox
// size (e.g. 24x24) is what caused the original blurry/aliased icon, since
// that then gets GPU-upscaled ~4x with no say from us. Neither the cap nor
// the raw viewBox is a usable "native size" for a vector image, so we report
// a fixed, decent resolution instead whenever the incoming hint doesn't look
// like a genuine specific request.
constexpr unsigned int kNativeSize = 512;
constexpr unsigned int kMaxPlausibleHint = 4096; // no UI icon control asks for more than this
} // namespace

bool SvgPicture::LoadImageFromMemory(const std::string& mimetype,
                                     const uint8_t* buffer,
                                     size_t bufSize,
                                     unsigned int& width,
                                     unsigned int& height)
{
  m_document =
      lunasvg::Document::loadFromData(reinterpret_cast<const char*>(buffer), bufSize);
  if (!m_document)
  {
    kodi::Log(ADDON_LOG_ERROR, "%s: Failed to parse SVG data", __func__);
    return false;
  }

  if (width == 0 || height == 0 || width > kMaxPlausibleHint || height > kMaxPlausibleHint)
  {
    double docWidth = m_document->width();
    double docHeight = m_document->height();
    if (docWidth <= 0 || docHeight <= 0)
    {
      docWidth = kNativeSize;
      docHeight = kNativeSize;
    }

    const double aspect = docWidth / docHeight;
    if (aspect >= 1.0)
    {
      width = kNativeSize;
      height = static_cast<unsigned int>(kNativeSize / aspect + 0.5);
    }
    else
    {
      height = kNativeSize;
      width = static_cast<unsigned int>(kNativeSize * aspect + 0.5);
    }
  }

  if (width == 0 || height == 0)
  {
    kodi::Log(ADDON_LOG_ERROR, "%s: SVG has no usable intrinsic size and none was requested",
              __func__);
    return false;
  }

  return true;
}

bool SvgPicture::Decode(uint8_t* pixels,
                        unsigned int width,
                        unsigned int height,
                        unsigned int pitch,
                        ADDON_IMG_FMT format)
{
  if (!m_document)
    return false;

  if (format != ADDON_IMG_FMT_RGBA8 && format != ADDON_IMG_FMT_A8R8G8B8)
  {
    // Kodi's skin texture path actually requests ADDON_IMG_FMT_A8R8G8B8, not
    // RGBA8 as originally assumed - keeping this deliberately narrow to just
    // the two byte orders actually observed/needed rather than all four
    // ADDON_IMG_FMT_* variants.
    kodi::Log(ADDON_LOG_ERROR,
              "%s: Unsupported target format (%d), only ADDON_IMG_FMT_RGBA8/A8R8G8B8 are "
              "implemented",
              __func__, static_cast<int>(format));
    return false;
  }

  kodi::Log(ADDON_LOG_DEBUG, "%s: Kodi requested decode at %ux%u", __func__, width, height);

  // Hard safety net: LoadImageFromMemory() is what should normally keep this
  // sane (see kMaxPlausibleHint there), but Decode()'s width/height come from
  // Kodi independently of that - refuse anything absurd outright rather than
  // ever again attempting a multi-gigabyte supersampled allocation.
  if (width > kMaxPlausibleHint || height > kMaxPlausibleHint)
  {
    kodi::Log(ADDON_LOG_ERROR, "%s: Refusing implausible decode size %ux%u", __func__, width,
              height);
    return false;
  }

  // plutovg (lunasvg's rasterizer) is a fork of FreeType's own "smooth"
  // scanline rasterizer and always renders with AA on, so it isn't that the
  // SVG comes out unantialiased - but its coverage-to-alpha mapping is a
  // plain linear one, unlike FreeType's text path which is perceptually
  // gamma-tuned, so edges read as harder even at the same target resolution.
  // Rendering at a higher internal resolution and box-averaging back down
  // closes most of that gap. The averaging is exact here because lunasvg's
  // native bitmap is premultiplied alpha - a plain per-channel mean over each
  // NxN block is a correct downsample with no straight-alpha color bleed at
  // partially-transparent edges.
  constexpr unsigned int kSupersample = 4;
  const unsigned int renderWidth = width * kSupersample;
  const unsigned int renderHeight = height * kSupersample;

  lunasvg::Bitmap hiRes = m_document->renderToBitmap(static_cast<int>(renderWidth),
                                                      static_cast<int>(renderHeight));
  if (hiRes.isNull())
  {
    kodi::Log(ADDON_LOG_ERROR, "%s: Rendering SVG to %ux%u failed", __func__, renderWidth,
              renderHeight);
    return false;
  }

  constexpr unsigned int kSamples = kSupersample * kSupersample;
  const uint8_t* src = hiRes.data();
  const unsigned int srcStride = static_cast<unsigned int>(hiRes.stride());

  for (unsigned int y = 0; y < height; ++y)
  {
    uint8_t* dstRow = pixels + y * pitch;
    for (unsigned int x = 0; x < width; ++x)
    {
      unsigned int sumB = 0, sumG = 0, sumR = 0, sumA = 0;
      for (unsigned int sy = 0; sy < kSupersample; ++sy)
      {
        const uint8_t* srcPx = src + (y * kSupersample + sy) * srcStride + (x * kSupersample) * 4;
        for (unsigned int sx = 0; sx < kSupersample; ++sx)
        {
          sumB += srcPx[0];
          sumG += srcPx[1];
          sumR += srcPx[2];
          sumA += srcPx[3];
          srcPx += 4;
        }
      }
      // lunasvg's native premultiplied ARGB32 is already B,G,R,A in memory on
      // a little-endian target.
      const uint8_t b = static_cast<uint8_t>((sumB + kSamples / 2) / kSamples);
      const uint8_t g = static_cast<uint8_t>((sumG + kSamples / 2) / kSamples);
      const uint8_t r = static_cast<uint8_t>((sumR + kSamples / 2) / kSamples);
      const uint8_t a = static_cast<uint8_t>((sumA + kSamples / 2) / kSamples);

      uint8_t* dst = dstRow + x * 4;
      if (format == ADDON_IMG_FMT_A8R8G8B8)
      {
        // Same premultiplied B,G,R,A layout this format wants - no
        // conversion needed.
        dst[0] = b;
        dst[1] = g;
        dst[2] = r;
        dst[3] = a;
      }
      else // ADDON_IMG_FMT_RGBA8: plain (non-premultiplied) byte order
      {
        if (a == 0)
        {
          dst[0] = dst[1] = dst[2] = dst[3] = 0;
        }
        else
        {
          dst[0] = static_cast<uint8_t>(std::min(255u, (r * 255u + a / 2) / a));
          dst[1] = static_cast<uint8_t>(std::min(255u, (g * 255u + a / 2) / a));
          dst[2] = static_cast<uint8_t>(std::min(255u, (b * 255u + a / 2) / a));
          dst[3] = a;
        }
      }
    }
  }

  return true;
}

class ATTR_DLL_LOCAL CSvgAddon : public kodi::addon::CAddonBase
{
public:
  CSvgAddon() = default;
  ADDON_STATUS CreateInstance(const kodi::addon::IInstanceInfo& instance,
                              KODI_ADDON_INSTANCE_HDL& hdl) override
  {
    if (instance.IsType(ADDON_INSTANCE_IMAGEDECODER))
    {
      hdl = new SvgPicture(instance);
      return ADDON_STATUS_OK;
    }

    return ADDON_STATUS_NOT_IMPLEMENTED;
  }
};

ADDONCREATOR(CSvgAddon)
