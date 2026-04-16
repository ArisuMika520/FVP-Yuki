#include "hcb.h"

#include <array>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace packcpp
{
    namespace
    {

        constexpr const char *kLineSchema = "fvp.hcb.line.v1";
        constexpr const char *kVoiceMapSchema = "fvp.voice-map.v1";

        constexpr std::array<int, 0x28> kOpcodeLength = {
            0,
            2,
            4,
            2,
            0,
            0,
            4,
            4,
            0,
            0,
            4,
            2,
            1,
            4,
            0,
            2,
            1,
            2,
            1,
            0,
            0,
            2,
            1,
            2,
            1,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
        };

        LineRecord BuildLineRecord(
            const std::string &sourceFile,
            int lineId,
            int stringSlot,
            int opcodeAddr,
            const std::string &text,
            const std::optional<int> &voiceId,
            const std::string &encoding,
            bool isTitle)
        {
            LineRecord record;
            record.lineId = lineId;
            record.stringSlot = stringSlot;
            record.opcodeAddr = opcodeAddr;
            record.sourceFile = sourceFile;
            record.text = text;
            record.voiceId = voiceId;
            if (voiceId.has_value())
            {
                record.voiceAsset = std::to_string(*voiceId) + ".ogg";
                record.voiceContainerHint = "voice.bin";
            }
            record.textKind = ClassifyTextKind(text, voiceId, isTitle);
            record.encoding = encoding;
            record.isTitle = isTitle;
            return record;
        }

        std::string FormatLegacyOutput(const LineRecord &record)
        {
            if (record.textKind == "dialogue" || record.textKind == "narration_or_ui")
            {
                std::ostringstream output;
                output << "[Voice: ";
                if (record.voiceId.has_value())
                {
                    output << *record.voiceId;
                }
                else
                {
                    output << "None";
                }
                output << "] " << record.text;
                return output.str();
            }
            return record.text;
        }

        std::string InferInputFormat(const fs::path &inputPath, const std::string &explicitFormat)
        {
            if (!explicitFormat.empty())
            {
                return explicitFormat;
            }
            return ToLowerAscii(inputPath.extension().string()) == ".jsonl" ? "jsonl" : "legacy";
        }

        std::vector<std::optional<ByteBuffer>> LoadLegacyReplacements(const fs::path &inputPath, unsigned int codePage)
        {
            std::ifstream input(inputPath, std::ios::binary);
            if (!input)
            {
                throw std::runtime_error("Failed to open translation file: " + PathToUtf8(inputPath));
            }

            std::vector<std::optional<ByteBuffer>> replacements;
            std::string line;
            while (std::getline(input, line))
            {
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                if (!line.empty() && line.front() == '@')
                {
                    replacements.push_back(EncodeWideToCodePage(Utf8ToWide(line.substr(1)), codePage));
                }
                else
                {
                    replacements.push_back(std::nullopt);
                }
            }
            return replacements;
        }

        std::vector<std::optional<ByteBuffer>> LoadJsonlReplacements(const fs::path &inputPath, unsigned int codePage)
        {
            std::ifstream input(inputPath, std::ios::binary);
            if (!input)
            {
                throw std::runtime_error("Failed to open translation file: " + PathToUtf8(inputPath));
            }

            std::map<int, std::optional<ByteBuffer>> indexed;
            int maxLineId = -1;
            std::string line;
            while (std::getline(input, line))
            {
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                if (line.find_first_not_of(" \t") == std::string::npos)
                {
                    continue;
                }

                const int lineId = ExtractJsonIntField(line, "line_id");
                const auto translated = ExtractJsonOptionalStringField(line, "translated_text");
                if (translated.has_value())
                {
                    indexed[lineId] = EncodeWideToCodePage(Utf8ToWide(*translated), codePage);
                }
                else
                {
                    indexed[lineId] = std::nullopt;
                }
                maxLineId = std::max(maxLineId, lineId);
            }

            std::vector<std::optional<ByteBuffer>> replacements(static_cast<size_t>(maxLineId + 1));
            for (auto &[lineId, replacement] : indexed)
            {
                replacements[static_cast<size_t>(lineId)] = std::move(replacement);
            }
            return replacements;
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

    } // namespace

    std::vector<LineRecord> ExtractText(const fs::path &inputPath, int entryOffset, const fs::path &outputDir, DecodeMode decodeMode, const ProgressCallback &progress)
    {
        const ByteBuffer data = ReadBinaryFile(inputPath);
        if (data.size() < 4)
        {
            throw std::runtime_error("HCB file is too small.");
        }

        ReportProgress(progress, 5, L"读取 HCB 数据");

        const uint32_t opcodeLength = ReadU32LE(data, 0);
        if (opcodeLength > data.size())
        {
            throw std::runtime_error("Invalid HCB opcode length.");
        }

        std::vector<LineRecord> records;
        std::optional<int> lastVoice;
        int stringSlot = 0;
        size_t position = 4;
        int lastParsePercent = 12;
        const size_t parseDenominator = opcodeLength > 4 ? opcodeLength - 4 : 1;
        const auto maybeReportParseProgress = [&](size_t currentPosition)
        {
            const size_t progressed = currentPosition > 4 ? currentPosition - 4 : 0;
            const int parsePercent = 12 + static_cast<int>((progressed * 58) / parseDenominator);
            if (parsePercent >= lastParsePercent + 2)
            {
                lastParsePercent = parsePercent;
                ReportProgress(progress, parsePercent, L"解析脚本文本");
            }
        };
        ReportProgress(progress, 12, L"解析脚本文本");
        while (position < opcodeLength)
        {
            const size_t opcodeAddr = position;
            const uint8_t opcode = data[position++];
            if (opcode == 0x0E)
            {
                if (position >= data.size())
                {
                    throw std::runtime_error("Invalid HCB string opcode.");
                }
                const uint8_t offset = data[position++];
                if (offset == 0 || position + offset - 1 > data.size())
                {
                    throw std::runtime_error("Invalid HCB string length.");
                }

                ByteBuffer rawBytes(data.begin() + position, data.begin() + position + offset - 1);
                const DecodedString decoded = DecodeScriptBytes(rawBytes, decodeMode);
                if (static_cast<int>(opcodeAddr) >= entryOffset)
                {
                    records.push_back(BuildLineRecord(
                        PathFilenameToUtf8(inputPath),
                        static_cast<int>(records.size()),
                        stringSlot,
                        static_cast<int>(opcodeAddr),
                        decoded.textUtf8,
                        lastVoice,
                        decoded.encoding,
                        false));
                }
                position += offset - 1;
                if (position >= data.size())
                {
                    throw std::runtime_error("Invalid HCB string terminator.");
                }
                ++position;
                lastVoice.reset();
                ++stringSlot;
                maybeReportParseProgress(position);
                continue;
            }

            if (opcode == 0x0A)
            {
                if (position + 4 > data.size())
                {
                    throw std::runtime_error("Invalid HCB opcode payload.");
                }
                const int32_t value = static_cast<int32_t>(ReadU32LE(data, position));
                if (value > 1000000)
                {
                    lastVoice = value;
                }
                position += 4;
                maybeReportParseProgress(position);
                continue;
            }

            if (opcode >= kOpcodeLength.size())
            {
                std::ostringstream message;
                message << "Invalid opcode 0x" << std::hex << static_cast<int>(opcode) << std::dec;
                throw std::runtime_error(message.str());
            }

            position += kOpcodeLength[opcode];
            if (position > data.size())
            {
                throw std::runtime_error("Unexpected end of HCB while skipping opcode payload.");
            }
            maybeReportParseProgress(position);
        }

        const size_t titlePos = static_cast<size_t>(opcodeLength) + 10;
        if (titlePos >= data.size())
        {
            throw std::runtime_error("Invalid HCB title position.");
        }
        const uint8_t titleLength = data[titlePos];
        if (titleLength == 0 || titlePos + titleLength > data.size())
        {
            throw std::runtime_error("Invalid HCB title length.");
        }

        ByteBuffer titleBytes(data.begin() + titlePos + 1, data.begin() + titlePos + titleLength);
        const DecodedString titleDecoded = DecodeScriptBytes(titleBytes, decodeMode);
        records.push_back(BuildLineRecord(
            PathFilenameToUtf8(inputPath),
            static_cast<int>(records.size()),
            stringSlot,
            static_cast<int>(opcodeLength + 10),
            titleDecoded.textUtf8,
            std::nullopt,
            titleDecoded.encoding,
            true));

        fs::create_directories(outputDir);

        ReportProgress(progress, 78, L"写出 output.txt");
        std::ostringstream legacyOutput;
        for (const auto &record : records)
        {
            legacyOutput << FormatLegacyOutput(record) << "\n";
        }
        WriteTextFileUtf8(outputDir / "output.txt", legacyOutput.str());

        ReportProgress(progress, 88, L"写出 lines.jsonl");
        std::ostringstream linesOutput;
        for (const auto &record : records)
        {
            linesOutput
                << "{"
                << "\"schema\":\"" << kLineSchema << "\","
                << "\"line_id\":" << record.lineId << ","
                << "\"string_slot\":" << record.stringSlot << ","
                << "\"opcode_addr\":" << record.opcodeAddr << ","
                << "\"source_file\":\"" << JsonEscape(record.sourceFile) << "\","
                << "\"text\":\"" << JsonEscape(record.text) << "\","
                << "\"voice_id\":" << JsonIntOrNull(record.voiceId) << ","
                << "\"voice_asset\":" << JsonQuotedOrNull(record.voiceAsset) << ","
                << "\"voice_container_hint\":" << JsonQuotedOrNull(record.voiceContainerHint) << ","
                << "\"text_kind\":\"" << JsonEscape(record.textKind) << "\","
                << "\"encoding\":\"" << JsonEscape(record.encoding) << "\","
                << "\"is_title\":" << (record.isTitle ? "true" : "false") << ","
                << "\"translated_text\":null"
                << "}\n";
        }
        WriteTextFileUtf8(outputDir / "lines.jsonl", linesOutput.str());

        std::map<int, std::vector<const LineRecord *>> voiceIndex;
        int voicedLineCount = 0;
        for (const auto &record : records)
        {
            if (record.voiceId.has_value())
            {
                ++voicedLineCount;
                voiceIndex[*record.voiceId].push_back(&record);
            }
        }

        ReportProgress(progress, 96, L"写出 voice_map.json");
        std::ostringstream voiceMap;
        voiceMap << "{\n"
                 << "  \"schema\": \"" << kVoiceMapSchema << "\",\n"
                 << "  \"generated_at\": \"" << GetIsoUtcNow() << "\",\n"
                 << "  \"source_file\": \"" << JsonEscape(PathFilenameToUtf8(inputPath)) << "\",\n"
                 << "  \"line_count\": " << records.size() << ",\n"
                 << "  \"voiced_line_count\": " << voicedLineCount << ",\n"
                 << "  \"voices\": [\n";

        bool firstVoice = true;
        for (const auto &[voiceId, items] : voiceIndex)
        {
            if (!firstVoice)
            {
                voiceMap << ",\n";
            }
            firstVoice = false;
            voiceMap << "    {\n"
                     << "      \"voice_id\": " << voiceId << ",\n"
                     << "      \"voice_asset\": \"" << voiceId << ".ogg\",\n"
                     << "      \"voice_container_hint\": \"voice.bin\",\n"
                     << "      \"line_count\": " << items.size() << ",\n"
                     << "      \"line_ids\": [";
            for (size_t index = 0; index < items.size(); ++index)
            {
                if (index > 0)
                {
                    voiceMap << ", ";
                }
                voiceMap << items[index]->lineId;
            }
            voiceMap << "],\n"
                     << "      \"texts\": [";
            for (size_t index = 0; index < items.size(); ++index)
            {
                if (index > 0)
                {
                    voiceMap << ", ";
                }
                voiceMap << "\"" << JsonEscape(items[index]->text) << "\"";
            }
            voiceMap << "]\n"
                     << "    }";
        }

        voiceMap << "\n  ],\n"
                 << "  \"line_map\": [\n";
        for (size_t index = 0; index < records.size(); ++index)
        {
            const auto &record = records[index];
            if (index > 0)
            {
                voiceMap << ",\n";
            }
            voiceMap << "    {"
                     << "\"line_id\":" << record.lineId << ","
                     << "\"opcode_addr\":" << record.opcodeAddr << ","
                     << "\"voice_id\":" << JsonIntOrNull(record.voiceId) << ","
                     << "\"voice_asset\":" << JsonQuotedOrNull(record.voiceAsset) << ","
                     << "\"text_kind\":\"" << JsonEscape(record.textKind) << "\","
                     << "\"encoding\":\"" << JsonEscape(record.encoding) << "\","
                     << "\"text\":\"" << JsonEscape(record.text) << "\""
                     << "}";
        }
        voiceMap << "\n  ]\n"
                 << "}\n";
        WriteTextFileUtf8(outputDir / "voice_map.json", voiceMap.str());

        ReportProgress(progress, 100, L"文本导出完成");

        return records;
    }

    std::vector<std::optional<ByteBuffer>> LoadReplacements(const fs::path &inputPath, const std::string &explicitFormat, const std::string &textEncoding)
    {
        const std::string format = InferInputFormat(inputPath, explicitFormat);
        const unsigned int codePage = EncodingToCodePage(textEncoding);
        if (format == "legacy")
        {
            return LoadLegacyReplacements(inputPath, codePage);
        }
        if (format == "jsonl")
        {
            return LoadJsonlReplacements(inputPath, codePage);
        }
        throw std::runtime_error("Unsupported input format: " + format);
    }

    void RepackText(const fs::path &hcbPath, const fs::path &translationPath, const fs::path &outputPath, const RepackOptions &options, const ProgressCallback &progress)
    {
        ReportProgress(progress, 5, L"读取 HCB 原文件");
        ByteBuffer hcb = ReadBinaryFile(hcbPath);
        ReportProgress(progress, 15, L"读取翻译输入");
        auto replacements = LoadReplacements(translationPath, options.inputFormat, options.textEncoding);

        std::vector<size_t> strAddr;
        std::vector<std::pair<size_t, uint32_t>> jmpAddr;
        std::map<uint32_t, int32_t> addrOffset;

        const uint32_t opcodeLength = ReadU32LE(hcb, 0);
        const uint32_t entry = ReadU32LE(hcb, opcodeLength);
        const size_t titlePos = static_cast<size_t>(opcodeLength) + 10;
        size_t position = titlePos + 1 + hcb[titlePos];
        const uint16_t sysFuncNum = ReadU16LE(hcb, position);
        position += 2;

        int threadStartIndex = -1;
        for (uint16_t index = 0; index < sysFuncNum; ++index)
        {
            ++position;
            const uint8_t funcLen = hcb[position++];
            std::string funcName(reinterpret_cast<const char *>(hcb.data() + position), reinterpret_cast<const char *>(hcb.data() + position + funcLen - 1));
            if (funcName == "ThreadStart")
            {
                threadStartIndex = index;
                break;
            }
            position += funcLen;
        }
        if (threadStartIndex < 0)
        {
            throw std::runtime_error("system function ThreadStart not found");
        }

        position = 4;
        int lastScanPercent = 24;
        const size_t scanDenominator = opcodeLength > 4 ? opcodeLength - 4 : 1;
        const auto maybeReportScanProgress = [&](size_t currentPosition)
        {
            const size_t progressed = currentPosition > 4 ? currentPosition - 4 : 0;
            const int scanPercent = 24 + static_cast<int>((progressed * 36) / scanDenominator);
            if (scanPercent >= lastScanPercent + 2)
            {
                lastScanPercent = scanPercent;
                ReportProgress(progress, scanPercent, L"分析文本和跳转表");
            }
        };
        ReportProgress(progress, 24, L"分析文本和跳转表");
        while (position < opcodeLength)
        {
            const uint8_t opcode = hcb[position++];
            if (opcode == 0x0E)
            {
                strAddr.push_back(position);
                position += 1 + hcb[position];
                maybeReportScanProgress(position);
                continue;
            }
            if (opcode == 0x02 || opcode == 0x06 || opcode == 0x07)
            {
                const uint32_t addr = ReadU32LE(hcb, position);
                jmpAddr.emplace_back(position, addr);
                addrOffset[addr] = 0;
                position += 4;
                maybeReportScanProgress(position);
                continue;
            }
            if (opcode == 0x0A)
            {
                if (position + 7 <= hcb.size() && hcb[position + 4] == 0x03 && ReadU16LE(hcb, position + 5) == threadStartIndex)
                {
                    const uint32_t addr = ReadU32LE(hcb, position);
                    jmpAddr.emplace_back(position, addr);
                    addrOffset[addr] = 0;
                    position += 7;
                }
                else
                {
                    position += 4;
                }
                maybeReportScanProgress(position);
                continue;
            }

            if (opcode >= kOpcodeLength.size())
            {
                std::ostringstream message;
                message << "Invalid opcode 0x" << std::hex << static_cast<int>(opcode) << std::dec;
                throw std::runtime_error(message.str());
            }
            position += kOpcodeLength[opcode];
            maybeReportScanProgress(position);
        }
        strAddr.push_back(titlePos);

        ReportProgress(progress, 66, L"计算地址偏移");
        int strIndex = 0;
        int32_t currentOffset = 0;
        int strNum = static_cast<int>(replacements.size()) - 1;
        if (static_cast<int>(strAddr.size()) != static_cast<int>(replacements.size()))
        {
            std::cerr << "Warning: Found " << strAddr.size() << " strings in HCB, but " << replacements.size() << " records in translation input.\n";
            strNum = std::min(static_cast<int>(strAddr.size()), static_cast<int>(replacements.size())) - 1;
        }

        addrOffset[entry] = 0;
        for (auto &[addr, offset] : addrOffset)
        {
            while (strIndex < strNum && addr > strAddr[static_cast<size_t>(strIndex)])
            {
                const auto &replacement = replacements[static_cast<size_t>(strIndex)];
                if (replacement.has_value())
                {
                    if (replacement->size() + 1 > 255)
                    {
                        throw std::runtime_error("Replacement string is too long for HCB storage.");
                    }
                    currentOffset += static_cast<int32_t>(replacement->size() + 1) - hcb[strAddr[static_cast<size_t>(strIndex)]];
                }
                ++strIndex;
            }
            offset = currentOffset;
        }

        ReportProgress(progress, 78, L"更新跳转地址");
        WriteU32LE(hcb, 0, opcodeLength + currentOffset);
        WriteU32LE(hcb, opcodeLength, entry + addrOffset[entry]);
        for (const auto &[pos, addr] : jmpAddr)
        {
            WriteU32LE(hcb, pos, addr + addrOffset[addr]);
        }

        EnsureParentDir(outputPath);
        ReportProgress(progress, 84, L"写出新的 HCB");
        std::ofstream output(outputPath, std::ios::binary);
        if (!output)
        {
            throw std::runtime_error("Failed to write output HCB: " + PathToUtf8(outputPath));
        }

        size_t writePosition = 0;
        for (int index = 0; index <= strNum; ++index)
        {
            const size_t addr = strAddr[static_cast<size_t>(index)];
            output.write(reinterpret_cast<const char *>(hcb.data() + writePosition), static_cast<std::streamsize>(addr - writePosition));
            const auto &replacement = replacements[static_cast<size_t>(index)];
            writePosition = addr + hcb[addr];
            if (replacement.has_value())
            {
                output.put(static_cast<char>(replacement->size() + 1));
                output.write(reinterpret_cast<const char *>(replacement->data()), static_cast<std::streamsize>(replacement->size()));
            }
            else
            {
                output.write(reinterpret_cast<const char *>(hcb.data() + addr), static_cast<std::streamsize>(writePosition - addr));
            }

            const int writePercent = strNum <= 0
                                         ? 96
                                         : 84 + static_cast<int>(((index + 1) * 12) / (strNum + 1));
            ReportProgress(progress, writePercent, L"写入文本段 [" + std::to_wstring(index + 1) + L"/" + std::to_wstring(strNum + 1) + L"]");
        }
        output.write(reinterpret_cast<const char *>(hcb.data() + writePosition), static_cast<std::streamsize>(hcb.size() - writePosition));
        ReportProgress(progress, 100, L"文本封包完成");
    }

} // namespace packcpp
