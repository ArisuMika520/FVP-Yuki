#pragma once

#include <windows.h>

#ifdef PACK_CPP_CORE_EXPORTS
#define PACK_CPP_CORE_API extern "C" __declspec(dllexport)
#else
#define PACK_CPP_CORE_API extern "C" __declspec(dllimport)
#endif

namespace packcpp
{

    enum class DroppedFileKind : int
    {
        Unsupported = 0,
        Hcb = 1,
        Bin = 2,
    };

}

PACK_CPP_CORE_API int __stdcall PackCppDetectFileKind(const wchar_t *inputPath);
PACK_CPP_CORE_API int __stdcall PackCppExtractDroppedFile(const wchar_t *inputPath, wchar_t *outputPathBuffer, int outputPathBufferChars);
PACK_CPP_CORE_API int __stdcall PackCppRepackTextFile(const wchar_t *hcbPath, const wchar_t *translationPath, const wchar_t *outputPath, const wchar_t *inputFormat, const wchar_t *encoding, wchar_t *outputPathBuffer, int outputPathBufferChars);
PACK_CPP_CORE_API int __stdcall PackCppRepackBinFile(const wchar_t *manifestOrDir, const wchar_t *outputPath, int autoRebuildImages, wchar_t *outputPathBuffer, int outputPathBufferChars, wchar_t *reportPathBuffer, int reportPathBufferChars);
PACK_CPP_CORE_API int __stdcall PackCppBuildPatchBundle(const wchar_t *inputRoot, const wchar_t *outputDir, int autoRebuildImages, int includeUnchanged, const wchar_t *translationFile, const wchar_t *translationFormat, const wchar_t *hcbFile, const wchar_t *textOutputPath, const wchar_t *textEncoding, wchar_t *reportPathBuffer, int reportPathBufferChars);
PACK_CPP_CORE_API const wchar_t *__stdcall PackCppGetLastErrorMessage();
PACK_CPP_CORE_API void __stdcall PackCppResetProgressState();
PACK_CPP_CORE_API int __stdcall PackCppGetProgressPercent();
PACK_CPP_CORE_API int __stdcall PackCppCopyProgressMessage(wchar_t *messageBuffer, int messageBufferChars);
PACK_CPP_CORE_API const wchar_t *__stdcall PackCppGetDefaultUnpackRoot();
PACK_CPP_CORE_API const wchar_t *__stdcall PackCppGetDefaultTextOutputDir();