/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _FCITX_PINYIN_MIXED_CHINESEARCORACLE_H_
#define _FCITX_PINYIN_MIXED_CHINESEARCORACLE_H_

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

#include <libime/pinyin/pinyindata.h>

#include "mixedsegmentation.h"

namespace libime {
class ShuangpinProfile;
}

namespace pinyin {

enum class ChineseInputMode {
    Pinyin,
    Shuangpin,
};

// LibIME-backed IChineseArcOracle. Builds the syllable SegmentGraph once per
// raw string via PinyinEncoder::parseUserPinyin / parseUserShuangpin (so the
// structural validity of each arc comes from the same parser the classical
// Chinese decoder uses, NOT a duplicated hand-maintained syllable table).
// Arcs are emitted with an empty `resolvedOutput`; the Han resolver at the
// fusion seam (see HanWordResolver) turns each accepted arc into a Han string
// via PinyinDictionary::matchWords. Confidence is fixed structural evidence
// from the graph edge; per-candidate LM quality lives with the classical
// decoder and is expressed through the top-N Han decodes the resolver emits.
//
// This class is not thread-safe; it is owned by the per-InputContext state.
// Calling `setRaw` after every keystroke (or after Backspace/Escape/ continued
// typing) is the intended usage and is O(raw.size()) amortised.
class LibIMEChineseArcOracle : public IChineseArcOracle {
public:
    explicit LibIMEChineseArcOracle(ChineseInputMode mode);
    ~LibIMEChineseArcOracle() override;

    // Configure Shuangpin profile (required when mode == Shuangpin). A null
    // profile in Shuangpin mode makes setRaw clear the graph, so the engine
    // gracefully degrades instead of crashing.
    void setShuangpinProfile(const libime::ShuangpinProfile *profile);

    // Configure the fuzzy flags for the graph parse (defaults to None).
    void setFuzzyFlags(libime::PinyinFuzzyFlags flags);

    // Rebuild the internal graph for `raw`. Cheap; safe per keystroke.
    void setRaw(std::string_view raw);

    // The oracle's IChineseArcOracle::arcsAt signature receives a
    // std::string_view; it must match the string passed to setRaw. The
    // graph is only consulted at matching positions.
    std::vector<SegmentationArc> arcsAt(std::string_view raw, size_t begin,
                                        size_t maxSpan) const override;

    // Exposed for Batch 10 diagnostics.
    bool graphValid() const;

private:
    struct Private;
    std::unique_ptr<Private> d_;
};

} // namespace pinyin

#endif // _FCITX_PINYIN_MIXED_CHINESEARCORACLE_H_
