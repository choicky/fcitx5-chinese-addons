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
class PinyinCorrectionProfile;
class ShuangpinProfile;
} // namespace libime

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
// Batch 8 correction preservation: when a `PinyinCorrectionProfile` is set
// (Pinyin mode only; Shuangpin correction is baked into the ShuangpinProfile
// key map at construction time and is not separately observable through the
// public LibIME API — recorded as a source-traced ceiling, not fabricated),
// `setRaw` builds two graphs. The `correctedGraph` includes both fuzzy and
// layout-correction hypotheses via the 3-arg `parseUserPinyin(pinyin,
// profile, flags)` overload. The `baseGraph` (profile = nullptr) includes
// only classical exact + fuzzy hypotheses. An arc present in `correctedGraph`
// but not in `baseGraph` at the same [begin, end) span is correction-derived
// and is emitted with `provenance = CandidateProvenance::Correction` and a
// lower arc-level confidence so the UnifiedRanker can express the weaker
// structural evidence through its existing explainable features
// (correctionArcs count, boundaryConfidence 0.55 per provenance).
//
// This class is not thread-safe; it is owned by the per-InputContext state.
// Calling `setRaw` after every keystroke (or after Backspace/Escape/ continued
// typing) is the intended usage and is O(raw.size()) amortised. Building two
// graphs only when a correction profile is set keeps the base classical path
// unchanged when the option is off.
class LibIMEChineseArcOracle : public IChineseArcOracle {
public:
    explicit LibIMEChineseArcOracle(ChineseInputMode mode);
    ~LibIMEChineseArcOracle() override;

    // Configure Shuangpin profile (required when mode == Shuangpin). A null
    // profile in Shuangpin mode makes setRaw clear the graph, so the engine
    // gracefully degrades instead of crashing.
    void setShuangpinProfile(const libime::ShuangpinProfile *profile);

    // Configure the fuzzy flags for the graph parse (defaults to None). These
    // are the same flags the classical LibIME decoder uses, so the structural
    // validity of Chinese arcs in the mixed path matches the classical path.
    void setFuzzyFlags(libime::PinyinFuzzyFlags flags);

    // Batch 8: enable layout-correction recall + provenance. A null profile
    // disables the second (corrected) graph and every arc is reported as
    // CandidateProvenance::Exact (unchanged classical behavior).
    //
    // The profile pointer is only observed during `setRaw`; the caller owns
    // the profile's lifetime (PinyinEngine passes `ime_->correctionProfile()
    // .get()` which outlives per-InputContext state). Passing nullptr is the
    // correct way to disable correction.
    //
    // Ignored in Shuangpin mode (ShuangpinProfile already embeds the
    // correction mapping; the public LibIME API does not expose per-arc
    // correction provenance for Shuangpin — recorded ceiling, not fabricated).
    void setCorrectionProfile(const libime::PinyinCorrectionProfile *profile);

    // Rebuild the internal graph for `raw`. Cheap; safe per keystroke.
    void setRaw(std::string_view raw);

    // The oracle's IChineseArcOracle::arcsAt signature receives a
    // std::string_view; it must match the string passed to setRaw. The
    // graph is only consulted at matching positions.
    std::vector<SegmentationArc> arcsAt(std::string_view raw, size_t begin,
                                        size_t maxSpan) const override;

    // Exposed for Batch 10 diagnostics.
    bool graphValid() const;

    // Exposed for Batch 8 diagnostics: true when a correction profile has
    // been set in Pinyin mode (so `setRaw` is producing two graphs and
    // `arcsAt` can tag correction arcs). False in Shuangpin mode (source
    // ceiling — see class comment) and when the profile pointer is null.
    bool correctionEnabled() const;

private:
    struct Private;
    std::unique_ptr<Private> d_;
};

} // namespace pinyin

#endif // _FCITX_PINYIN_MIXED_CHINESEARCORACLE_H_
