#using < System.dll>
#using < System.Drawing.dll>
#using < System.IO.Compression.dll>

#include "hzc1.h"

#include <msclr/marshal_cppstd.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

using namespace System;
using namespace System::Drawing;
using namespace System::Drawing::Imaging;
using namespace System::IO;
using namespace System::IO::Compression;
using namespace System::Runtime::InteropServices;

namespace packcpp
{
    namespace
    {

        std::string ManagedStringToUtf8(String ^ text)
        {
            if (text == nullptr)
            {
                return {};
            }
            return WideToUtf8(msclr::interop::marshal_as<std::wstring>(text));
        }

        String ^ PathToManagedString(const fs::path &path)
        {
            const std::wstring wide = path.wstring();
            return gcnew String(wide.c_str());
        }

        array<Byte> ^ ToManagedBytes(const ByteBuffer &data) {
            auto managed = gcnew array<Byte>(static_cast<int>(data.size()));
            if (!data.empty())
            {
                Marshal::Copy(IntPtr(const_cast<uint8_t *>(data.data())), managed, 0, managed->Length);
            }
            return managed;
        }

            ByteBuffer ToNativeBytes(array<Byte> ^ data)
        {
            ByteBuffer result(static_cast<size_t>(data->Length));
            if (data->Length > 0)
            {
                Marshal::Copy(data, 0, IntPtr(result.data()), data->Length);
            }
            return result;
        }

        uint32_t ReadBigEndianU32(const ByteBuffer &data, size_t offset)
        {
            if (offset + 4 > data.size())
            {
                throw std::runtime_error("Unexpected end of buffer while reading big-endian u32.");
            }
            return (static_cast<uint32_t>(data[offset]) << 24) |
                   (static_cast<uint32_t>(data[offset + 1]) << 16) |
                   (static_cast<uint32_t>(data[offset + 2]) << 8) |
                   static_cast<uint32_t>(data[offset + 3]);
        }

        void AppendBigEndianU32(ByteBuffer &data, uint32_t value)
        {
            data.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
            data.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
            data.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
            data.push_back(static_cast<uint8_t>(value & 0xFF));
        }

        uint32_t Adler32(const ByteBuffer &data)
        {
            constexpr uint32_t kMod = 65521;
            uint32_t a = 1;
            uint32_t b = 0;
            for (uint8_t byte : data)
            {
                a = (a + byte) % kMod;
                b = (b + a) % kMod;
            }
            return (b << 16) | a;
        }

        bool LooksLikeZlibStream(const ByteBuffer &data)
        {
            if (data.size() < 6)
            {
                return false;
            }
            const uint8_t cmf = data[0];
            const uint8_t flg = data[1];
            if ((cmf & 0x0F) != 8)
            {
                return false;
            }
            if ((((static_cast<unsigned int>(cmf) << 8) | flg) % 31) != 0)
            {
                return false;
            }
            if ((flg & 0x20) != 0)
            {
                return false;
            }
            return true;
        }

        ByteBuffer InflateWithDeflateStream(const ByteBuffer &compressed)
        {
            try
            {
                auto inputBytes = ToManagedBytes(compressed);
                auto inputStream = gcnew MemoryStream(inputBytes, false);
                auto inflater = gcnew DeflateStream(inputStream, CompressionMode::Decompress, true);
                auto outputStream = gcnew MemoryStream();
                try
                {
                    inflater->CopyTo(outputStream);
                    return ToNativeBytes(outputStream->ToArray());
                }
                finally
                {
                    delete outputStream;
                    delete inflater;
                    delete inputStream;
                }
            }
            catch (Exception ^ ex)
            {
                throw std::runtime_error("Deflate decompression failed: " + ManagedStringToUtf8(ex->Message));
            }
        }

        ByteBuffer DecompressZlibPayload(const ByteBuffer &compressed)
        {
            if (!LooksLikeZlibStream(compressed))
            {
                return InflateWithDeflateStream(compressed);
            }

            const uint32_t expectedAdler = ReadBigEndianU32(compressed, compressed.size() - 4);

            try
            {
                ByteBuffer inflated = InflateWithDeflateStream(compressed);
                if (Adler32(inflated) == expectedAdler)
                {
                    return inflated;
                }
            }
            catch (const std::exception &)
            {
            }

            ByteBuffer inner(compressed.begin() + 2, compressed.end() - 4);
            ByteBuffer inflated = InflateWithDeflateStream(inner);
            if (Adler32(inflated) != expectedAdler)
            {
                throw std::runtime_error("Zlib Adler-32 check failed.");
            }
            return inflated;
        }

        ByteBuffer DeflateWithManagedStream(const ByteBuffer &uncompressed)
        {
            try
            {
                auto outputStream = gcnew MemoryStream();
                auto deflater = gcnew DeflateStream(outputStream, CompressionLevel::Optimal, true);
                try
                {
                    auto inputBytes = ToManagedBytes(uncompressed);
                    deflater->Write(inputBytes, 0, inputBytes->Length);
                    deflater->Flush();
                }
                finally
                {
                    delete deflater;
                }
                ByteBuffer compressed = ToNativeBytes(outputStream->ToArray());
                delete outputStream;
                return compressed;
            }
            catch (Exception ^ ex)
            {
                throw std::runtime_error("Deflate compression failed: " + ManagedStringToUtf8(ex->Message));
            }
        }

        ByteBuffer CompressZlibPayload(const ByteBuffer &uncompressed)
        {
            ByteBuffer compressed = DeflateWithManagedStream(uncompressed);
            const uint32_t adler = Adler32(uncompressed);
            if (LooksLikeZlibStream(compressed) && ReadBigEndianU32(compressed, compressed.size() - 4) == adler)
            {
                return compressed;
            }

            ByteBuffer wrapped;
            wrapped.reserve(compressed.size() + 6);
            wrapped.push_back(0x78);
            wrapped.push_back(0xDA);
            wrapped.insert(wrapped.end(), compressed.begin(), compressed.end());
            AppendBigEndianU32(wrapped, adler);
            return wrapped;
        }

        void CopyNativeRowsToBitmap(Bitmap ^ bitmap, const ByteBuffer &rawPixels, int rowSize)
        {
            const System::Drawing::Rectangle rect(0, 0, bitmap->Width, bitmap->Height);
            BitmapData ^ data = bitmap->LockBits(rect, ImageLockMode::WriteOnly, bitmap->PixelFormat);
            try
            {
                auto managedBytes = ToManagedBytes(rawPixels);
                for (int row = 0; row < bitmap->Height; ++row)
                {
                    const long long rowAddress = data->Stride >= 0
                                                     ? data->Scan0.ToInt64() + static_cast<long long>(row) * data->Stride
                                                     : data->Scan0.ToInt64() + static_cast<long long>(bitmap->Height - 1 - row) * (-data->Stride);
                    Marshal::Copy(managedBytes, row * rowSize, IntPtr(rowAddress), rowSize);
                }
            }
            finally
            {
                bitmap->UnlockBits(data);
            }
        }

        ByteBuffer CopyBitmapRows(Bitmap ^ bitmap, PixelFormat pixelFormat, int rowSize)
        {
            const System::Drawing::Rectangle rect(0, 0, bitmap->Width, bitmap->Height);
            BitmapData ^ data = bitmap->LockBits(rect, ImageLockMode::ReadOnly, pixelFormat);
            try
            {
                auto managedBytes = gcnew array<Byte>(bitmap->Height * rowSize);
                for (int row = 0; row < bitmap->Height; ++row)
                {
                    const long long rowAddress = data->Stride >= 0
                                                     ? data->Scan0.ToInt64() + static_cast<long long>(row) * data->Stride
                                                     : data->Scan0.ToInt64() + static_cast<long long>(bitmap->Height - 1 - row) * (-data->Stride);
                    Marshal::Copy(IntPtr(rowAddress), managedBytes, row * rowSize, rowSize);
                }
                return ToNativeBytes(managedBytes);
            }
            finally
            {
                bitmap->UnlockBits(data);
            }
        }

        ByteBuffer BuildHzc1(const Hzc1Metadata &metadata, const ByteBuffer &rawPixels, const ByteBuffer &trailingPadding)
        {
            if (rawPixels.size() != metadata.ExpectedRawSize())
            {
                throw std::runtime_error("Unexpected raw pixel size for HZC1 rebuild.");
            }
            if (metadata.payloadHeader.size() != 32)
            {
                throw std::runtime_error("Original HZC1 payload header is missing or invalid.");
            }

            ByteBuffer rawBlob = rawPixels;
            rawBlob.insert(rawBlob.end(), trailingPadding.begin(), trailingPadding.end());
            ByteBuffer compressed = CompressZlibPayload(rawBlob);

            ByteBuffer output;
            output.reserve(44 + compressed.size());
            output.insert(output.end(), {'h', 'z', 'c', '1'});
            output.resize(12);
            WriteU32LE(output, 4, static_cast<uint32_t>(rawBlob.size()));
            WriteU32LE(output, 8, metadata.headerSize);
            output.insert(output.end(), metadata.payloadHeader.begin(), metadata.payloadHeader.end());
            output.insert(output.end(), compressed.begin(), compressed.end());
            return output;
        }

        ByteBuffer LoadPngPixels(const fs::path &pngPath, const Hzc1Metadata &metadata)
        {
            Bitmap ^ original = nullptr;
            Bitmap ^ converted = nullptr;
            try
            {
                original = gcnew Bitmap(PathToManagedString(pngPath));
                if (original->Width != metadata.width || original->Height != metadata.height)
                {
                    throw std::runtime_error(
                        "PNG dimensions do not match original HZC1 image: " +
                        std::to_string(original->Width) + "x" + std::to_string(original->Height) +
                        " vs " + std::to_string(metadata.width) + "x" + std::to_string(metadata.height));
                }

                const PixelFormat targetFormat = metadata.BitsPerPixel() == 32 ? PixelFormat::Format32bppArgb : PixelFormat::Format24bppRgb;
                if (original->PixelFormat == targetFormat)
                {
                    converted = original;
                }
                else
                {
                    converted = original->Clone(System::Drawing::Rectangle(0, 0, original->Width, original->Height), targetFormat);
                }

                ByteBuffer pixels = CopyBitmapRows(converted, targetFormat, metadata.width * metadata.ChannelCount());
                if (converted != nullptr && converted != original)
                {
                    delete converted;
                }
                if (original != nullptr)
                {
                    delete original;
                }
                return pixels;
            }
            catch (const std::exception &)
            {
                if (converted != nullptr && converted != original)
                {
                    delete converted;
                }
                if (original != nullptr)
                {
                    delete original;
                }
                throw;
            }
            catch (Exception ^ ex)
            {
                if (converted != nullptr && converted != original)
                {
                    delete converted;
                }
                if (original != nullptr)
                {
                    delete original;
                }
                throw std::runtime_error("Failed to load PNG: " + ManagedStringToUtf8(ex->Message));
            }
        }

    } // namespace

    Hzc1Metadata ParseHzc1Metadata(const ByteBuffer &data)
    {
        Hzc1Metadata metadata;
        if (data.size() < 44 || std::memcmp(data.data(), "hzc1", 4) != 0)
        {
            return metadata;
        }
        if (std::memcmp(data.data() + 12, "NVSG", 4) != 0)
        {
            return metadata;
        }

        metadata.uncompressedSize = ReadU32LE(data, 4);
        metadata.headerSize = ReadU32LE(data, 8);
        metadata.unknown1 = ReadU16LE(data, 16);
        metadata.bppFlag = ReadU16LE(data, 18);
        metadata.width = ReadU16LE(data, 20);
        metadata.height = ReadU16LE(data, 22);
        metadata.offsetX = ReadU16LE(data, 24);
        metadata.offsetY = ReadU16LE(data, 26);
        metadata.unknown2 = ReadU16LE(data, 28);
        metadata.unknown3 = ReadU16LE(data, 30);
        metadata.payloadHeader.assign(data.begin() + 12, data.begin() + 44);
        metadata.valid = true;
        return metadata;
    }

    Hzc1DecodeResult DecodeHzc1(const ByteBuffer &data)
    {
        Hzc1DecodeResult result;
        result.metadata = ParseHzc1Metadata(data);
        if (!result.metadata.valid)
        {
            throw std::runtime_error("Invalid HZC1 header.");
        }
        if (data.size() <= 44)
        {
            throw std::runtime_error("HZC1 payload is truncated.");
        }

        ByteBuffer compressed(data.begin() + 44, data.end());
        ByteBuffer rawData = DecompressZlibPayload(compressed);
        if (rawData.size() < result.metadata.ExpectedRawSize())
        {
            throw std::runtime_error("HZC1 raw pixel payload is shorter than expected.");
        }

        result.rawPixels.assign(rawData.begin(), rawData.begin() + static_cast<std::ptrdiff_t>(result.metadata.ExpectedRawSize()));
        result.trailingPadding.assign(rawData.begin() + static_cast<std::ptrdiff_t>(result.metadata.ExpectedRawSize()), rawData.end());
        return result;
    }

    void ExportPngPreview(const ByteBuffer &data, const fs::path &outputPath)
    {
        const Hzc1DecodeResult decoded = DecodeHzc1(data);
        Bitmap ^ bitmap = nullptr;
        try
        {
            const PixelFormat pixelFormat = decoded.metadata.BitsPerPixel() == 32 ? PixelFormat::Format32bppArgb : PixelFormat::Format24bppRgb;
            bitmap = gcnew Bitmap(decoded.metadata.width, decoded.metadata.height, pixelFormat);
            CopyNativeRowsToBitmap(bitmap, decoded.rawPixels, decoded.metadata.width * decoded.metadata.ChannelCount());
            EnsureParentDir(outputPath);
            bitmap->Save(PathToManagedString(outputPath), ImageFormat::Png);
        }
        catch (Exception ^ ex)
        {
            if (bitmap != nullptr)
            {
                delete bitmap;
            }
            throw std::runtime_error("Failed to export PNG preview: " + ManagedStringToUtf8(ex->Message));
        }
        if (bitmap != nullptr)
        {
            delete bitmap;
        }
    }

    std::pair<ByteBuffer, Hzc1Metadata> RebuildHzc1FromPng(const ByteBuffer &originalHzc1, const fs::path &pngPath, int /*compressionLevel*/)
    {
        const Hzc1DecodeResult decoded = DecodeHzc1(originalHzc1);
        const ByteBuffer rawPixels = LoadPngPixels(pngPath, decoded.metadata);
        const ByteBuffer rebuilt = BuildHzc1(decoded.metadata, rawPixels, decoded.trailingPadding);
        return {rebuilt, decoded.metadata};
    }

    fs::path PreviewSingleHzc1(const fs::path &inputPath, const std::optional<fs::path> &outputPng)
    {
        fs::path outputPath = inputPath;
        if (outputPng.has_value())
        {
            outputPath = *outputPng;
        }
        else
        {
            outputPath.replace_extension(L".png");
        }
        ExportPngPreview(ReadBinaryFile(inputPath), outputPath);
        return outputPath;
    }

    fs::path RebuildSingleHzc1(const fs::path &originalHzc1, const fs::path &inputPng, const std::optional<fs::path> &outputHzc1)
    {
        const fs::path outputPath = outputHzc1.has_value() ? *outputHzc1 : originalHzc1.parent_path() / (originalHzc1.stem().wstring() + L".rebuilt.hzc1");
        const auto rebuilt = RebuildHzc1FromPng(ReadBinaryFile(originalHzc1), inputPng);
        WriteBinaryFile(outputPath, rebuilt.first);
        return outputPath;
    }

} // namespace packcpp
