#pragma once

#include "common.h"

#include <optional>
#include <string>

namespace packcpp
{

    enum class DecodeMode
    {
        Auto,
        ShiftJis,
        Gbk,
    };

    struct DecodedString
    {
        std::wstring textWide;
        std::string textUtf8;
        std::string encoding;
        double score = 0.0;
    };

    DecodeMode ParseDecodeMode(const std::string &value);
    DecodedString DecodeScriptBytes(const ByteBuffer &rawBytes, DecodeMode mode);
    bool IsDialogueCandidate(const std::string &text);
    std::string ClassifyTextKind(const std::string &text, const std::optional<int> &voiceId, bool isTitle);

} // namespace packcpp
