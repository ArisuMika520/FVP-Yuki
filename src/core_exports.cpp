#include "core_exports.h"

#include "archive.h"
#include "hcb.h"

#include <algorithm>
#include <cwchar>
#include <mutex>
#include <string>

namespace packcpp
{
    namespace coreexports
    {

        std::mutex g_stateMutex;
        std::wstring g_lastError;
        std::wstring g_progressMessage = L"等待任务开始。";
        int g_progressPercent = 0;
        std::wstring g_defaultUnpackRoot = L"unpack";
        std::wstring g_defaultTextOutputDir = (DefaultTextExtractDir()).wstring();

        void SetLastErrorMessage(const std::wstring &message)
        {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            g_lastError = message;
        }

        void ClearLastErrorMessage()
        {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            g_lastError.clear();
        }

        void SetProgressState(int percent, const std::wstring &message)
        {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            g_progressPercent = std::clamp(percent, 0, 100);
            g_progressMessage = message;
        }

        std::optional<fs::path> OptionalPathFromInput(const wchar_t *inputPath)
        {
            if (inputPath == nullptr || *inputPath == L'\0')
            {
                return std::nullopt;
            }
            return fs::path(inputPath);
        }

        std::string OptionalUtf8FromInput(const wchar_t *inputText)
        {
            if (inputText == nullptr || *inputText == L'\0')
            {
                return {};
            }
            return WideToUtf8(inputText);
        }

        std::wstring ComposeProgressMessage(const std::wstring &subject, const std::wstring &message)
        {
            if (message.empty())
            {
                return subject;
            }
            return subject + L" | " + message;
        }

        bool CopyToWideBuffer(const std::wstring &text, wchar_t *buffer, int bufferChars)
        {
            if (buffer == nullptr || bufferChars <= 0)
            {
                return false;
            }

            const size_t required = text.size() + 1;
            if (required > static_cast<size_t>(bufferChars))
            {
                return false;
            }

            std::wmemcpy(buffer, text.c_str(), required);
            return true;
        }

        DroppedFileKind DetectKind(const fs::path &inputPath)
        {
            const std::string extension = ToLowerAscii(inputPath.extension().string());
            if (extension == ".hcb")
            {
                return DroppedFileKind::Hcb;
            }
            if (extension == ".bin")
            {
                return DroppedFileKind::Bin;
            }
            return DroppedFileKind::Unsupported;
        }

        int FailWithMessage(const std::wstring &message)
        {
            SetLastErrorMessage(message);
            return 0;
        }

    } // namespace coreexports
} // namespace packcpp

int __stdcall PackCppDetectFileKind(const wchar_t *inputPath)
{
    if (inputPath == nullptr || *inputPath == L'\0')
    {
        return static_cast<int>(packcpp::DroppedFileKind::Unsupported);
    }

    return static_cast<int>(packcpp::coreexports::DetectKind(packcpp::fs::path(inputPath)));
}

int __stdcall PackCppExtractDroppedFile(const wchar_t *inputPath, wchar_t *outputPathBuffer, int outputPathBufferChars)
{
    using namespace packcpp;

    coreexports::ClearLastErrorMessage();
    if (inputPath == nullptr || *inputPath == L'\0')
    {
        return coreexports::FailWithMessage(L"输入文件路径为空。");
    }

    try
    {
        const fs::path sourcePath(inputPath);
        const std::wstring fileName = sourcePath.filename().wstring();
        const auto reportProgress = [&fileName](int percent, const std::wstring &message)
        {
            std::wstring fullMessage = fileName;
            if (!message.empty())
            {
                fullMessage += L" | ";
                fullMessage += message;
            }
            coreexports::SetProgressState(percent, fullMessage);
        };

        reportProgress(0, L"准备开始");
        if (!fs::exists(sourcePath))
        {
            return coreexports::FailWithMessage(L"输入文件不存在：" + sourcePath.wstring());
        }

        const DroppedFileKind kind = coreexports::DetectKind(sourcePath);
        fs::path outputPath;
        if (kind == DroppedFileKind::Hcb)
        {
            outputPath = fs::current_path() / DefaultTextExtractDir();
            ExtractText(sourcePath, 0, outputPath, DecodeMode::Auto, reportProgress);
        }
        else if (kind == DroppedFileKind::Bin)
        {
            outputPath = fs::current_path() / DefaultExtractParent(sourcePath) / (std::wstring(L"extracted_") + sourcePath.stem().wstring());
            ExtractBin(sourcePath, outputPath, reportProgress);
        }
        else
        {
            return coreexports::FailWithMessage(L"只支持拖入 .hcb 和 .bin 文件。");
        }

        if (!coreexports::CopyToWideBuffer(outputPath.wstring(), outputPathBuffer, outputPathBufferChars))
        {
            return coreexports::FailWithMessage(L"输出路径缓冲区不足。");
        }
        reportProgress(100, L"处理完成");
        return 1;
    }
    catch (const std::exception &ex)
    {
        return coreexports::FailWithMessage(Utf8ToWide(ex.what()));
    }
    catch (...)
    {
        return coreexports::FailWithMessage(L"发生未知错误。");
    }
}

const wchar_t *__stdcall PackCppGetLastErrorMessage()
{
    return packcpp::coreexports::g_lastError.c_str();
}

void __stdcall PackCppResetProgressState()
{
    packcpp::coreexports::SetProgressState(0, L"等待任务开始。");
}

int __stdcall PackCppGetProgressPercent()
{
    std::lock_guard<std::mutex> lock(packcpp::coreexports::g_stateMutex);
    return packcpp::coreexports::g_progressPercent;
}

int __stdcall PackCppCopyProgressMessage(wchar_t *messageBuffer, int messageBufferChars)
{
    std::lock_guard<std::mutex> lock(packcpp::coreexports::g_stateMutex);
    return packcpp::coreexports::CopyToWideBuffer(packcpp::coreexports::g_progressMessage, messageBuffer, messageBufferChars) ? 1 : 0;
}

const wchar_t *__stdcall PackCppGetDefaultUnpackRoot()
{
    return packcpp::coreexports::g_defaultUnpackRoot.c_str();
}

const wchar_t *__stdcall PackCppGetDefaultTextOutputDir()
{
    return packcpp::coreexports::g_defaultTextOutputDir.c_str();
}

int __stdcall PackCppRepackTextFile(
    const wchar_t *hcbPath,
    const wchar_t *translationPath,
    const wchar_t *outputPath,
    const wchar_t *inputFormat,
    const wchar_t *encoding,
    wchar_t *outputPathBuffer,
    int outputPathBufferChars)
{
    using namespace packcpp;

    coreexports::ClearLastErrorMessage();
    try
    {
        if (translationPath == nullptr || *translationPath == L'\0')
        {
            return coreexports::FailWithMessage(L"翻译文件路径为空。");
        }

        const fs::path sourceHcb = (hcbPath == nullptr || *hcbPath == L'\0') ? fs::path(L"AstralAirFinale.hcb") : fs::path(hcbPath);
        const fs::path translationFile(translationPath);
        const fs::path finalOutputPath = (outputPath == nullptr || *outputPath == L'\0') ? fs::path(L"output.hcb") : fs::path(outputPath);
        const std::wstring subject = finalOutputPath.filename().wstring();
        const auto reportProgress = [&subject](int percent, const std::wstring &message)
        {
            coreexports::SetProgressState(percent, coreexports::ComposeProgressMessage(subject, message));
        };

        reportProgress(0, L"准备开始");
        if (!fs::exists(sourceHcb))
        {
            return coreexports::FailWithMessage(L"未找到源 HCB 文件：" + sourceHcb.wstring());
        }
        if (!fs::exists(translationFile))
        {
            return coreexports::FailWithMessage(L"未找到翻译文件：" + translationFile.wstring());
        }

        RepackOptions options;
        options.inputFormat = coreexports::OptionalUtf8FromInput(inputFormat);
        const std::string textEncoding = coreexports::OptionalUtf8FromInput(encoding);
        if (!textEncoding.empty())
        {
            options.textEncoding = textEncoding;
        }

        RepackText(sourceHcb, translationFile, finalOutputPath, options, reportProgress);
        if (!coreexports::CopyToWideBuffer(finalOutputPath.wstring(), outputPathBuffer, outputPathBufferChars))
        {
            return coreexports::FailWithMessage(L"输出路径缓冲区不足。");
        }
        reportProgress(100, L"处理完成");
        return 1;
    }
    catch (const std::exception &ex)
    {
        return coreexports::FailWithMessage(Utf8ToWide(ex.what()));
    }
    catch (...)
    {
        return coreexports::FailWithMessage(L"发生未知错误。");
    }
}

int __stdcall PackCppRepackBinFile(
    const wchar_t *manifestOrDir,
    const wchar_t *outputPath,
    int autoRebuildImages,
    wchar_t *outputPathBuffer,
    int outputPathBufferChars,
    wchar_t *reportPathBuffer,
    int reportPathBufferChars)
{
    using namespace packcpp;

    coreexports::ClearLastErrorMessage();
    try
    {
        if (manifestOrDir == nullptr || *manifestOrDir == L'\0')
        {
            return coreexports::FailWithMessage(L"资源目录或 manifest 路径为空。");
        }

        const fs::path manifestPath(manifestOrDir);
        const std::optional<fs::path> outputBin = coreexports::OptionalPathFromInput(outputPath);
        const std::wstring subject = manifestPath.filename().wstring();
        const auto reportProgress = [&subject](int percent, const std::wstring &message)
        {
            coreexports::SetProgressState(percent, coreexports::ComposeProgressMessage(subject, message));
        };

        reportProgress(0, L"准备开始");
        const BinRepackResult result = RepackBin(manifestPath, outputBin, {autoRebuildImages != 0, true}, reportProgress);
        if (!coreexports::CopyToWideBuffer(result.outputPath.wstring(), outputPathBuffer, outputPathBufferChars))
        {
            return coreexports::FailWithMessage(L"输出路径缓冲区不足。");
        }
        if (!coreexports::CopyToWideBuffer(result.reportPath.wstring(), reportPathBuffer, reportPathBufferChars))
        {
            return coreexports::FailWithMessage(L"报告路径缓冲区不足。");
        }
        reportProgress(100, L"处理完成");
        return 1;
    }
    catch (const std::exception &ex)
    {
        return coreexports::FailWithMessage(Utf8ToWide(ex.what()));
    }
    catch (...)
    {
        return coreexports::FailWithMessage(L"发生未知错误。");
    }
}

int __stdcall PackCppBuildPatchBundle(
    const wchar_t *inputRoot,
    const wchar_t *outputDir,
    int autoRebuildImages,
    int includeUnchanged,
    const wchar_t *translationFile,
    const wchar_t *translationFormat,
    const wchar_t *hcbFile,
    const wchar_t *textOutputPath,
    const wchar_t *textEncoding,
    wchar_t *reportPathBuffer,
    int reportPathBufferChars)
{
    using namespace packcpp;

    coreexports::ClearLastErrorMessage();
    try
    {
        BuildPatchOptions options;
        if (const auto detectedOutputDir = coreexports::OptionalPathFromInput(outputDir))
        {
            options.outputDir = *detectedOutputDir;
        }
        if (const auto detectedTranslationFile = coreexports::OptionalPathFromInput(translationFile))
        {
            options.translationFile = *detectedTranslationFile;
        }
        options.translationFormat = coreexports::OptionalUtf8FromInput(translationFormat);
        if (const auto detectedHcb = coreexports::OptionalPathFromInput(hcbFile))
        {
            options.hcbFile = *detectedHcb;
        }
        if (const auto detectedTextOutput = coreexports::OptionalPathFromInput(textOutputPath))
        {
            options.textOutput = *detectedTextOutput;
        }
        const std::string encodingText = coreexports::OptionalUtf8FromInput(textEncoding);
        if (!encodingText.empty())
        {
            options.textEncoding = encodingText;
        }
        options.autoRebuildImages = autoRebuildImages != 0;
        options.includeUnchanged = includeUnchanged != 0;

        std::vector<fs::path> inputs;
        if (const auto rootPath = coreexports::OptionalPathFromInput(inputRoot))
        {
            inputs.push_back(*rootPath);
        }

        const std::wstring subject = options.outputDir.filename().wstring().empty() ? options.outputDir.wstring() : options.outputDir.filename().wstring();
        const auto reportProgress = [&subject](int percent, const std::wstring &message)
        {
            coreexports::SetProgressState(percent, coreexports::ComposeProgressMessage(subject, message));
        };

        reportProgress(0, L"准备开始");
        const fs::path reportPath = BuildPatch(inputs, options, reportProgress);
        if (!coreexports::CopyToWideBuffer(reportPath.wstring(), reportPathBuffer, reportPathBufferChars))
        {
            return coreexports::FailWithMessage(L"报告路径缓冲区不足。");
        }
        reportProgress(100, L"处理完成");
        return 1;
    }
    catch (const std::exception &ex)
    {
        return coreexports::FailWithMessage(Utf8ToWide(ex.what()));
    }
    catch (...)
    {
        return coreexports::FailWithMessage(L"发生未知错误。");
    }
}