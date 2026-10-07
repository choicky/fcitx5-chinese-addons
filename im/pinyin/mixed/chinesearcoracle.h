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
class PinyinIME;
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
// decoder and, when a word decoder is configured (see `setWordDecoder`), is
// emitted as additional LM-decoded word arcs; otherwise it is expressed
// through the top-N Han decodes the resolver emits.
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

    // Switch between the Pinyin and Shuangpin parse at runtime. The active
    // input mode is a property of the input method entry ("pinyin" vs
    // "shuangpin"), not of the add-on config, so the fusion seam calls this
    // before every `setRaw`. Any cached graph is invalidated on change;
    // calling with the current mode is a no-op.
    void setMode(ChineseInputMode mode);

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

    // Phase 3A-1 (fix design Revision B1): enable LM word arcs. When the
    // IME is configured (the fusion seam passes LibIME's own `PinyinIME`,
    // borrowed — PinyinEngine owns it and outlives the per-InputContext
    // state, same pattern as `setCorrectionProfile`), `arcsAt` ADDITIONALLY
    // emits one arc per classical span decode over every span reachable from
    // `begin` by up to four graph edges: the span substring is parsed with
    // the SAME `parseUserPinyin` / `parseUserShuangpin` call `setRaw` uses
    // and decoded with the public `Decoder::decode` — the exact classical
    // call shape (pinyincontext.cpp). The IME (not just its decoder and
    // model) is required because LibIME's dictionary matching reads
    // fuzzy/shuangpin/correction semantics ONLY from a `PinyinMatchState`
    // helper (PinyinMatchContext, pinyindictionary.cpp:303-325): the
    // helper-less overload syllabifies Shuangpin edges as Pinyin. The oracle
    // owns an input-less `PinyinContext` and builds that helper over it, so
    // the span decode uses the identical IME decoder, model, language-model
    // null state and decode parameters the classical path reads. Each
    // returned sentence becomes an arc carrying `resolvedOutput` = the
    // sentence text (pure-Han bytes only), `sourceLocalRank` = the sentence
    // index, and provenance `CandidateProvenance::Correction` when any node
    // reports `anyCorrectionOnPath()` and a correction profile is active
    // (Pinyin mode), else Exact. This is purely additive: the structural
    // per-syllable arcs above are still emitted unchanged, and with a null
    // IME the class behaves bit-identically to before. The fix targets the
    // source-traced root cause — Han text generation was a context-free
    // single-syllable dictionary-table pick that never consulted the
    // classical decoder, so multi-syllable words (打开/我想买/配件) were
    // structurally unreachable and Shuangpin spans were misdecoded as
    // Pinyin. No LibIME modification: only public API.
    void setWordDecoder(libime::PinyinIME *ime);

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
