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
  // Cheap sniff only - lunasvg's parser does the real validation later.
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
// Kodi passes its GPU maxTextureSize as an upper bound, not a real per-control
// request, and an SVG's own viewBox is usually far too small to serve as a
// native size. Report a fixed one instead. See README for the full reasoning.
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

    // An SVG declaring a width/height larger than kNativeSize is asking to be
    // decoded at that resolution - the only way to opt one asset into
    // rendering natively on a 4K screen, since neither the control size nor
    // the output resolution ever reaches this addon. Clamped so a declaration
    // can only raise the resolution: honouring a small declared size would
    // decode an icon far below the size it is actually drawn at.
    const double longSide = std::max(docWidth, docHeight);
    const double target = std::clamp(longSide, static_cast<double>(kNativeSize),
                                     static_cast<double>(kMaxPlausibleHint));

    const double aspect = docWidth / docHeight;
    if (aspect >= 1.0)
    {
      width = static_cast<unsigned int>(target + 0.5);
      height = static_cast<unsigned int>(target / aspect + 0.5);
    }
    else
    {
      height = static_cast<unsigned int>(target + 0.5);
      width = static_cast<unsigned int>(target * aspect + 0.5);
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
    // Deliberately narrow: only the two byte orders Kodi actually asks for.
    kodi::Log(ADDON_LOG_ERROR,
              "%s: Unsupported target format (%d), only ADDON_IMG_FMT_RGBA8/A8R8G8B8 are "
              "implemented",
              __func__, static_cast<int>(format));
    return false;
  }

  kodi::Log(ADDON_LOG_DEBUG, "%s: Kodi requested decode at %ux%u", __func__, width, height);

  // Decode() gets its size from Kodi independently of LoadImageFromMemory(),
  // so re-check rather than risk a runaway allocation.
  if (width > kMaxPlausibleHint || height > kMaxPlausibleHint)
  {
    kodi::Log(ADDON_LOG_ERROR, "%s: Refusing implausible decode size %ux%u", __func__, width,
              height);
    return false;
  }

  // plutovg computes analytic coverage antialiasing at whatever size it is
  // given, so rendering straight at the target size is both cheaper and no
  // worse than supersampling and averaging back down.
  lunasvg::Bitmap bitmap =
      m_document->renderToBitmap(static_cast<int>(width), static_cast<int>(height));
  if (bitmap.isNull())
  {
    kodi::Log(ADDON_LOG_ERROR, "%s: Rendering SVG to %ux%u failed", __func__, width, height);
    return false;
  }

  const uint8_t* src = bitmap.data();
  const unsigned int srcStride = static_cast<unsigned int>(bitmap.stride());

  for (unsigned int y = 0; y < height; ++y)
  {
    const uint8_t* srcRow = src + y * srcStride;
    uint8_t* dstRow = pixels + y * pitch;
    for (unsigned int x = 0; x < width; ++x)
    {
      // lunasvg's native premultiplied ARGB32 is already B,G,R,A in memory on
      // a little-endian target.
      const uint8_t b = srcRow[x * 4 + 0];
      const uint8_t g = srcRow[x * 4 + 1];
      const uint8_t r = srcRow[x * 4 + 2];
      const uint8_t a = srcRow[x * 4 + 3];

      uint8_t* dst = dstRow + x * 4;
      if (format == ADDON_IMG_FMT_A8R8G8B8)
      {
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
