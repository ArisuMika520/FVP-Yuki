#pragma once

#include "common.h"
#include "text_codec.h"

#include <optional>
#include <string>
#include <vector>

namespace packcpp
{

    struct LineRecord
    {
        int lineId = 0;
        int stringSlot = 0;
        int opcodeAddr = 0;
        std::string sourceFile;
        std::string text;
        std::optional<int> voiceId;
        std::optional<std::string> voiceAsset;
        std::optional<std::string> voiceContainerHint;
        std::string textKind;
        std::string encoding;
        bool isTitle = false;
    };

    struct RepackOptions
    {
        std::string inputFormat;
        std::string textEncoding = "gbk";
    };

    std::vector<LineRecord> ExtractText(const fs::path &inputPath, int entryOffset, const fs::path &outputDir, DecodeMode decodeMode, const ProgressCallback &progress = {});
    std::vector<std::optional<ByteBuffer>> LoadReplacements(const fs::path &inputPath, const std::string &explicitFormat, const std::string &textEncoding);
    void RepackText(const fs::path &hcbPath, const fs::path &translationPath, const fs::path &outputPath, const RepackOptions &options, const ProgressCallback &progress = {});

} // namespace packcpp
