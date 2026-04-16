#pragma once

#include "common.h"

#include <optional>
#include <string>
#include <vector>

namespace packcpp
{

    struct ManifestLayout
    {
        std::string rawDir = "raw";
        std::string previewDir = "preview";
        std::string rebuiltDir = "rebuilt";
    };

    struct ManifestEntry
    {
        int index = 0;
        std::string archiveName;
        std::string archiveNameHex;
        std::string normalizedName;
        uint32_t nameOffset = 0;
        uint32_t dataOffset = 0;
        uint32_t dataSize = 0;
        std::string assetType = "application/octet-stream";
        std::string magicHex;
        std::string sha256;
        std::string rawPath;
        std::optional<std::string> previewPngPath;
        std::optional<std::string> previewPngSha256;
        std::string originalFormat;
    };

    struct AssetManifest
    {
        fs::path manifestPath;
        std::string schema;
        std::string sourceContainer;
        std::optional<fs::path> sourcePath;
        ManifestLayout layout;
        std::string containerSha256;
        uint32_t entryCount = 0;
        uint32_t stringTableSize = 0;
        uint64_t containerSize = 0;
        std::string stringTableSha256;
        std::string stringTableHex;
        std::vector<ManifestEntry> entries;
    };

    struct BinRepackOptions
    {
        bool autoRebuildImages = false;
        bool persistRebuiltImages = true;
    };

    struct BinRepackResult
    {
        fs::path outputPath;
        fs::path reportPath;
    };

    struct BuildPatchOptions
    {
        fs::path outputDir = L"patch_build";
        bool autoRebuildImages = false;
        bool includeUnchanged = false;
        std::optional<fs::path> translationFile;
        std::string translationFormat;
        std::optional<fs::path> hcbFile;
        std::optional<fs::path> textOutput;
        std::string textEncoding = "gbk";
    };

    std::string DefaultExtractCategory(const fs::path &binFile);
    fs::path DefaultExtractParent(const fs::path &binFile);
    fs::path DefaultTextExtractDir();
    void ExtractBin(const fs::path &inputPath, const fs::path &outputRoot, const ProgressCallback &progress = {});
    fs::path RebuildImages(const fs::path &manifestOrDir, const std::optional<fs::path> &outputDir = std::nullopt);
    BinRepackResult RepackBin(const fs::path &manifestOrDir, const std::optional<fs::path> &outputBin = std::nullopt, const BinRepackOptions &options = {}, const ProgressCallback &progress = {});
    fs::path BuildPatch(const std::vector<fs::path> &inputs, const BuildPatchOptions &options = {}, const ProgressCallback &progress = {});
    std::vector<fs::path> DiscoverManifestPaths(const std::vector<fs::path> &inputs);

} // namespace packcpp
