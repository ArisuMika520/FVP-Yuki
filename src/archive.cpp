#using < System.dll>
#using < System.Web.Extensions.dll>

#include "archive.h"

#include "common.h"
#include "hcb.h"
#include "hzc1.h"

#include <msclr/marshal_cppstd.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

using namespace System;
using namespace System::Collections;
using namespace System::Collections::Generic;
using namespace System::Web::Script::Serialization;

namespace packcpp
{
    namespace
    {

        constexpr const char *kAssetManifestSchema = "fvp.asset-manifest.v1";
        constexpr const char *kBinRepackReportSchema = "fvp.bin-repack-report.v1";
        constexpr const char *kPatchBuildReportSchema = "fvp.patch-build-report.v1";
        constexpr const char *kHzc1RebuildReportSchema = "fvp.hzc1-rebuild-report.v1";

        using JsonDict = Dictionary<String ^, Object ^>;
        using JsonArray = System::Collections::IList;

        struct SelectedEntry
        {
            ManifestEntry entry;
            fs::path sourcePath;
            std::string selectedRelPath;
            std::string selectedSha256;
            std::string sourceKind;
            bool modified = false;
            std::string changeReason;
            std::string rawSha256;
            std::string rebuiltSha256;
            std::string previewSha256;
            bool previewModified = false;
            uint32_t payloadSize = 0;
            uint32_t dataOffset = 0;
            std::string assetFamily;
        };

        struct SelectionSummary
        {
            int totalEntries = 0;
            int modifiedEntries = 0;
            int unchangedEntries = 0;
            bool hasChanges = false;
            std::map<std::string, int> assetTypeCounts;
            std::map<std::string, int> modifiedAssetTypeCounts;
            std::map<std::string, int> assetFamilyCounts;
            std::map<std::string, int> modifiedAssetFamilyCounts;
            std::map<std::string, int> sourceKindCounts;
            std::map<std::string, int> modifiedSourceKindCounts;
            std::map<std::string, int> changeReasonCounts;
            std::map<std::string, int> modifiedChangeReasonCounts;
        };

        std::string ManagedStringToUtf8(String ^ text)
        {
            if (text == nullptr)
            {
                return {};
            }
            return WideToUtf8(msclr::interop::marshal_as<std::wstring>(text));
        }

        String ^ ToManagedString(const std::string &text)
        {
            const std::wstring wide = Utf8ToWide(text);
            return gcnew String(wide.c_str());
        }

        JsonDict ^ AsDict(Object ^ value, const std::string &context)
        {
            JsonDict ^ dict = dynamic_cast<JsonDict ^>(value);
            if (dict == nullptr)
            {
                throw std::runtime_error("Expected JSON object for " + context + ".");
            }
            return dict;
        }

        JsonArray ^ AsArray(Object ^ value, const std::string &context)
        {
            JsonArray ^ array = dynamic_cast<JsonArray ^>(value);
            if (array == nullptr)
            {
                throw std::runtime_error("Expected JSON array for " + context + ".");
            }
            return array;
        }

        Object ^ GetRequiredValue(JsonDict ^ dict, const wchar_t *key)
        {
            String ^ managedKey = gcnew String(key);
            if (!dict->ContainsKey(managedKey))
            {
                throw std::runtime_error("Missing JSON field: " + ManagedStringToUtf8(managedKey));
            }
            return dict[managedKey];
        }

        Object ^ GetOptionalValue(JsonDict ^ dict, const wchar_t *key)
        {
            String ^ managedKey = gcnew String(key);
            if (!dict->ContainsKey(managedKey))
            {
                return nullptr;
            }
            return dict[managedKey];
        }

        std::string GetRequiredString(JsonDict ^ dict, const wchar_t *key)
        {
            Object ^ value = GetRequiredValue(dict, key);
            if (value == nullptr)
            {
                throw std::runtime_error("JSON string field is null.");
            }
            return ManagedStringToUtf8(safe_cast<String ^>(value));
        }

        std::optional<std::string> GetOptionalString(JsonDict ^ dict, const wchar_t *key)
        {
            Object ^ value = GetOptionalValue(dict, key);
            if (value == nullptr)
            {
                return std::nullopt;
            }
            return ManagedStringToUtf8(safe_cast<String ^>(value));
        }

        int GetRequiredInt(JsonDict ^ dict, const wchar_t *key)
        {
            Object ^ value = GetRequiredValue(dict, key);
            return Convert::ToInt32(value);
        }

        bool GetOptionalBool(JsonDict ^ dict, const wchar_t *key, bool defaultValue = false)
        {
            Object ^ value = GetOptionalValue(dict, key);
            if (value == nullptr)
            {
                return defaultValue;
            }
            return Convert::ToBoolean(value);
        }

        JsonDict ^ GetOptionalDict(JsonDict ^ dict, const wchar_t *key)
        {
            Object ^ value = GetOptionalValue(dict, key);
            if (value == nullptr)
            {
                return nullptr;
            }
            return AsDict(value, ManagedStringToUtf8(gcnew String(key)));
        }

        void ReportProgress(const ProgressCallback &progress, int percent, const std::wstring &message)
        {
            if (!progress)
            {
                return;
            }
            if (percent < 0)
            {
                percent = 0;
            }
            else if (percent > 100)
            {
                percent = 100;
            }
            progress(percent, message);
        }

        fs::path ResolveManifestPath(const fs::path &manifestOrDir)
        {
            fs::path candidate = manifestOrDir;
            if (fs::is_directory(candidate))
            {
                candidate /= L"manifest.json";
            }
            if (!fs::exists(candidate))
            {
                throw std::runtime_error("Manifest not found: " + PathToUtf8(candidate));
            }
            return candidate;
        }

        AssetManifest LoadManifest(const fs::path &manifestOrDir)
        {
            const fs::path manifestPath = ResolveManifestPath(manifestOrDir);
            AssetManifest manifest;
            manifest.manifestPath = manifestPath;

            try
            {
                JavaScriptSerializer ^ serializer = gcnew JavaScriptSerializer();
                serializer->MaxJsonLength = Int32::MaxValue;
                Object ^ rootObject = serializer->DeserializeObject(ToManagedString(ReadTextFileUtf8(manifestPath)));
                JsonDict ^ root = AsDict(rootObject, "manifest root");

                manifest.schema = GetRequiredString(root, L"schema");
                manifest.sourceContainer = GetRequiredString(root, L"source_container");
                if (auto sourcePath = GetOptionalString(root, L"source_path"))
                {
                    manifest.sourcePath = Utf8Path(*sourcePath);
                }
                if (auto containerSha = GetOptionalString(root, L"container_sha256"))
                {
                    manifest.containerSha256 = *containerSha;
                }
                manifest.entryCount = static_cast<uint32_t>(GetRequiredInt(root, L"entry_count"));
                manifest.stringTableSize = static_cast<uint32_t>(GetRequiredInt(root, L"string_table_size"));
                if (Object ^ containerSize = GetOptionalValue(root, L"container_size"))
                {
                    manifest.containerSize = static_cast<uint64_t>(Convert::ToInt64(containerSize));
                }
                if (auto stringTableSha = GetOptionalString(root, L"string_table_sha256"))
                {
                    manifest.stringTableSha256 = *stringTableSha;
                }
                if (auto stringTableHex = GetOptionalString(root, L"string_table_hex"))
                {
                    manifest.stringTableHex = *stringTableHex;
                }

                if (JsonDict ^ layout = GetOptionalDict(root, L"layout"))
                {
                    if (auto rawDir = GetOptionalString(layout, L"raw_dir"))
                    {
                        manifest.layout.rawDir = *rawDir;
                    }
                    if (auto previewDir = GetOptionalString(layout, L"preview_dir"))
                    {
                        manifest.layout.previewDir = *previewDir;
                    }
                    if (auto rebuiltDir = GetOptionalString(layout, L"rebuilt_dir"))
                    {
                        manifest.layout.rebuiltDir = *rebuiltDir;
                    }
                }

                JsonArray ^ entries = AsArray(GetRequiredValue(root, L"entries"), "manifest entries");
                manifest.entries.reserve(entries->Count);
                for each (Object ^ entryObject in entries)
                {
                    JsonDict ^ entryDict = AsDict(entryObject, "manifest entry");
                    ManifestEntry entry;
                    entry.index = GetRequiredInt(entryDict, L"index");
                    entry.archiveName = GetRequiredString(entryDict, L"archive_name");
                    if (auto archiveNameHex = GetOptionalString(entryDict, L"archive_name_hex"))
                    {
                        entry.archiveNameHex = *archiveNameHex;
                    }
                    entry.normalizedName = GetRequiredString(entryDict, L"normalized_name");
                    entry.nameOffset = static_cast<uint32_t>(GetRequiredInt(entryDict, L"name_offset"));
                    entry.dataOffset = static_cast<uint32_t>(GetRequiredInt(entryDict, L"data_offset"));
                    entry.dataSize = static_cast<uint32_t>(GetRequiredInt(entryDict, L"data_size"));
                    entry.assetType = GetRequiredString(entryDict, L"asset_type");
                    if (auto magicHex = GetOptionalString(entryDict, L"magic_hex"))
                    {
                        entry.magicHex = *magicHex;
                    }
                    if (auto sha256 = GetOptionalString(entryDict, L"sha256"))
                    {
                        entry.sha256 = *sha256;
                    }
                    entry.rawPath = GetRequiredString(entryDict, L"raw_path");
                    entry.previewPngPath = GetOptionalString(entryDict, L"preview_png_path");
                    entry.previewPngSha256 = GetOptionalString(entryDict, L"preview_png_sha256");
                    if (auto originalFormat = GetOptionalString(entryDict, L"original_format"))
                    {
                        entry.originalFormat = *originalFormat;
                    }
                    manifest.entries.push_back(std::move(entry));
                }
            }
            catch (const std::exception &)
            {
                throw;
            }
            catch (Exception ^ ex)
            {
                throw std::runtime_error("Failed to parse manifest JSON: " + ManagedStringToUtf8(ex->Message));
            }

            return manifest;
        }

        std::string RelativePathAfterPrefix(const std::string &pathText, const std::string &prefixText)
        {
            std::string normalizedPath = pathText;
            std::replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');
            std::string normalizedPrefix = prefixText;
            std::replace(normalizedPrefix.begin(), normalizedPrefix.end(), '\\', '/');
            if (!normalizedPrefix.empty() && normalizedPrefix.back() != '/')
            {
                normalizedPrefix.push_back('/');
            }
            if (normalizedPath.rfind(normalizedPrefix, 0) == 0)
            {
                return normalizedPath.substr(normalizedPrefix.size());
            }
            return Utf8Path(normalizedPath).filename().generic_string();
        }

        std::string DefaultRebuiltRelPath(const ManifestEntry &entry, const AssetManifest &manifest)
        {
            const std::string relativePath = RelativePathAfterPrefix(entry.rawPath, manifest.layout.rawDir);
            return (Utf8Path(manifest.layout.rebuiltDir) / Utf8Path(relativePath)).generic_string();
        }

        ByteBuffer BuildStringTableBytes(const AssetManifest &manifest)
        {
            if (!manifest.stringTableHex.empty())
            {
                ByteBuffer stringTable = HexToBytes(manifest.stringTableHex);
                if (manifest.stringTableSize != 0 && stringTable.size() != manifest.stringTableSize)
                {
                    throw std::runtime_error("Manifest string table size mismatch.");
                }
                return stringTable;
            }

            if (manifest.stringTableSize == 0)
            {
                throw std::runtime_error("Manifest is missing string_table_size.");
            }

            ByteBuffer stringTable(manifest.stringTableSize, 0);
            for (const auto &entry : manifest.entries)
            {
                ByteBuffer rawName = entry.archiveNameHex.empty()
                                         ? EncodeWideToCodePage(Utf8ToWide(entry.archiveName), 932)
                                         : HexToBytes(entry.archiveNameHex);
                const size_t start = entry.nameOffset;
                const size_t end = start + rawName.size() + 1;
                if (end > stringTable.size())
                {
                    throw std::runtime_error("Entry name overflows manifest string table: " + entry.archiveName);
                }
                std::copy(rawName.begin(), rawName.end(), stringTable.begin() + static_cast<std::ptrdiff_t>(start));
                stringTable[start + rawName.size()] = 0;
            }

            return stringTable;
        }

        std::string AssetFamily(const std::string &assetType)
        {
            const size_t separator = assetType.find('/');
            return separator == std::string::npos ? assetType : assetType.substr(0, separator);
        }

        SelectedEntry ResolveEntrySource(
            const ManifestEntry &entry,
            const AssetManifest &manifest,
            bool autoRebuildImages,
            bool persistRebuiltImages,
            const fs::file_time_type &manifestWriteTime)
        {
            const fs::path baseDir = manifest.manifestPath.parent_path();
            const fs::path rawPath = baseDir / Utf8Path(entry.rawPath);
            const std::string rebuiltRelPath = DefaultRebuiltRelPath(entry, manifest);
            const fs::path rebuiltPath = baseDir / Utf8Path(rebuiltRelPath);
            const fs::path previewPath = entry.previewPngPath.has_value() ? (baseDir / Utf8Path(*entry.previewPngPath)) : fs::path();

            const std::string originalSha256 = entry.sha256;
            const std::string rawSha256 = fs::exists(rawPath) ? Sha256File(rawPath) : std::string();
            std::string rebuiltSha256 = fs::exists(rebuiltPath) ? Sha256File(rebuiltPath) : std::string();
            std::string previewSha256 = (entry.previewPngPath.has_value() && fs::exists(previewPath)) ? Sha256File(previewPath) : std::string();

            bool previewModified = false;
            if (entry.previewPngPath.has_value() && fs::exists(previewPath))
            {
                if (entry.previewPngSha256.has_value())
                {
                    previewModified = previewSha256 != *entry.previewPngSha256;
                }
                else
                {
                    previewModified = fs::last_write_time(previewPath) > manifestWriteTime;
                }
            }

            if (entry.assetType == "image/hzc1" && autoRebuildImages && entry.previewPngPath.has_value() && fs::exists(previewPath) && previewModified)
            {
                if (!fs::exists(rawPath))
                {
                    throw std::runtime_error("Raw HZC1 source is missing for auto rebuild: " + PathToUtf8(rawPath));
                }
                const auto rebuilt = RebuildHzc1FromPng(ReadBinaryFile(rawPath), previewPath);
                rebuiltSha256 = Sha256Hex(rebuilt.first);
                if (persistRebuiltImages)
                {
                    WriteBinaryFile(rebuiltPath, rebuilt.first);
                }
                SelectedEntry selected;
                selected.entry = entry;
                selected.sourcePath = persistRebuiltImages ? rebuiltPath : rawPath;
                selected.selectedRelPath = rebuiltRelPath;
                selected.selectedSha256 = rebuiltSha256;
                selected.sourceKind = "preview_png";
                selected.modified = rebuiltSha256 != originalSha256;
                selected.changeReason = "preview_png_modified";
                selected.rawSha256 = rawSha256;
                selected.rebuiltSha256 = rebuiltSha256;
                selected.previewSha256 = previewSha256;
                selected.previewModified = previewModified;
                selected.assetFamily = AssetFamily(entry.assetType);
                return selected;
            }

            if (fs::exists(rebuiltPath))
            {
                SelectedEntry selected;
                selected.entry = entry;
                selected.sourcePath = rebuiltPath;
                selected.selectedRelPath = rebuiltRelPath;
                selected.selectedSha256 = rebuiltSha256;
                selected.sourceKind = "rebuilt";
                selected.modified = rebuiltSha256 != originalSha256;
                selected.changeReason = selected.modified ? "rebuilt_override" : "rebuilt_matches_original";
                selected.rawSha256 = rawSha256;
                selected.rebuiltSha256 = rebuiltSha256;
                selected.previewSha256 = previewSha256;
                selected.previewModified = previewModified;
                selected.assetFamily = AssetFamily(entry.assetType);
                return selected;
            }

            if (!fs::exists(rawPath))
            {
                throw std::runtime_error("Raw asset is missing: " + PathToUtf8(rawPath));
            }

            SelectedEntry selected;
            selected.entry = entry;
            selected.sourcePath = rawPath;
            selected.selectedRelPath = entry.rawPath;
            selected.selectedSha256 = rawSha256;
            selected.sourceKind = "raw";
            selected.modified = rawSha256 != originalSha256;
            selected.changeReason = selected.modified ? "raw_modified" : "unchanged_raw";
            selected.rawSha256 = rawSha256;
            selected.rebuiltSha256 = rebuiltSha256;
            selected.previewSha256 = previewSha256;
            selected.previewModified = previewModified;
            selected.assetFamily = AssetFamily(entry.assetType);
            return selected;
        }

        std::pair<std::vector<SelectedEntry>, ByteBuffer> CollectSelectedEntries(
            const AssetManifest &manifest,
            bool autoRebuildImages,
            bool persistRebuiltImages)
        {
            std::vector<ManifestEntry> entries = manifest.entries;
            std::sort(entries.begin(), entries.end(), [](const ManifestEntry &left, const ManifestEntry &right)
                      { return left.index < right.index; });

            const ByteBuffer stringTable = BuildStringTableBytes(manifest);
            uint32_t dataOffset = static_cast<uint32_t>(8 + entries.size() * 12 + stringTable.size());
            const fs::file_time_type manifestWriteTime = fs::last_write_time(manifest.manifestPath);

            std::vector<SelectedEntry> selectedEntries;
            selectedEntries.reserve(entries.size());
            for (const auto &entry : entries)
            {
                SelectedEntry selected = ResolveEntrySource(entry, manifest, autoRebuildImages, persistRebuiltImages, manifestWriteTime);
                selected.payloadSize = static_cast<uint32_t>(fs::file_size(selected.sourcePath));
                selected.dataOffset = dataOffset;
                dataOffset += selected.payloadSize;
                selectedEntries.push_back(std::move(selected));
            }

            return {selectedEntries, stringTable};
        }

        SelectionSummary SummarizeSelectedEntries(const std::vector<SelectedEntry> &selectedEntries)
        {
            SelectionSummary summary;
            summary.totalEntries = static_cast<int>(selectedEntries.size());
            for (const auto &selectedEntry : selectedEntries)
            {
                IncrementCounter(summary.assetTypeCounts, selectedEntry.entry.assetType);
                IncrementCounter(summary.assetFamilyCounts, selectedEntry.assetFamily);
                IncrementCounter(summary.sourceKindCounts, selectedEntry.sourceKind);
                IncrementCounter(summary.changeReasonCounts, selectedEntry.changeReason);

                if (selectedEntry.modified)
                {
                    ++summary.modifiedEntries;
                    IncrementCounter(summary.modifiedAssetTypeCounts, selectedEntry.entry.assetType);
                    IncrementCounter(summary.modifiedAssetFamilyCounts, selectedEntry.assetFamily);
                    IncrementCounter(summary.modifiedSourceKindCounts, selectedEntry.sourceKind);
                    IncrementCounter(summary.modifiedChangeReasonCounts, selectedEntry.changeReason);
                }
                else
                {
                    ++summary.unchangedEntries;
                }
            }
            summary.hasChanges = summary.modifiedEntries > 0;
            return summary;
        }

        void WriteFileToHandle(const fs::path &inputPath, std::ofstream &output, size_t chunkSize = 1024 * 1024)
        {
            std::ifstream input(inputPath, std::ios::binary);
            if (!input)
            {
                throw std::runtime_error("Failed to read source asset: " + PathToUtf8(inputPath));
            }
            ByteBuffer buffer(chunkSize);
            while (input)
            {
                input.read(reinterpret_cast<char *>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
                const auto count = input.gcount();
                if (count > 0)
                {
                    output.write(reinterpret_cast<const char *>(buffer.data()), count);
                }
            }
        }

        void AppendJsonCounterObject(std::ostringstream &output, const std::map<std::string, int> &counters, int indent)
        {
            output << "{";
            bool first = true;
            for (const auto &[key, value] : counters)
            {
                if (!first)
                {
                    output << ",";
                }
                output << "\n"
                       << std::string(indent, ' ') << "\"" << JsonEscape(key) << "\": " << value;
                first = false;
            }
            if (!counters.empty())
            {
                output << "\n"
                       << std::string(indent - 2, ' ');
            }
            output << "}";
        }

        void AppendSelectionSummary(std::ostringstream &output, const SelectionSummary &summary, int indent)
        {
            output << "{\n"
                   << std::string(indent, ' ') << "\"total_entries\": " << summary.totalEntries << ",\n"
                   << std::string(indent, ' ') << "\"modified_entries\": " << summary.modifiedEntries << ",\n"
                   << std::string(indent, ' ') << "\"unchanged_entries\": " << summary.unchangedEntries << ",\n"
                   << std::string(indent, ' ') << "\"has_changes\": " << (summary.hasChanges ? "true" : "false") << ",\n"
                   << std::string(indent, ' ') << "\"asset_type_counts\": ";
            AppendJsonCounterObject(output, summary.assetTypeCounts, indent + 2);
            output << ",\n"
                   << std::string(indent, ' ') << "\"modified_asset_type_counts\": ";
            AppendJsonCounterObject(output, summary.modifiedAssetTypeCounts, indent + 2);
            output << ",\n"
                   << std::string(indent, ' ') << "\"asset_family_counts\": ";
            AppendJsonCounterObject(output, summary.assetFamilyCounts, indent + 2);
            output << ",\n"
                   << std::string(indent, ' ') << "\"modified_asset_family_counts\": ";
            AppendJsonCounterObject(output, summary.modifiedAssetFamilyCounts, indent + 2);
            output << ",\n"
                   << std::string(indent, ' ') << "\"source_kind_counts\": ";
            AppendJsonCounterObject(output, summary.sourceKindCounts, indent + 2);
            output << ",\n"
                   << std::string(indent, ' ') << "\"modified_source_kind_counts\": ";
            AppendJsonCounterObject(output, summary.modifiedSourceKindCounts, indent + 2);
            output << ",\n"
                   << std::string(indent, ' ') << "\"change_reason_counts\": ";
            AppendJsonCounterObject(output, summary.changeReasonCounts, indent + 2);
            output << ",\n"
                   << std::string(indent, ' ') << "\"modified_change_reason_counts\": ";
            AppendJsonCounterObject(output, summary.modifiedChangeReasonCounts, indent + 2);
            output << "\n"
                   << std::string(indent - 2, ' ') << "}";
        }

        std::vector<uint8_t> ExtractEntryNameBytes(const ByteBuffer &stringTable, uint32_t nameOffset)
        {
            if (nameOffset >= stringTable.size())
            {
                return {};
            }
            size_t end = nameOffset;
            while (end < stringTable.size() && stringTable[end] != 0)
            {
                ++end;
            }
            return ByteBuffer(stringTable.begin() + nameOffset, stringTable.begin() + end);
        }

        std::string DecodeEntryName(const ByteBuffer &rawName, int index)
        {
            if (rawName.empty())
            {
                std::ostringstream fallback;
                fallback << "file_" << std::setfill('0') << std::setw(4) << index;
                return fallback.str();
            }
            const std::string decoded = WideToUtf8(DecodeBytesToWide(rawName, 932, false));
            if (!decoded.empty())
            {
                return decoded;
            }
            std::ostringstream fallback;
            fallback << "file_" << std::setfill('0') << std::setw(4) << index;
            return fallback.str();
        }

        std::string SanitizeEntryName(const std::string &name, int index)
        {
            std::string normalized = name;
            std::replace(normalized.begin(), normalized.end(), '\\', '/');
            while (!normalized.empty() && normalized.front() == '/')
            {
                normalized.erase(normalized.begin());
            }

            std::vector<std::string> parts;
            std::stringstream stream(normalized);
            std::string part;
            while (std::getline(stream, part, '/'))
            {
                if (part.empty() || part == "." || part == "..")
                {
                    continue;
                }
                parts.push_back(part);
            }
            if (parts.empty())
            {
                std::ostringstream fallback;
                fallback << "file_" << std::setfill('0') << std::setw(4) << index;
                return fallback.str();
            }

            std::ostringstream output;
            for (size_t i = 0; i < parts.size(); ++i)
            {
                if (i > 0)
                {
                    output << '/';
                }
                output << parts[i];
            }
            return output.str();
        }

        std::string EnsureSuffix(const std::string &pathValue, const std::string &suffix)
        {
            fs::path path = Utf8Path(pathValue);
            if (ToLowerAscii(path.extension().string()) == ToLowerAscii(suffix))
            {
                return path.generic_string();
            }
            if (path.has_extension())
            {
                path.replace_extension(Utf8ToWide(suffix));
                return path.generic_string();
            }
            return path.generic_string() + suffix;
        }

        struct AssetTypeInfo
        {
            std::string assetType;
            std::string rawStorageName;
        };

        AssetTypeInfo DetectAssetType(const std::string &normalizedName, const ByteBuffer &data)
        {
            if (data.size() >= 4 && std::memcmp(data.data(), "hzc1", 4) == 0)
            {
                return {"image/hzc1", EnsureSuffix(normalizedName, ".hzc1")};
            }
            if (data.size() >= 4 && std::memcmp(data.data(), "OggS", 4) == 0)
            {
                return {"audio/ogg", EnsureSuffix(normalizedName, ".ogg")};
            }
            if (data.size() >= 4 && std::memcmp(data.data(), "RIFF", 4) == 0)
            {
                return {"audio/wav", EnsureSuffix(normalizedName, ".wav")};
            }
            return {"application/octet-stream", normalizedName};
        }

        std::string AssetOriginalFormat(const std::string &assetType)
        {
            const size_t separator = assetType.find('/');
            return separator == std::string::npos ? assetType : assetType.substr(separator + 1);
        }

        std::tuple<std::optional<fs::path>, std::string, std::optional<fs::path>> AutoDetectTextInputs(const fs::path &workspaceRoot)
        {
            const fs::path linesPath = workspaceRoot / DefaultTextExtractDir() / L"lines.jsonl";
            const fs::path legacyPath = workspaceRoot / DefaultTextExtractDir() / L"output.txt";
            const std::vector<fs::path> hcbCandidates = {
                workspaceRoot / L"AstralAirFinale.hcb",
                workspaceRoot / L"output.hcb",
            };

            std::optional<fs::path> translationPath;
            std::string translationFormat;
            if (fs::exists(linesPath))
            {
                translationPath = linesPath;
                translationFormat = "jsonl";
            }
            else if (fs::exists(legacyPath))
            {
                translationPath = legacyPath;
                translationFormat = "legacy";
            }

            std::optional<fs::path> hcbPath;
            for (const auto &candidate : hcbCandidates)
            {
                if (fs::exists(candidate))
                {
                    hcbPath = candidate;
                    break;
                }
            }

            return {translationPath, translationFormat, hcbPath};
        }

    } // namespace

    std::string DefaultExtractCategory(const fs::path &binFile)
    {
        const std::string stem = ToLowerAscii(binFile.stem().string());
        if (stem.rfind("graph", 0) == 0)
        {
            return "images";
        }
        if (stem == "voice" || stem == "bgm" || stem.rfind("se", 0) == 0)
        {
            return "audio";
        }
        return "other";
    }

    fs::path DefaultExtractParent(const fs::path &binFile)
    {
        (void)binFile;
        return fs::path(L"unpack");
    }

    fs::path DefaultTextExtractDir()
    {
        return fs::path(L"unpack") / L"text";
    }

    void ExtractBin(const fs::path &inputPath, const fs::path &outputRoot, const ProgressCallback &progress)
    {
        const fs::path rawRoot = outputRoot / L"raw";
        const fs::path previewRoot = outputRoot / L"preview";
        fs::create_directories(rawRoot);
        fs::create_directories(previewRoot);
        ReportProgress(progress, 3, L"创建输出目录");

        std::cout << "--- Extracting " << PathFilenameToUtf8(inputPath) << " to " << PathToUtf8(outputRoot) << "/ ---\n";

        const ByteBuffer data = ReadBinaryFile(inputPath);
        if (data.size() < 8)
        {
            throw std::runtime_error("Archive is too small: " + PathToUtf8(inputPath));
        }
        ReportProgress(progress, 8, L"读取归档数据");

        const uint32_t entryCount = ReadU32LE(data, 0);
        const uint32_t stringTableSize = ReadU32LE(data, 4);
        size_t position = 8;
        std::vector<std::tuple<uint32_t, uint32_t, uint32_t>> entries;
        entries.reserve(entryCount);
        for (uint32_t index = 0; index < entryCount; ++index)
        {
            if (position + 12 > data.size())
            {
                throw std::runtime_error("Archive entry table is truncated.");
            }
            entries.emplace_back(ReadU32LE(data, position), ReadU32LE(data, position + 4), ReadU32LE(data, position + 8));
            position += 12;
        }
        if (position + stringTableSize > data.size())
        {
            throw std::runtime_error("Archive string table is truncated.");
        }
        ByteBuffer stringTable(data.begin() + position, data.begin() + position + stringTableSize);
        ReportProgress(progress, 12, L"解析归档目录");

        std::ostringstream manifest;
        manifest << "{\n"
                 << "  \"schema\": \"" << kAssetManifestSchema << "\",\n"
                 << "  \"generated_at\": \"" << GetIsoUtcNow() << "\",\n"
                 << "  \"source_container\": \"" << JsonEscape(PathFilenameToUtf8(inputPath)) << "\",\n"
                 << "  \"source_path\": \"" << JsonEscape(PathToUtf8(fs::absolute(inputPath))) << "\",\n"
                 << "  \"output_root\": \"" << JsonEscape(PathToUtf8(fs::absolute(outputRoot))) << "\",\n"
                 << "  \"layout\": {\n"
                 << "    \"raw_dir\": \"raw\",\n"
                 << "    \"preview_dir\": \"preview\",\n"
                 << "    \"rebuilt_dir\": \"rebuilt\"\n"
                 << "  },\n"
                 << "  \"container_sha256\": \"" << Sha256Hex(data) << "\",\n"
                 << "  \"entry_count\": " << entryCount << ",\n"
                 << "  \"string_table_size\": " << stringTableSize << ",\n"
                 << "  \"container_size\": " << data.size() << ",\n"
                 << "  \"string_table_sha256\": \"" << Sha256Hex(stringTable) << "\",\n"
                 << "  \"string_table_hex\": \"" << BytesToHex(stringTable) << "\",\n"
                 << "  \"entries\": [\n";

        for (uint32_t index = 0; index < entryCount; ++index)
        {
            const auto [nameOffset, dataOffset, dataSize] = entries[index];
            if (static_cast<uint64_t>(dataOffset) + static_cast<uint64_t>(dataSize) > data.size())
            {
                throw std::runtime_error("Archive payload exceeds file size.");
            }
            ByteBuffer rawNameBytes = ExtractEntryNameBytes(stringTable, nameOffset);
            std::string archiveName = DecodeEntryName(rawNameBytes, static_cast<int>(index));
            std::string normalizedName = SanitizeEntryName(archiveName, static_cast<int>(index));
            ByteBuffer payload(data.begin() + dataOffset, data.begin() + dataOffset + dataSize);
            const AssetTypeInfo assetInfo = DetectAssetType(normalizedName, payload);

            const fs::path rawOutputPath = rawRoot / Utf8Path(assetInfo.rawStorageName);
            WriteBinaryFile(rawOutputPath, payload);

            Hzc1Metadata imageMetadata = ParseHzc1Metadata(payload);
            std::optional<std::string> previewRelPath;
            std::optional<std::string> previewSha256;
            std::optional<std::string> extractionError;
            if (assetInfo.assetType == "image/hzc1")
            {
                try
                {
                    fs::path previewStoragePath = Utf8Path(assetInfo.rawStorageName);
                    previewStoragePath.replace_extension(L".png");
                    const fs::path previewOutputPath = previewRoot / previewStoragePath;
                    ExportPngPreview(payload, previewOutputPath);
                    previewRelPath = PathToUtf8(fs::relative(previewOutputPath, outputRoot));
                    previewSha256 = Sha256File(previewOutputPath);
                }
                catch (const std::exception &ex)
                {
                    extractionError = ex.what();
                }
            }

            if (index > 0)
            {
                manifest << ",\n";
            }
            manifest << "    {\n"
                     << "      \"index\": " << index << ",\n"
                     << "      \"archive_name\": \"" << JsonEscape(archiveName) << "\",\n"
                     << "      \"archive_name_hex\": \"" << BytesToHex(rawNameBytes) << "\",\n"
                     << "      \"normalized_name\": \"" << JsonEscape(normalizedName) << "\",\n"
                     << "      \"name_offset\": " << nameOffset << ",\n"
                     << "      \"data_offset\": " << dataOffset << ",\n"
                     << "      \"data_size\": " << dataSize << ",\n"
                     << "      \"asset_type\": \"" << assetInfo.assetType << "\",\n"
                     << "      \"magic_hex\": \"" << BytesToHex(payload.data(), std::min<size_t>(payload.size(), 4)) << "\",\n"
                     << "      \"sha256\": \"" << Sha256Hex(payload) << "\",\n"
                     << "      \"raw_path\": \"" << JsonEscape(PathToUtf8(fs::relative(rawOutputPath, outputRoot))) << "\",\n"
                     << "      \"preview_png_path\": " << JsonQuotedOrNull(previewRelPath) << ",\n"
                     << "      \"preview_png_sha256\": " << JsonQuotedOrNull(previewSha256) << ",\n"
                     << "      \"original_format\": \"" << JsonEscape(AssetOriginalFormat(assetInfo.assetType)) << "\",\n";

            if (imageMetadata.valid)
            {
                manifest << "      \"image_metadata\": {\n"
                         << "        \"width\": " << imageMetadata.width << ",\n"
                         << "        \"height\": " << imageMetadata.height << ",\n"
                         << "        \"bits_per_pixel\": " << imageMetadata.BitsPerPixel() << ",\n"
                         << "        \"channel_count\": " << imageMetadata.ChannelCount() << ",\n"
                         << "        \"offset_x\": " << imageMetadata.offsetX << ",\n"
                         << "        \"offset_y\": " << imageMetadata.offsetY << ",\n"
                         << "        \"header_size\": " << imageMetadata.headerSize << ",\n"
                         << "        \"uncompressed_size\": " << imageMetadata.uncompressedSize << ",\n"
                         << "        \"unknown1\": " << imageMetadata.unknown1 << ",\n"
                         << "        \"unknown2\": " << imageMetadata.unknown2 << ",\n"
                         << "        \"unknown3\": " << imageMetadata.unknown3 << "\n"
                         << "      },\n";
            }
            else
            {
                manifest << "      \"image_metadata\": null,\n";
            }

            manifest << "      \"error\": " << JsonQuotedOrNull(extractionError) << "\n"
                     << "    }";

            const int progressPercent = entryCount == 0
                                            ? 94
                                            : 12 + static_cast<int>(((index + 1) * 82) / entryCount);
            ReportProgress(
                progress,
                progressPercent,
                L"提取 [" + std::to_wstring(index + 1) + L"/" + std::to_wstring(entryCount) + L"] " + Utf8ToWide(assetInfo.rawStorageName));

            std::cout << "[" << (index + 1) << "/" << entryCount << "] Saved RAW: "
                      << PathToUtf8(fs::relative(rawOutputPath, outputRoot)) << "\n";
            if (previewRelPath.has_value())
            {
                std::cout << "[" << (index + 1) << "/" << entryCount << "] Saved PNG: " << *previewRelPath << "\n";
            }
        }

        manifest << "\n  ]\n"
                 << "}\n";

        const fs::path manifestPath = outputRoot / L"manifest.json";
        ReportProgress(progress, 98, L"写出 manifest.json");
        WriteTextFileUtf8(manifestPath, manifest.str());
        std::cout << "Manifest written: " << PathToUtf8(manifestPath) << "\n";
        ReportProgress(progress, 100, L"资源解包完成");
    }

    fs::path RebuildImages(const fs::path &manifestOrDir, const std::optional<fs::path> &outputDir)
    {
        const AssetManifest manifest = LoadManifest(manifestOrDir);
        const fs::path baseDir = manifest.manifestPath.parent_path();
        const fs::path outputRoot = outputDir.has_value() ? *outputDir : (baseDir / Utf8ToWide(manifest.layout.rebuiltDir));
        fs::create_directories(outputRoot);

        std::ostringstream report;
        report << "{\n"
               << "  \"schema\": \"" << kHzc1RebuildReportSchema << "\",\n"
               << "  \"generated_at\": \"" << GetIsoUtcNow() << "\",\n"
               << "  \"manifest_path\": \"" << JsonEscape(PathToUtf8(fs::absolute(manifest.manifestPath))) << "\",\n"
               << "  \"output_root\": \"" << JsonEscape(PathToUtf8(fs::absolute(outputRoot))) << "\",\n"
               << "  \"rebuilt_entries\": [\n";

        bool firstRebuilt = true;
        std::vector<std::string> skippedEntries;
        for (const auto &entry : manifest.entries)
        {
            if (entry.assetType != "image/hzc1")
            {
                continue;
            }
            if (!entry.previewPngPath.has_value())
            {
                skippedEntries.push_back("{\"index\":" + std::to_string(entry.index) + ",\"reason\":\"missing preview path\"}");
                continue;
            }

            const fs::path previewPath = baseDir / Utf8Path(*entry.previewPngPath);
            const fs::path rawPath = baseDir / Utf8Path(entry.rawPath);
            if (!fs::exists(previewPath))
            {
                skippedEntries.push_back("{\"index\":" + std::to_string(entry.index) + ",\"reason\":\"preview missing\"}");
                continue;
            }
            if (!fs::exists(rawPath))
            {
                skippedEntries.push_back("{\"index\":" + std::to_string(entry.index) + ",\"reason\":\"raw source missing\"}");
                continue;
            }

            try
            {
                const std::string rebuiltRelative = RelativePathAfterPrefix(entry.rawPath, manifest.layout.rawDir);
                const fs::path rebuiltOutputPath = outputRoot / Utf8Path(rebuiltRelative);
                const auto rebuilt = RebuildHzc1FromPng(ReadBinaryFile(rawPath), previewPath);
                WriteBinaryFile(rebuiltOutputPath, rebuilt.first);

                if (!firstRebuilt)
                {
                    report << ",\n";
                }
                firstRebuilt = false;
                report << "    {\n"
                       << "      \"index\": " << entry.index << ",\n"
                       << "      \"archive_name\": \"" << JsonEscape(entry.archiveName) << "\",\n"
                       << "      \"source_png_path\": \"" << JsonEscape(*entry.previewPngPath) << "\",\n"
                       << "      \"original_raw_path\": \"" << JsonEscape(entry.rawPath) << "\",\n"
                       << "      \"rebuilt_raw_path\": \"" << JsonEscape(PathToUtf8(fs::relative(rebuiltOutputPath, outputRoot))) << "\",\n"
                       << "      \"sha256\": \"" << Sha256Hex(rebuilt.first) << "\",\n"
                       << "      \"image_metadata\": {\n"
                       << "        \"width\": " << rebuilt.second.width << ",\n"
                       << "        \"height\": " << rebuilt.second.height << ",\n"
                       << "        \"bits_per_pixel\": " << rebuilt.second.BitsPerPixel() << ",\n"
                       << "        \"channel_count\": " << rebuilt.second.ChannelCount() << ",\n"
                       << "        \"offset_x\": " << rebuilt.second.offsetX << ",\n"
                       << "        \"offset_y\": " << rebuilt.second.offsetY << ",\n"
                       << "        \"header_size\": " << rebuilt.second.headerSize << ",\n"
                       << "        \"uncompressed_size\": " << rebuilt.second.uncompressedSize << ",\n"
                       << "        \"unknown1\": " << rebuilt.second.unknown1 << ",\n"
                       << "        \"unknown2\": " << rebuilt.second.unknown2 << ",\n"
                       << "        \"unknown3\": " << rebuilt.second.unknown3 << "\n"
                       << "      }\n"
                       << "    }";
                std::cout << "Rebuilt HZC1: " << PathToUtf8(rebuiltOutputPath) << "\n";
            }
            catch (const std::exception &ex)
            {
                skippedEntries.push_back("{\"index\":" + std::to_string(entry.index) + ",\"reason\":\"" + JsonEscape(ex.what()) + "\"}");
            }
        }

        report << "\n  ],\n"
               << "  \"skipped_entries\": [";
        for (size_t index = 0; index < skippedEntries.size(); ++index)
        {
            if (index > 0)
            {
                report << ",";
            }
            report << "\n    " << skippedEntries[index];
        }
        if (!skippedEntries.empty())
        {
            report << "\n  ";
        }
        report << "]\n"
               << "}\n";

        const fs::path reportPath = outputRoot / L"rebuild_report.json";
        WriteTextFileUtf8(reportPath, report.str());
        return reportPath;
    }

    BinRepackResult RepackBin(const fs::path &manifestOrDir, const std::optional<fs::path> &outputBin, const BinRepackOptions &options, const ProgressCallback &progress)
    {
        ReportProgress(progress, 5, L"读取 manifest.json");
        const AssetManifest manifest = LoadManifest(manifestOrDir);
        ReportProgress(progress, 18, L"分析资源改动");
        auto [selectedEntries, stringTable] = CollectSelectedEntries(manifest, options.autoRebuildImages, options.persistRebuiltImages);
        const SelectionSummary summary = SummarizeSelectedEntries(selectedEntries);

        fs::path outputPath = outputBin.has_value()
                                  ? *outputBin
                                  : (manifest.manifestPath.parent_path() / (Utf8Path(manifest.sourceContainer).stem().wstring() + std::wstring(L".repacked") + Utf8Path(manifest.sourceContainer).extension().wstring()));
        EnsureParentDir(outputPath);

        ReportProgress(progress, 36, L"写入 BIN 头和索引");
        std::ofstream output(outputPath, std::ios::binary);
        if (!output)
        {
            throw std::runtime_error("Failed to write BIN output: " + PathToUtf8(outputPath));
        }

        output.write(reinterpret_cast<const char *>(&manifest.entryCount), sizeof(uint32_t));
        const uint32_t stringTableSize = static_cast<uint32_t>(stringTable.size());
        output.write(reinterpret_cast<const char *>(&stringTableSize), sizeof(uint32_t));
        for (const auto &selectedEntry : selectedEntries)
        {
            output.write(reinterpret_cast<const char *>(&selectedEntry.entry.nameOffset), sizeof(uint32_t));
            output.write(reinterpret_cast<const char *>(&selectedEntry.dataOffset), sizeof(uint32_t));
            output.write(reinterpret_cast<const char *>(&selectedEntry.payloadSize), sizeof(uint32_t));
        }
        if (!stringTable.empty())
        {
            output.write(reinterpret_cast<const char *>(stringTable.data()), static_cast<std::streamsize>(stringTable.size()));
        }
        const size_t entryCount = selectedEntries.size();
        for (size_t index = 0; index < entryCount; ++index)
        {
            const auto &selectedEntry = selectedEntries[index];
            WriteFileToHandle(selectedEntry.sourcePath, output);
            const int writePercent = entryCount == 0
                                         ? 86
                                         : 42 + static_cast<int>(((index + 1) * 44) / entryCount);
            ReportProgress(
                progress,
                writePercent,
                L"写入资源 [" + std::to_wstring(index + 1) + L"/" + std::to_wstring(entryCount) + L"] " + Utf8ToWide(selectedEntry.entry.archiveName));
        }
        output.close();

        ReportProgress(progress, 90, L"计算输出校验");
        const std::string outputSha256 = Sha256File(outputPath);
        const bool originalContainerExists = manifest.sourcePath.has_value() && fs::exists(*manifest.sourcePath);
        const bool matchesOriginal = originalContainerExists && outputSha256 == manifest.containerSha256;

        std::ostringstream report;
        report << "{\n"
               << "  \"schema\": \"" << kBinRepackReportSchema << "\",\n"
               << "  \"generated_at\": \"" << GetIsoUtcNow() << "\",\n"
               << "  \"manifest_path\": \"" << JsonEscape(PathToUtf8(fs::absolute(manifest.manifestPath))) << "\",\n"
               << "  \"output_path\": \"" << JsonEscape(PathToUtf8(fs::absolute(outputPath))) << "\",\n"
               << "  \"output_sha256\": \"" << outputSha256 << "\",\n"
               << "  \"entry_count\": " << selectedEntries.size() << ",\n"
               << "  \"string_table_sha256\": \"" << Sha256Hex(stringTable) << "\",\n"
               << "  \"auto_rebuild_images\": " << (options.autoRebuildImages ? "true" : "false") << ",\n"
               << "  \"persist_rebuilt_images\": " << (options.persistRebuiltImages ? "true" : "false") << ",\n"
               << "  \"original_container_exists\": " << (originalContainerExists ? "true" : "false") << ",\n"
               << "  \"matches_original_container\": " << (matchesOriginal ? "true" : "false") << ",\n"
               << "  \"summary\": ";
        AppendSelectionSummary(report, summary, 4);
        report << ",\n  \"entries\": [\n";
        for (size_t index = 0; index < selectedEntries.size(); ++index)
        {
            const auto &selectedEntry = selectedEntries[index];
            if (index > 0)
            {
                report << ",\n";
            }
            report << "    {\n"
                   << "      \"index\": " << selectedEntry.entry.index << ",\n"
                   << "      \"archive_name\": \"" << JsonEscape(selectedEntry.entry.archiveName) << "\",\n"
                   << "      \"asset_type\": \"" << JsonEscape(selectedEntry.entry.assetType) << "\",\n"
                   << "      \"asset_family\": \"" << JsonEscape(selectedEntry.assetFamily) << "\",\n"
                   << "      \"selected_source\": \"" << JsonEscape(selectedEntry.selectedRelPath) << "\",\n"
                   << "      \"source_kind\": \"" << JsonEscape(selectedEntry.sourceKind) << "\",\n"
                   << "      \"modified\": " << (selectedEntry.modified ? "true" : "false") << ",\n"
                   << "      \"change_reason\": \"" << JsonEscape(selectedEntry.changeReason) << "\",\n"
                   << "      \"payload_size\": " << selectedEntry.payloadSize << ",\n"
                   << "      \"data_offset\": " << selectedEntry.dataOffset << ",\n"
                   << "      \"selected_sha256\": \"" << JsonEscape(selectedEntry.selectedSha256) << "\",\n"
                   << "      \"original_sha256\": \"" << JsonEscape(selectedEntry.entry.sha256) << "\",\n"
                   << "      \"raw_sha256\": " << JsonQuotedOrNull(selectedEntry.rawSha256.empty() ? std::nullopt : std::optional<std::string>(selectedEntry.rawSha256)) << ",\n"
                   << "      \"rebuilt_sha256\": " << JsonQuotedOrNull(selectedEntry.rebuiltSha256.empty() ? std::nullopt : std::optional<std::string>(selectedEntry.rebuiltSha256)) << ",\n"
                   << "      \"preview_sha256\": " << JsonQuotedOrNull(selectedEntry.previewSha256.empty() ? std::nullopt : std::optional<std::string>(selectedEntry.previewSha256)) << ",\n"
                   << "      \"preview_modified\": " << (selectedEntry.previewModified ? "true" : "false") << "\n"
                   << "    }";
        }
        report << "\n  ]\n"
               << "}\n";

        const fs::path reportPath(outputPath.wstring() + std::wstring(L".report.json"));
        ReportProgress(progress, 97, L"写出封包报告");
        WriteTextFileUtf8(reportPath, report.str());
        ReportProgress(progress, 100, L"资源封包完成");
        return {outputPath, reportPath};
    }

    std::vector<fs::path> DiscoverManifestPaths(const std::vector<fs::path> &inputs)
    {
        std::vector<fs::path> candidates;
        std::set<fs::path> seen;
        const std::vector<fs::path> searchTargets = inputs.empty() ? std::vector<fs::path>{fs::path(L".")} : inputs;

        for (const auto &target : searchTargets)
        {
            std::vector<fs::path> current;
            if (fs::is_regular_file(target) && target.filename() == L"manifest.json")
            {
                current.push_back(target);
            }
            else if (fs::is_directory(target) && fs::exists(target / L"manifest.json"))
            {
                current.push_back(target / L"manifest.json");
            }
            else if (fs::is_directory(target))
            {
                for (const auto &entry : fs::recursive_directory_iterator(target))
                {
                    if (entry.is_regular_file() && entry.path().filename() == L"manifest.json")
                    {
                        if (entry.path().wstring().find(L".venv") != std::wstring::npos)
                        {
                            continue;
                        }
                        current.push_back(entry.path());
                    }
                }
            }

            for (const auto &candidate : current)
            {
                const fs::path resolved = fs::absolute(candidate);
                if (seen.count(resolved) > 0)
                {
                    continue;
                }
                try
                {
                    const AssetManifest manifest = LoadManifest(candidate);
                    if (manifest.schema != kAssetManifestSchema)
                    {
                        continue;
                    }
                    seen.insert(resolved);
                    candidates.push_back(candidate);
                }
                catch (const std::exception &)
                {
                    continue;
                }
            }
        }

        std::sort(candidates.begin(), candidates.end());
        return candidates;
    }

    fs::path BuildPatch(const std::vector<fs::path> &inputs, const BuildPatchOptions &options, const ProgressCallback &progress)
    {
        const fs::path workspaceRoot = fs::current_path();
        const fs::path outputRoot = options.outputDir;
        fs::create_directories(outputRoot);

        ReportProgress(progress, 4, L"扫描可用的 manifest");
        const std::vector<fs::path> manifestPaths = DiscoverManifestPaths(inputs);
        std::vector<std::string> containerReports;
        int totalModifiedEntries = 0;
        int totalEntries = 0;
        int changedContainerCount = 0;
        std::map<std::string, int> assetTypeTotals;
        std::map<std::string, int> assetTypeModifiedTotals;
        std::map<std::string, int> sourceKindModifiedTotals;

        const int manifestCount = static_cast<int>(manifestPaths.size());
        for (int manifestIndex = 0; manifestIndex < manifestCount; ++manifestIndex)
        {
            const auto &manifestPath = manifestPaths[static_cast<size_t>(manifestIndex)];
            const AssetManifest manifest = LoadManifest(manifestPath);
            const int segmentStart = manifestCount == 0 ? 12 : 12 + (manifestIndex * 58) / manifestCount;
            const int segmentEnd = manifestCount == 0 ? 70 : 12 + ((manifestIndex + 1) * 58) / manifestCount;
            ReportProgress(progress, segmentStart, Utf8ToWide(manifest.sourceContainer) + L" | 检查资源变更");
            auto [selectedEntries, _stringTable] = CollectSelectedEntries(manifest, options.autoRebuildImages, true);
            const SelectionSummary summary = SummarizeSelectedEntries(selectedEntries);
            totalModifiedEntries += summary.modifiedEntries;
            totalEntries += summary.totalEntries;
            for (const auto &[key, value] : summary.assetTypeCounts)
            {
                IncrementCounter(assetTypeTotals, key, value);
            }
            for (const auto &[key, value] : summary.modifiedAssetTypeCounts)
            {
                IncrementCounter(assetTypeModifiedTotals, key, value);
            }
            for (const auto &[key, value] : summary.modifiedSourceKindCounts)
            {
                IncrementCounter(sourceKindModifiedTotals, key, value);
            }

            std::optional<BinRepackResult> repackResult;
            if (summary.hasChanges || options.includeUnchanged)
            {
                const auto scaledProgress = [&](int nestedPercent, const std::wstring &message)
                {
                    const int mappedPercent = segmentStart + ((segmentEnd - segmentStart) * nestedPercent) / 100;
                    ReportProgress(progress, mappedPercent, Utf8ToWide(manifest.sourceContainer) + L" | " + message);
                };
                repackResult = RepackBin(manifestPath, outputRoot / Utf8Path(manifest.sourceContainer), {options.autoRebuildImages, true}, scaledProgress);
                if (summary.hasChanges)
                {
                    ++changedContainerCount;
                }
            }
            else
            {
                ReportProgress(progress, segmentEnd, Utf8ToWide(manifest.sourceContainer) + L" | 未检测到需要封包的改动");
            }

            std::ostringstream containerJson;
            containerJson << "{\n"
                          << "      \"manifest_path\": \"" << JsonEscape(PathToUtf8(fs::absolute(manifestPath))) << "\",\n"
                          << "      \"source_container\": \"" << JsonEscape(manifest.sourceContainer) << "\",\n"
                          << "      \"has_changes\": " << (summary.hasChanges ? "true" : "false") << ",\n"
                          << "      \"summary\": ";
            AppendSelectionSummary(containerJson, summary, 8);
            containerJson << ",\n"
                          << "      \"output_path\": " << JsonQuotedOrNull(repackResult.has_value() ? std::optional<std::string>(PathToUtf8(fs::absolute(repackResult->outputPath))) : std::nullopt) << ",\n"
                          << "      \"report_path\": " << JsonQuotedOrNull(repackResult.has_value() ? std::optional<std::string>(PathToUtf8(fs::absolute(repackResult->reportPath))) : std::nullopt) << "\n"
                          << "    }";
            containerReports.push_back(containerJson.str());
        }

        ReportProgress(progress, 76, L"检测文本回写输入");
        auto [detectedTranslationPath, detectedTranslationFormat, detectedHcbPath] = AutoDetectTextInputs(workspaceRoot);
        const std::optional<fs::path> translationPath = options.translationFile.has_value() ? options.translationFile : detectedTranslationPath;
        const std::string translationFormat = !options.translationFormat.empty() ? options.translationFormat : detectedTranslationFormat;
        const std::optional<fs::path> hcbPath = options.hcbFile.has_value() ? options.hcbFile : detectedHcbPath;

        bool attemptedTextPatch = false;
        bool builtTextPatch = false;
        int replacedLineCount = 0;
        std::optional<fs::path> textOutputPath;
        if (translationPath.has_value() && fs::exists(*translationPath) && hcbPath.has_value() && fs::exists(*hcbPath))
        {
            attemptedTextPatch = true;
            const auto replacements = LoadReplacements(*translationPath, translationFormat, options.textEncoding);
            replacedLineCount = static_cast<int>(std::count_if(replacements.begin(), replacements.end(), [](const auto &item)
                                                               { return item.has_value(); }));
            if (replacedLineCount > 0 || options.includeUnchanged)
            {
                textOutputPath = options.textOutput.has_value() ? options.textOutput : std::optional<fs::path>(outputRoot / L"output.hcb");
                const auto textProgress = [&](int nestedPercent, const std::wstring &message)
                {
                    const int mappedPercent = 80 + (nestedPercent * 16) / 100;
                    ReportProgress(progress, mappedPercent, L"文本补丁 | " + message);
                };
                RepackText(*hcbPath, *translationPath, *textOutputPath, {translationFormat, options.textEncoding}, textProgress);
                builtTextPatch = true;
            }
            else
            {
                ReportProgress(progress, 92, L"文本补丁 | 没有可写回的翻译行");
            }
        }

        std::ostringstream report;
        report << "{\n"
               << "  \"schema\": \"" << kPatchBuildReportSchema << "\",\n"
               << "  \"generated_at\": \"" << GetIsoUtcNow() << "\",\n"
               << "  \"output_root\": \"" << JsonEscape(PathToUtf8(fs::absolute(outputRoot))) << "\",\n"
               << "  \"auto_rebuild_images\": " << (options.autoRebuildImages ? "true" : "false") << ",\n"
               << "  \"include_unchanged\": " << (options.includeUnchanged ? "true" : "false") << ",\n"
               << "  \"manifest_count\": " << manifestPaths.size() << ",\n"
               << "  \"container_count\": " << containerReports.size() << ",\n"
               << "  \"changed_container_count\": " << changedContainerCount << ",\n"
               << "  \"total_entries\": " << totalEntries << ",\n"
               << "  \"total_modified_entries\": " << totalModifiedEntries << ",\n"
               << "  \"modified_asset_type_counts\": ";
        AppendJsonCounterObject(report, assetTypeModifiedTotals, 4);
        report << ",\n  \"modified_source_kind_counts\": ";
        AppendJsonCounterObject(report, sourceKindModifiedTotals, 4);
        report << ",\n  \"asset_type_counts\": ";
        AppendJsonCounterObject(report, assetTypeTotals, 4);
        report << ",\n  \"containers\": [\n";
        for (size_t index = 0; index < containerReports.size(); ++index)
        {
            if (index > 0)
            {
                report << ",\n";
            }
            report << containerReports[index];
        }
        report << "\n  ],\n"
               << "  \"text_patch\": {\n"
               << "    \"attempted\": " << (attemptedTextPatch ? "true" : "false") << ",\n"
               << "    \"output_path\": " << JsonQuotedOrNull(textOutputPath.has_value() ? std::optional<std::string>(PathToUtf8(fs::absolute(*textOutputPath))) : std::nullopt) << ",\n"
               << "    \"translation_file\": " << JsonQuotedOrNull(translationPath.has_value() && fs::exists(*translationPath) ? std::optional<std::string>(PathToUtf8(fs::absolute(*translationPath))) : std::nullopt) << ",\n"
               << "    \"translation_format\": " << JsonQuotedOrNull(translationFormat.empty() ? std::nullopt : std::optional<std::string>(translationFormat)) << ",\n"
               << "    \"hcb_file\": " << JsonQuotedOrNull(hcbPath.has_value() && fs::exists(*hcbPath) ? std::optional<std::string>(PathToUtf8(fs::absolute(*hcbPath))) : std::nullopt) << ",\n"
               << "    \"replaced_line_count\": " << replacedLineCount << ",\n"
               << "    \"built\": " << (builtTextPatch ? "true" : "false") << "\n"
               << "  }\n"
               << "}\n";

        const fs::path reportPath = outputRoot / L"patch_build_report.json";
        ReportProgress(progress, 98, L"写出补丁报告");
        WriteTextFileUtf8(reportPath, report.str());
        ReportProgress(progress, 100, L"补丁构建完成");
        return reportPath;
    }

} // namespace packcpp
