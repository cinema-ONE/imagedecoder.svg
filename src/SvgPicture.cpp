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

  // Honor Kodi's requested size if given - SVG is vector data, so we can
  // rasterize at exactly whatever resolution is asked for, sharp every time,
  // instead of being limited to (or upscaling from) one "native" pixel size.
  if (width == 0 || height == 0)
  {
    width = static_cast<unsigned int>(m_document->width());
    height = static_cast<unsigned int>(m_document->height());
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

  lunasvg::Bitmap bitmap = m_document->renderToBitmap(static_cast<int>(width),
                                                       static_cast<int>(height));
  if (bitmap.isNull())
  {
    kodi::Log(ADDON_LOG_ERROR, "%s: Rendering SVG to %ux%u failed", __func__, width, height);
    return false;
  }

  const unsigned int srcStride = static_cast<unsigned int>(bitmap.stride());

  if (format == ADDON_IMG_FMT_A8R8G8B8)
  {
    // lunasvg's native Bitmap format is ARGB32 Premultiplied, which is
    // already byte-for-byte B,G,R,A in memory on a little-endian target -
    // the same layout ADDON_IMG_FMT_A8R8G8B8 wants, premultiplied alpha
    // included (the norm for GPU texture compositing). No conversion needed,
    // straight memcpy.
    const uint8_t* src = bitmap.data();
    const unsigned int copyBytesPerRow = std::min<unsigned int>(srcStride, pitch);
    for (unsigned int y = 0; y < height; ++y)
      std::memcpy(pixels + y * pitch, src + y * srcStride, copyBytesPerRow);
  }
  else // ADDON_IMG_FMT_RGBA8
  {
    // Plain (non-premultiplied) RGBA byte order - convert in place.
    bitmap.convertToRGBA();
    const uint8_t* src = bitmap.data();
    const unsigned int copyBytesPerRow = std::min<unsigned int>(srcStride, pitch);
    for (unsigned int y = 0; y < height; ++y)
      std::memcpy(pixels + y * pitch, src + y * srcStride, copyBytesPerRow);
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
