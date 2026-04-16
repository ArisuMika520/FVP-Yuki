#pragma once

#include "common.h"

#include <optional>
#include <utility>

namespace packcpp
{

    struct Hzc1Metadata
    {
        uint32_t uncompressedSize = 0;
        uint32_t headerSize = 0;
        uint16_t unknown1 = 0;
        uint16_t bppFlag = 0;
        uint16_t width = 0;
        uint16_t height = 0;
        uint16_t offsetX = 0;
        uint16_t offsetY = 0;
        uint16_t unknown2 = 0;
        uint16_t unknown3 = 0;
        ByteBuffer payloadHeader;
        bool valid = false;

        int BitsPerPixel() const
        {
            return bppFlag == 1 ? 32 : 24;
        }

        int ChannelCount() const
        {
            return BitsPerPixel() == 32 ? 4 : 3;
        }

        size_t ExpectedRawSize() const
        {
            return static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(ChannelCount());
        }
    };

    struct Hzc1DecodeResult
    {
        Hzc1Metadata metadata;
        ByteBuffer rawPixels;
        ByteBuffer trailingPadding;
    };

    Hzc1Metadata ParseHzc1Metadata(const ByteBuffer &data);
    Hzc1DecodeResult DecodeHzc1(const ByteBuffer &data);
    void ExportPngPreview(const ByteBuffer &data, const fs::path &outputPath);
    std::pair<ByteBuffer, Hzc1Metadata> RebuildHzc1FromPng(const ByteBuffer &originalHzc1, const fs::path &pngPath, int compressionLevel = 9);
    fs::path PreviewSingleHzc1(const fs::path &inputPath, const std::optional<fs::path> &outputPng = std::nullopt);
    fs::path RebuildSingleHzc1(const fs::path &originalHzc1, const fs::path &inputPng, const std::optional<fs::path> &outputHzc1 = std::nullopt);

} // namespace packcpp
