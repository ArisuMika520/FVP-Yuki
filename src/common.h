#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace packcpp
{

    namespace fs = std::filesystem;

    using ByteBuffer = std::vector<uint8_t>;
    using ProgressCallback = std::function<void(int percent, const std::wstring &message)>;

    uint16_t ReadU16LE(const ByteBuffer &data, size_t offset);
    uint32_t ReadU32LE(const ByteBuffer &data, size_t offset);
    void WriteU32LE(ByteBuffer &data, size_t offset, uint32_t value);

    ByteBuffer ReadBinaryFile(const fs::path &path);
    std::string ReadTextFileUtf8(const fs::path &path);
    void WriteBinaryFile(const fs::path &path, const ByteBuffer &data);
    void WriteTextFileUtf8(const fs::path &path, const std::string &content);
    void EnsureParentDir(const fs::path &path);

    std::wstring DecodeBytesToWide(const ByteBuffer &raw, unsigned int codePage, bool strict);
    std::string WideToUtf8(std::wstring_view wide);
    std::wstring Utf8ToWide(std::string_view text);
    ByteBuffer EncodeWideToCodePage(std::wstring_view wide, unsigned int codePage);

    std::string PathToUtf8(const fs::path &path);
    std::string PathFilenameToUtf8(const fs::path &path);
    fs::path Utf8Path(std::string_view text);

    std::string ToLowerAscii(std::string value);
    bool StartsWithAsciiInsensitive(std::string_view text, std::string_view prefix);

    std::string JsonEscape(std::string_view text);
    std::string JsonQuotedOrNull(const std::optional<std::string> &value);
    std::string JsonIntOrNull(const std::optional<int> &value);

    std::vector<uint8_t> HexToBytes(std::string_view text);
    std::string BytesToHex(const uint8_t *data, size_t size);
    std::string BytesToHex(const ByteBuffer &data);

    std::string GetIsoUtcNow();
    std::string Sha256Hex(const ByteBuffer &data);
    std::string Sha256File(const fs::path &path);

    std::string NormalizeEncodingName(std::string value);
    unsigned int EncodingToCodePage(const std::string &encoding);

    std::string ParseJsonStringLiteral(std::string_view line, size_t &position);
    size_t SkipJsonWhitespace(std::string_view text, size_t position);
    int ExtractJsonIntField(std::string_view line, std::string_view key);
    std::optional<std::string> ExtractJsonOptionalStringField(std::string_view line, std::string_view key);

    template <typename MapType>
    void IncrementCounter(MapType &counter, const std::string &key, int amount = 1)
    {
        counter[key] = counter[key] + amount;
    }

} // namespace packcpp
