#include "text_codec.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <stdexcept>
#include <vector>

namespace packcpp
{
    namespace
    {

        struct TextProfile
        {
            int cjk = 0;
            int hiragana = 0;
            int katakana = 0;
            int halfwidthKatakana = 0;
            int latin = 0;
            int digits = 0;
            int punctuation = 0;
            int fullwidthAscii = 0;
            int control = 0;
            int replacement = 0;
            int rareSymbol = 0;
            int japanesePunct = 0;
            int chinesePunct = 0;
        };

        bool IsJapanesePunctuation(wchar_t ch)
        {
            static const std::wstring kChars = L"、。・「」『』【】〈〉《》ー〜～…";
            return kChars.find(ch) != std::wstring::npos;
        }

        bool IsChinesePunctuation(wchar_t ch)
        {
            static const std::wstring kChars = L"，。！？；：、“”‘’（）【】《》〈〉「」『』—……";
            return kChars.find(ch) != std::wstring::npos;
        }

        TextProfile BuildTextProfile(std::wstring_view text)
        {
            TextProfile profile;
            for (const wchar_t ch : text)
            {
                const uint32_t codepoint = static_cast<uint32_t>(ch);
                if (ch == 0xFFFD)
                {
                    ++profile.replacement;
                }
                else if (ch == L'\t' || ch == L'\r' || ch == L'\n')
                {
                    ++profile.punctuation;
                }
                else if (codepoint < 0x20 || (0x7F <= codepoint && codepoint < 0xA0))
                {
                    ++profile.control;
                }
                else if (0x3040 <= codepoint && codepoint <= 0x309F)
                {
                    ++profile.hiragana;
                }
                else if (0x30A0 <= codepoint && codepoint <= 0x30FF)
                {
                    ++profile.katakana;
                }
                else if (0xFF61 <= codepoint && codepoint <= 0xFF9F)
                {
                    ++profile.halfwidthKatakana;
                }
                else if ((0x4E00 <= codepoint && codepoint <= 0x9FFF) || (0x3400 <= codepoint && codepoint <= 0x4DBF))
                {
                    ++profile.cjk;
                }
                else if (ch < 128 && std::isalpha(static_cast<unsigned char>(ch)))
                {
                    ++profile.latin;
                }
                else if (ch < 128 && std::isdigit(static_cast<unsigned char>(ch)))
                {
                    ++profile.digits;
                }
                else if (0xFF01 <= codepoint && codepoint <= 0xFF5E)
                {
                    ++profile.fullwidthAscii;
                }
                else if (IsJapanesePunctuation(ch))
                {
                    ++profile.japanesePunct;
                    ++profile.punctuation;
                }
                else if (IsChinesePunctuation(ch))
                {
                    ++profile.chinesePunct;
                    ++profile.punctuation;
                }
                else if (std::iswdigit(ch))
                {
                    ++profile.digits;
                }
                else if (std::iswalpha(ch))
                {
                    ++profile.latin;
                }
                else if (std::iswspace(ch) || std::wstring_view(L".,!?;:'\"()[]{}<>/\\|@#$%^&*-_=+`~").find(ch) != std::wstring_view::npos)
                {
                    ++profile.punctuation;
                }
                else
                {
                    ++profile.rareSymbol;
                }
            }
            return profile;
        }

        double ScoreDecodedText(std::wstring_view text, std::string_view encoding)
        {
            const TextProfile profile = BuildTextProfile(text);
            double score = 0.0;
            score += profile.cjk * 1.1;
            score += profile.hiragana * 3.2;
            score += profile.katakana * 2.4;
            score += profile.latin * 0.15;
            score += profile.digits * 0.15;
            score += profile.punctuation * 0.08;
            score += profile.fullwidthAscii * 0.2;
            score -= profile.halfwidthKatakana * 4.0;
            score -= profile.control * 8.0;
            score -= profile.replacement * 12.0;
            score -= profile.rareSymbol * 0.75;

            if (encoding == "shift_jis")
            {
                score += profile.hiragana * 2.8;
                score += profile.katakana * 1.8;
                score += profile.japanesePunct * 0.8;
                score -= profile.chinesePunct * 0.3;
                if (profile.halfwidthKatakana > 0 && profile.hiragana + profile.katakana == 0 && profile.cjk > 0)
                {
                    score -= profile.halfwidthKatakana * 2.5;
                }
            }
            else if (encoding == "gbk")
            {
                score += profile.cjk * 0.4;
                score += profile.chinesePunct * 1.5;
                score -= (profile.hiragana + profile.katakana) * 1.6;
                score -= profile.halfwidthKatakana * 1.5;
            }

            return score;
        }

    } // namespace

    DecodeMode ParseDecodeMode(const std::string &value)
    {
        const std::string normalized = NormalizeEncodingName(value);
        if (normalized == "shift_jis")
        {
            return DecodeMode::ShiftJis;
        }
        if (normalized == "gbk")
        {
            return DecodeMode::Gbk;
        }
        return DecodeMode::Auto;
    }

    DecodedString DecodeScriptBytes(const ByteBuffer &rawBytes, DecodeMode mode)
    {
        auto makeCandidate = [&](unsigned int codePage, std::string encodingName, bool strict) -> std::optional<DecodedString>
        {
            std::wstring decoded = DecodeBytesToWide(rawBytes, codePage, strict);
            if (strict && decoded.empty() && !rawBytes.empty())
            {
                return std::nullopt;
            }
            DecodedString candidate;
            candidate.textWide = std::move(decoded);
            candidate.textUtf8 = WideToUtf8(candidate.textWide);
            candidate.encoding = std::move(encodingName);
            candidate.score = ScoreDecodedText(candidate.textWide, candidate.encoding);
            return candidate;
        };

        if (mode == DecodeMode::ShiftJis)
        {
            auto candidate = makeCandidate(932, "shift_jis", false);
            if (!candidate)
            {
                throw std::runtime_error("Failed to decode Shift-JIS string.");
            }
            return *candidate;
        }
        if (mode == DecodeMode::Gbk)
        {
            auto candidate = makeCandidate(936, "gbk", false);
            if (!candidate)
            {
                throw std::runtime_error("Failed to decode GBK string.");
            }
            return *candidate;
        }

        std::vector<DecodedString> candidates;
        if (auto sjis = makeCandidate(932, "shift_jis", true))
        {
            candidates.push_back(*sjis);
        }
        if (auto gbk = makeCandidate(936, "gbk", true))
        {
            candidates.push_back(*gbk);
        }

        if (candidates.empty())
        {
            auto fallback = makeCandidate(932, "shift_jis", false);
            if (!fallback)
            {
                throw std::runtime_error("Failed to decode script bytes.");
            }
            return *fallback;
        }
        if (candidates.size() == 1)
        {
            return candidates.front();
        }

        const DecodedString *shiftJisCandidate = nullptr;
        const DecodedString *gbkCandidate = nullptr;
        for (const auto &candidate : candidates)
        {
            if (candidate.encoding == "shift_jis")
            {
                shiftJisCandidate = &candidate;
            }
            else if (candidate.encoding == "gbk")
            {
                gbkCandidate = &candidate;
            }
        }

        if (shiftJisCandidate && gbkCandidate)
        {
            const TextProfile shiftProfile = BuildTextProfile(shiftJisCandidate->textWide);
            const TextProfile gbkProfile = BuildTextProfile(gbkCandidate->textWide);

            if (shiftProfile.hiragana + shiftProfile.katakana > 0)
            {
                return *shiftJisCandidate;
            }

            const int shiftPunctSignal = shiftProfile.japanesePunct + shiftProfile.fullwidthAscii + shiftProfile.punctuation;
            if (shiftPunctSignal >= 3 && gbkProfile.chinesePunct == 0 && gbkProfile.hiragana + gbkProfile.katakana == 0)
            {
                return *shiftJisCandidate;
            }

            if (gbkProfile.chinesePunct > 0 && shiftProfile.halfwidthKatakana > 0)
            {
                return *gbkCandidate;
            }
        }

        std::sort(candidates.begin(), candidates.end(), [](const DecodedString &left, const DecodedString &right)
                  { return left.score > right.score; });

        if (std::abs(candidates[0].score - candidates[1].score) < 1.5)
        {
            for (const auto &candidate : candidates)
            {
                if (candidate.encoding == "shift_jis")
                {
                    return candidate;
                }
            }
        }

        return candidates.front();
    }

    bool IsDialogueCandidate(const std::string &text)
    {
        if (text.empty())
        {
            return false;
        }
        if (StartsWithAsciiInsensitive(text, "voice/") || StartsWithAsciiInsensitive(text, "bgm/") || StartsWithAsciiInsensitive(text, "se/"))
        {
            return false;
        }
        return std::any_of(text.begin(), text.end(), [](unsigned char ch)
                           { return ch > 127; });
    }

    std::string ClassifyTextKind(const std::string &text, const std::optional<int> &voiceId, bool isTitle)
    {
        if (isTitle)
        {
            return "title";
        }
        if (StartsWithAsciiInsensitive(text, "voice/"))
        {
            return "resource_ref_voice";
        }
        if (StartsWithAsciiInsensitive(text, "bgm/"))
        {
            return "resource_ref_bgm";
        }
        if (StartsWithAsciiInsensitive(text, "se/"))
        {
            return "resource_ref_se";
        }
        if (IsDialogueCandidate(text))
        {
            return voiceId.has_value() ? "dialogue" : "narration_or_ui";
        }
        if (text.empty())
        {
            return "empty";
        }
        return "system";
    }

} // namespace packcpp
