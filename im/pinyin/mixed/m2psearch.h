/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _FCITX_PINYIN_MIXED_M2PSEARCH_H_
#define _FCITX_PINYIN_MIXED_M2PSEARCH_H_

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <libime/core/languagemodel.h>
#include <libime/core/userlanguagemodel.h>
#include <libime/pinyin/pinyindictionary.h>

#include "chinesearcoracle.h"
#include "chinesewordarcs.h"
#include "mixedengine.h"
#include "mixedsegmentation.h"

namespace libime {
class PinyinContext;
class PinyinIME;
class PinyinMatchState;
} // namespace libime

namespace pinyin {

// Terminology: "A-prime" is the superseded per-gap Chinese decoding design —
// one classical reading per typed gap — that this whole-raw search replaces.
// It no longer exists in this tree; the comments name it only to explain why
// the shapes and bounds below are what they are.

// Retention beam B: the number of states retained per raw position per
// retention class. D075-validated r4 baseline.
inline constexpr size_t M2P_DEFAULT_RETENTION_BEAM = 4;

// Section 2 item 4's other half: LibIME-style per-(from,to) frame pruning.
// LibIME keeps every dictionary match of a frame that starts at the graph start
// and caps the rest at frameSize (=40) by score
// (src/libime/core/decoder.cpp:114-155); A-prime inherited that exemption and
// then emitted one reading per gap, so the leading frame's 100+ Ziranma
// readings never reached the mixed search. A whole-raw table has no gap
// emission to drop them, so M2+ applies the frame prune to every frame with its
// own bound: the readings kept are the highest-scoring ones, in the collector's
// total order.
inline constexpr size_t M2P_MAX_READINGS_PER_FRAME = 8;

// The same per-frame prune applied to the *path states*: LibIME's decoder keeps
// one best parent per (from,to) frame edge (src/libime/core/decoder.cpp:200-243
// picks a single `maxScore` parent for each node), and item 4 lists that prune
// as part of the retention design. Without it the B cheapest states of a
// retention class at one position are B readings of ONE frame, and the frame
// that alone could open the next English span is pruned before it is ever
// expanded.
inline constexpr size_t M2P_MAX_STATES_PER_FRAME = 2;

// Frame-prune ceilings for the per-(from,to) reading bound and the per-(from,
// to) state prune above. D075-validated r4 baseline; the values come from the
// M2P_MAX_* constants.
size_t m2pMaxReadingsPerFrame();
size_t m2pMaxStatesPerFrame();

// The search ranks paths by
//   total = cost + lmLambda * (−lmScore + lmKappa * englishBytes)
// so the cross-word LM cost competes with the structural cost inside one
// search, instead of only breaking its ties. lmScore is the accumulated LibIME
// log-prob (log10, so −lmScore is the LM cost in the same units); an English
// arc carries no LM word at all, so lmKappa prices it per raw byte, otherwise
// English would be free in LM terms. Both weights are the D075-validated r4
// baseline.
inline constexpr double M2P_DEFAULT_LM_LAMBDA = 0.025;
inline constexpr double M2P_DEFAULT_ENGLISH_KAPPA = 0.8;

// M2+ is the product mixed search path (D075). The predicate exists so the
// fusion seam can name the selection point; A-prime is not a product path.
bool mixedM2PEnabled();

// M2+r arc-source variants (see ChineseWordArcTable::gapWordArcs). r4 builds
// on r3 (the mode-scoped rule) and supersedes the earlier r1/r2 experiment
// variants, which are not product code; the product enables r3.
bool mixedM2PR3Enabled();

// The r3 rule applies only in shuangpin; in pinyin it is inert.
bool mixedM2PR3ActiveFor(ChineseInputMode mode);

// M2+r4 English-shape-aware bounded frontier retention. Product path. The
// r3 window runs unchanged (stateLess order, existing frameOf bound, keep B);
// then a guarantee pass groups the class's generated states by English-shape
// signature, orders the shapes by their best member and, for the first K=B
// shapes, tops the window up to min(S, members) members of each shape it
// under-represented, with S = kTerminalSiblingsPerShape. The retained set is
// a superset of r3's: nothing r3 kept is dropped and no score, cost, class
// or the terminal selection changes. Class 0 (no English) holds only the
// empty shape, which the guarantee pass skips by construction, so
// pure-Chinese keys are untouched.
bool mixedM2PR4Enabled();

// M2+r4 input: the one whole-raw Chinese word-arc table plus the LM the search
// walks. The table owns every WordNode an LM State can point to (LibIME's
// UserLanguageModel::score stores `&word` into the outgoing state,
// src/libime/core/userlanguagemodel.cpp:138), so the nodes live in a deque with
// the table's lifetime and never on the stack.
class ChineseWordArcTable final {
public:
    explicit ChineseWordArcTable(libime::PinyinIME *ime);
    ~ChineseWordArcTable();

    void setMode(ChineseInputMode mode);
    ChineseInputMode mode() const { return mode_; }

    // One parseChineseGraph + one collectChineseWordArcs pass over the whole
    // raw. Rebuilt per call: the table is per-key working state, not a
    // cross-keystroke cache.
    bool build(std::string_view raw);
    std::string_view raw() const { return raw_; }

    const std::vector<ChineseWordArc> &arcs() const { return arcs_; }
    // Stable WordNode for arcs()[i], for LanguageModel::score.
    const libime::WordNode &node(size_t i) const { return nodes_[i]; }
    const libime::WordNode &eos() const { return *eos_; }
    const libime::LanguageModelBase *model() const { return model_; }

    // M2+r: the same word-arc builder as build() (parse, matchPrefix,
    // pure-Han guard, per-frame reading bound) applied to raw()[b, s) alone,
    // offsets mapped back to the whole raw. Cached per (b, s) until the next
    // build().
    const std::vector<ChineseWordArc> &gapWordArcs(size_t b, size_t s) const;
    // Stable WordNode for gapWordArcs(b, s)[i] (built by gapWordArcs).
    const libime::WordNode &gapNode(size_t b, size_t s, size_t i) const {
        return gapTables_.at({b, s}).words[i];
    }
    // Whole-raw positions at which the whole-raw arc set has at least one arc
    // *starting*, before the pure-Han guard and the per-frame reading bound.
    // The r3 suffix re-parse rule reads exactly this set; size is
    // raw().size() + 1.
    const std::vector<char> &wholeRawArcStarts() const {
        return wholeArcStarts_;
    }

private:
    libime::PinyinIME *ime_ = nullptr;
    const libime::LanguageModelBase *model_ = nullptr;
    ChineseInputMode mode_ = ChineseInputMode::Pinyin;
    std::string raw_;
    std::vector<ChineseWordArc> arcs_;
    // One node per arc, in stable storage: an LM State produced by
    // LanguageModel::score keeps a pointer to the WordNode it was scored with.
    std::deque<libime::WordNode> nodes_;
    std::unique_ptr<libime::WordNode> eos_;
    // LibIME's dictionary matching reads fuzzy/shuangpin/correction semantics
    // only through a PinyinMatchState built over a PinyinContext
    // (pinyindictionary.cpp:303-325); the context is input-less, as in A-prime.
    std::unique_ptr<libime::PinyinContext> context_;
    std::unique_ptr<libime::PinyinMatchState> matchState_;
    std::vector<char> wholeArcStarts_;
    struct GapTable {
        std::vector<ChineseWordArc> arcs;
        std::deque<libime::WordNode> words;
    };
    mutable std::map<std::pair<size_t, size_t>, GapTable> gapTables_;
};

// M2+ LM-state-aware single-pass bounded mixed hypothesis search.
//
// One bounded search per key over the whole raw: the Chinese word arcs of a
// ChineseWordArcTable and the admitted English arcs of an IEnglishArcOracle are
// edges of one graph. A path state carries cost, the LM State, the last source,
// the English run count and back-pointers, so a Chinese word after an English
// span is scored with the LM state the mixed path actually reached — which is
// what A-prime's separate per-gap Viterbi could not do without a decoder
// fan-out per English span.
class MixedLmStateSearch final : public MixedLmStateSearchHook {
public:
    // The LM state after an English arc (and before the next Chinese word).
    // This is the single centralized English-state policy boundary of the
    // mixed search; call sites never reimplement it. `Reset` (the
    // D075-validated r4 baseline) seeds the next Chinese word from
    // nullState. `Preserve` (carries the state from before the English arc)
    // is an alternate policy the future Accepted Decision can select without
    // rewriting the search; it is currently UNVERIFIED and not enabled in
    // product.
    enum class EnglishStatePolicy {
        Reset,
        Preserve,
    };

    struct Config {
        // B: states retained per position per retention class.
        size_t retentionBeam = M2P_DEFAULT_RETENTION_BEAM;
        size_t englishSegmentCap = 2;
        float weakBoundaryWeight = 0.5F;
        // Sort key weights (see M2P_DEFAULT_LM_LAMBDA / KAPPA docs).
        double lmLambda = M2P_DEFAULT_LM_LAMBDA;
        double englishKappa = M2P_DEFAULT_ENGLISH_KAPPA;
        EnglishStatePolicy englishStatePolicy = EnglishStatePolicy::Reset;
    };

    MixedLmStateSearch(Config config, const libime::LanguageModelBase *model,
                       const ChineseWordArcTable &chinese,
                       const libime::State &seed);

    // MixedLmStateSearchHook. Returns the top-K complete paths, arcs in raw
    // order, with every maximal run of Chinese words merged into a single
    // Chinese arc — the arc shape and cost units A-prime used when it turned a
    // gap reading into a mixed-search arc — so composer, ranker and placement
    // consume M2+ output unchanged.
    std::vector<SegmentationPath> search(std::string_view raw,
                                         const IEnglishArcOracle &english,
                                         size_t topK) const override;

    // The LM state after traversing an English arc (section 2 item 5).
    libime::State transitionAcrossEnglish(const libime::State &before) const;

    const Config &config() const { return config_; }

private:
    Config config_;
    const libime::LanguageModelBase *model_ = nullptr;
    const ChineseWordArcTable *chinese_ = nullptr;
    libime::State seed_;
};

// The product config: D075-validated r4 baseline (B, λ, κ, English state
// policy) taken from the M2P_DEFAULT_* constants above. Kept as a factory so
// the fusion seam can name the selection point.
MixedLmStateSearch::Config mixedM2PConfig();

} // namespace pinyin

#endif // _FCITX_PINYIN_MIXED_M2PSEARCH_H_
