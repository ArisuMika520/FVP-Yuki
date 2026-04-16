#define NOMINMAX
#include "common.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

#pragma comment(lib, "Bcrypt.lib")

namespace packcpp
{

    uint16_t ReadU16LE(const ByteBuffer &data, size_t offset)
    {
        if (offset + 2 > data.size())
        {
            throw std::runtime_error("Unexpected end of file while reading u16.");
        }
        return static_cast<uint16_t>(data[offset]) |
               (static_cast<uint16_t>(data[offset + 1]) << 8);
    }

    uint32_t ReadU32LE(const ByteBuffer &data, size_t offset)
    {
        if (offset + 4 > data.size())
        {
            throw std::runtime_error("Unexpected end of file while reading u32.");
        }
        return static_cast<uint32_t>(data[offset]) |
               (static_cast<uint32_t>(data[offset + 1]) << 8) |
               (static_cast<uint32_t>(data[offset + 2]) << 16) |
               (static_cast<uint32_t>(data[offset + 3]) << 24);
    }

    void WriteU32LE(ByteBuffer &data, size_t offset, uint32_t value)
    {
        if (offset + 4 > data.size())
        {
            throw std::runtime_error("Unexpected end of file while writing u32.");
        }
        data[offset] = static_cast<uint8_t>(value & 0xFF);
        data[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
        data[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
        data[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
    }

    void EnsureParentDir(const fs::path &path)
    {
        if (!path.parent_path().empty())
        {
            fs::create_directories(path.parent_path());
        }
    }

    ByteBuffer ReadBinaryFile(const fs::path &path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error("Failed to open file: " + PathToUtf8(path));
        }

        input.seekg(0, std::ios::end);
        const auto size = static_cast<size_t>(input.tellg());
        input.seekg(0, std::ios::beg);

        ByteBuffer buffer(size);
        if (size > 0)
        {
            input.read(reinterpret_cast<char *>(buffer.data()), static_cast<std::streamsize>(size));
        }
        return buffer;
    }

    std::string ReadTextFileUtf8(const fs::path &path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error("Failed to open file: " + PathToUtf8(path));
        }
        std::ostringstream buffer;
        buffer << input.rdbuf();
        return buffer.str();
    }

    void WriteBinaryFile(const fs::path &path, const ByteBuffer &data)
    {
        EnsureParentDir(path);
        std::ofstream output(path, std::ios::binary);
        if (!output)
        {
            throw std::runtime_error("Failed to write file: " + PathToUtf8(path));
        }
        if (!data.empty())
        {
            output.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
        }
    }

    void WriteTextFileUtf8(const fs::path &path, const std::string &content)
    {
        EnsureParentDir(path);
        std::ofstream output(path, std::ios::binary);
        if (!output)
        {
            throw std::runtime_error("Failed to write file: " + PathToUtf8(path));
        }
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    std::wstring DecodeBytesToWide(const ByteBuffer &raw, unsigned int codePage, bool strict)
    {
        if (raw.empty())
        {
            return L"";
        }
        const DWORD flags = strict ? MB_ERR_INVALID_CHARS : 0;
        const int sourceSize = static_cast<int>(raw.size());
        int required = MultiByteToWideChar(codePage, flags, reinterpret_cast<LPCCH>(raw.data()), sourceSize, nullptr, 0);
        if (required <= 0 && strict)
        {
            return L"";
        }
        if (required <= 0)
        {
            throw std::runtime_error("Failed to decode bytes with code page " + std::to_string(codePage) + ".");
        }
        std::wstring result(required, L'\0');
        required = MultiByteToWideChar(codePage, flags, reinterpret_cast<LPCCH>(raw.data()), sourceSize, result.data(), required);
        if (required <= 0)
        {
            throw std::runtime_error("Failed to decode bytes with code page " + std::to_string(codePage) + ".");
        }
        return result;
    }

    std::string WideToUtf8(std::wstring_view wide)
    {
        if (wide.empty())
        {
            return {};
        }
        int required = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        if (required <= 0)
        {
            throw std::runtime_error("Failed to convert wide string to UTF-8.");
        }
        std::string result(required, '\0');
        required = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), result.data(), required, nullptr, nullptr);
        if (required <= 0)
        {
            throw std::runtime_error("Failed to convert wide string to UTF-8.");
        }
        return result;
    }

    std::wstring Utf8ToWide(std::string_view text)
    {
        if (text.empty())
        {
            return {};
        }
        int required = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (required <= 0)
        {
            throw std::runtime_error("Failed to convert UTF-8 to wide string.");
        }
        std::wstring result(required, L'\0');
        required = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), required);
        if (required <= 0)
        {
            throw std::runtime_error("Failed to convert UTF-8 to wide string.");
        }
        return result;
    }

    ByteBuffer EncodeWideToCodePage(std::wstring_view wide, unsigned int codePage)
    {
        if (wide.empty())
        {
            return {};
        }

        BOOL usedDefaultChar = FALSE;
        int required = WideCharToMultiByte(
            codePage,
            WC_NO_BEST_FIT_CHARS,
            wide.data(),
            static_cast<int>(wide.size()),
            nullptr,
            0,
            nullptr,
            &usedDefaultChar);
        if (required <= 0)
        {
            throw std::runtime_error("Failed to encode text with code page " + std::to_string(codePage) + ".");
        }

        ByteBuffer result(static_cast<size_t>(required));
        usedDefaultChar = FALSE;
        required = WideCharToMultiByte(
            codePage,
            WC_NO_BEST_FIT_CHARS,
            wide.data(),
            static_cast<int>(wide.size()),
            reinterpret_cast<LPSTR>(result.data()),
            required,
            nullptr,
            &usedDefaultChar);
        if (required <= 0)
        {
            throw std::runtime_error("Failed to encode text with code page " + std::to_string(codePage) + ".");
        }
        if (usedDefaultChar)
        {
            throw std::runtime_error("Text contains characters that cannot be encoded with the selected code page.");
        }
        return result;
    }

    std::string PathToUtf8(const fs::path &path)
    {
        return WideToUtf8(path.generic_wstring());
    }

    std::string PathFilenameToUtf8(const fs::path &path)
    {
        return WideToUtf8(path.filename().wstring());
    }

    fs::path Utf8Path(std::string_view text)
    {
        return fs::path(Utf8ToWide(text));
    }

    std::string ToLowerAscii(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
                       { return static_cast<char>(std::tolower(ch)); });
        return value;
    }

    bool StartsWithAsciiInsensitive(std::string_view text, std::string_view prefix)
    {
        if (text.size() < prefix.size())
        {
            return false;
        }
        for (size_t index = 0; index < prefix.size(); ++index)
        {
            if (std::tolower(static_cast<unsigned char>(text[index])) != std::tolower(static_cast<unsigned char>(prefix[index])))
            {
                return false;
            }
        }
        return true;
    }

    std::string JsonEscape(std::string_view text)
    {
        std::ostringstream output;
        for (unsigned char ch : text)
        {
            switch (ch)
            {
            case '\\':
                output << "\\\\";
                break;
            case '"':
                output << "\\\"";
                break;
            case '\b':
                output << "\\b";
                break;
            case '\f':
                output << "\\f";
                break;
            case '\n':
                output << "\\n";
                break;
            case '\r':
                output << "\\r";
                break;
            case '\t':
                output << "\\t";
                break;
            default:
                if (ch < 0x20)
                {
                    output << "\\u"
                           << std::hex << std::setw(4) << std::setfill('0')
                           << static_cast<int>(ch)
                           << std::dec << std::setfill(' ');
                }
                else
                {
                    output << static_cast<char>(ch);
                }
                break;
            }
        }
        return output.str();
    }

    std::string JsonQuotedOrNull(const std::optional<std::string> &value)
    {
        if (!value.has_value())
        {
            return "null";
        }
        return "\"" + JsonEscape(*value) + "\"";
    }

    std::string JsonIntOrNull(const std::optional<int> &value)
    {
        if (!value.has_value())
        {
            return "null";
        }
        return std::to_string(*value);
    }

    std::vector<uint8_t> HexToBytes(std::string_view text)
    {
        if (text.size() % 2 != 0)
        {
            throw std::runtime_error("Hex string must have an even length.");
        }
        auto parseHexDigit = [](char ch) -> uint8_t
        {
            if ('0' <= ch && ch <= '9')
            {
                return static_cast<uint8_t>(ch - '0');
            }
            if ('a' <= ch && ch <= 'f')
            {
                return static_cast<uint8_t>(ch - 'a' + 10);
            }
            if ('A' <= ch && ch <= 'F')
            {
                return static_cast<uint8_t>(ch - 'A' + 10);
            }
            throw std::runtime_error("Invalid hex digit.");
        };

        ByteBuffer bytes(text.size() / 2);
        for (size_t index = 0; index < text.size(); index += 2)
        {
            bytes[index / 2] = static_cast<uint8_t>((parseHexDigit(text[index]) << 4) | parseHexDigit(text[index + 1]));
        }
        return bytes;
    }

    std::string BytesToHex(const uint8_t *data, size_t size)
    {
        std::ostringstream output;
        output << std::hex << std::setfill('0');
        for (size_t index = 0; index < size; ++index)
        {
            output << std::setw(2) << static_cast<int>(data[index]);
        }
        return output.str();
    }

    std::string BytesToHex(const ByteBuffer &data)
    {
        return BytesToHex(data.data(), data.size());
    }

    std::string GetIsoUtcNow()
    {
        SYSTEMTIME utc{};
        GetSystemTime(&utc);
        char buffer[64]{};
        std::snprintf(
            buffer,
            sizeof(buffer),
            "%04u-%02u-%02uT%02u:%02u:%02uZ",
            utc.wYear,
            utc.wMonth,
            utc.wDay,
            utc.wHour,
            utc.wMinute,
            utc.wSecond);
        return buffer;
    }

    std::string Sha256Hex(const ByteBuffer &data)
    {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        DWORD objectSize = 0;
        DWORD hashLength = 0;
        DWORD bytesWritten = 0;

        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
        {
            throw std::runtime_error("Failed to open SHA-256 provider.");
        }
        if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &bytesWritten, 0) != 0 ||
            BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength), &bytesWritten, 0) != 0)
        {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            throw std::runtime_error("Failed to query SHA-256 provider properties.");
        }

        ByteBuffer objectBuffer(objectSize);
        ByteBuffer hashBuffer(hashLength);
        if (BCryptCreateHash(algorithm, &hash, objectBuffer.data(), objectSize, nullptr, 0, 0) != 0)
        {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            throw std::runtime_error("Failed to create SHA-256 hash.");
        }
        if (!data.empty() && BCryptHashData(hash, const_cast<PUCHAR>(data.data()), static_cast<ULONG>(data.size()), 0) != 0)
        {
            BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            throw std::runtime_error("Failed to hash data.");
        }
        if (BCryptFinishHash(hash, hashBuffer.data(), hashLength, 0) != 0)
        {
            BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            throw std::runtime_error("Failed to finalize SHA-256 hash.");
        }

        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return BytesToHex(hashBuffer);
    }

    std::string Sha256File(const fs::path &path)
    {
        return Sha256Hex(ReadBinaryFile(path));
    }

    std::string NormalizeEncodingName(std::string value)
    {
        value = ToLowerAscii(value);
        std::replace(value.begin(), value.end(), '-', '_');
        if (value == "shiftjis" || value == "sjis" || value == "cp932" || value == "ms932")
        {
            return "shift_jis";
        }
        if (value == "gb2312" || value == "cp936")
        {
            return "gbk";
        }
        return value;
    }

    unsigned int EncodingToCodePage(const std::string &encoding)
    {
        const std::string normalized = NormalizeEncodingName(encoding);
        if (normalized == "shift_jis")
        {
            return 932;
        }
        if (normalized == "gbk")
        {
            return 936;
        }
        throw std::runtime_error("Unsupported encoding: " + encoding);
    }

    void AppendUtf8Codepoint(uint32_t codepoint, std::string &output)
    {
        if (codepoint <= 0x7F)
        {
            output.push_back(static_cast<char>(codepoint));
        }
        else if (codepoint <= 0x7FF)
        {
            output.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
        else if (codepoint <= 0xFFFF)
        {
            output.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
        else
        {
            output.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
    }

    bool ParseHex4(std::string_view text, size_t offset, uint32_t &value)
    {
        if (offset + 4 > text.size())
        {
            return false;
        }
        value = 0;
        for (size_t index = 0; index < 4; ++index)
        {
            const char ch = text[offset + index];
            value <<= 4;
            if (ch >= '0' && ch <= '9')
            {
                value |= static_cast<uint32_t>(ch - '0');
            }
            else if (ch >= 'a' && ch <= 'f')
            {
                value |= static_cast<uint32_t>(ch - 'a' + 10);
            }
            else if (ch >= 'A' && ch <= 'F')
            {
                value |= static_cast<uint32_t>(ch - 'A' + 10);
            }
            else
            {
                return false;
            }
        }
        return true;
    }

    std::string ParseJsonStringLiteral(std::string_view line, size_t &position)
    {
        if (position >= line.size() || line[position] != '"')
        {
            throw std::runtime_error("Expected JSON string literal.");
        }
        ++position;
        std::string output;
        while (position < line.size())
        {
            const unsigned char ch = static_cast<unsigned char>(line[position++]);
            if (ch == '"')
            {
                return output;
            }
            if (ch != '\\')
            {
                output.push_back(static_cast<char>(ch));
                continue;
            }
            if (position >= line.size())
            {
                throw std::runtime_error("Unterminated JSON escape sequence.");
            }
            const char esc = line[position++];
            switch (esc)
            {
            case '"':
                output.push_back('"');
                break;
            case '\\':
                output.push_back('\\');
                break;
            case '/':
                output.push_back('/');
                break;
            case 'b':
                output.push_back('\b');
                break;
            case 'f':
                output.push_back('\f');
                break;
            case 'n':
                output.push_back('\n');
                break;
            case 'r':
                output.push_back('\r');
                break;
            case 't':
                output.push_back('\t');
                break;
            case 'u':
            {
                uint32_t codepoint = 0;
                if (!ParseHex4(line, position, codepoint))
                {
                    throw std::runtime_error("Invalid JSON unicode escape.");
                }
                position += 4;
                if (0xD800 <= codepoint && codepoint <= 0xDBFF)
                {
                    if (position + 6 <= line.size() && line[position] == '\\' && line[position + 1] == 'u')
                    {
                        uint32_t low = 0;
                        if (!ParseHex4(line, position + 2, low))
                        {
                            throw std::runtime_error("Invalid low surrogate in JSON string.");
                        }
                        if (0xDC00 <= low && low <= 0xDFFF)
                        {
                            position += 6;
                            codepoint = 0x10000 + (((codepoint - 0xD800) << 10) | (low - 0xDC00));
                        }
                    }
                }
                AppendUtf8Codepoint(codepoint, output);
                break;
            }
            default:
                throw std::runtime_error("Unsupported JSON escape sequence.");
            }
        }
        throw std::runtime_error("Unterminated JSON string literal.");
    }

    size_t SkipJsonWhitespace(std::string_view text, size_t position)
    {
        while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])))
        {
            ++position;
        }
        return position;
    }

    size_t FindJsonKey(std::string_view line, std::string_view key)
    {
        const std::string pattern = std::string{"\""} + std::string{key} + "\"";
        return line.find(pattern);
    }

    int ExtractJsonIntField(std::string_view line, std::string_view key)
    {
        size_t position = FindJsonKey(line, key);
        if (position == std::string::npos)
        {
            throw std::runtime_error("Missing JSON field: " + std::string(key));
        }
        position += key.size() + 2;
        position = line.find(':', position);
        if (position == std::string::npos)
        {
            throw std::runtime_error("Invalid JSON field: " + std::string(key));
        }
        position = SkipJsonWhitespace(line, position + 1);
        bool negative = false;
        if (position < line.size() && line[position] == '-')
        {
            negative = true;
            ++position;
        }
        int value = 0;
        bool hasDigit = false;
        while (position < line.size() && std::isdigit(static_cast<unsigned char>(line[position])))
        {
            hasDigit = true;
            value = value * 10 + (line[position] - '0');
            ++position;
        }
        if (!hasDigit)
        {
            throw std::runtime_error("Invalid integer JSON field: " + std::string(key));
        }
        return negative ? -value : value;
    }

    std::optional<std::string> ExtractJsonOptionalStringField(std::string_view line, std::string_view key)
    {
        size_t position = FindJsonKey(line, key);
        if (position == std::string::npos)
        {
            throw std::runtime_error("Missing JSON field: " + std::string(key));
        }
        position += key.size() + 2;
        position = line.find(':', position);
        if (position == std::string::npos)
        {
            throw std::runtime_error("Invalid JSON field: " + std::string(key));
        }
        position = SkipJsonWhitespace(line, position + 1);
        if (line.substr(position, 4) == "null")
        {
            return std::nullopt;
        }
        return ParseJsonStringLiteral(line, position);
    }

} // namespace packcpp
