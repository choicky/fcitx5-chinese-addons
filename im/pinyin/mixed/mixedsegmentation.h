/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _FCITX_PINYIN_MIXED_MIXEDSEGMENTATION_H_
#define _FCITX_PINYIN_MIXED_MIXEDSEGMENTATION_H_

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "mixedcompositionstate.h"

namespace pinyin {

// One candidate decoding span over the raw stream [rawBegin, rawEnd). `source`
// records which candidate source produced it; `confidence` is a source-local,
// normalized quality in [0, 1] (LibIME LM scores and English lexical scores are
// NOT directly comparable and are bucketed by the producing oracle).
// `boundaryConfidence` is a source-local judgement of how strong the
// split/continuation at the arc's right edge is, in [0, 1].
struct SegmentationArc {
    size_t rawBegin = 0;
    size_t rawEnd = 0;
    SegmentSource source = SegmentSource::Chinese;
    CandidateProvenance provenance = CandidateProvenance::Exact;
    float confidence = 0.0F;
    float boundaryConfidence = 1.0F;
    // Source-local rank (0-based) within the producing source for this span.
    int sourceLocalRank = 0;
    // Concrete decoded surface for this arc, filled in by the producing
    // oracle. English arcs carry the display word (canonical form);
    // Chinese arcs carry one Han decoding from the parser-backed oracle.
    // Empty for arcs where the decoding is only determined later against
    // the whole composition (the top-K per-source decodes are emitted as
    // parallel arcs with distinct sourceLocalRank values, so composition
    // itself stays context-free).
    std::string resolvedOutput;

    size_t rawLength() const { return rawEnd - rawBegin; }
};

// Supplies valid Chinese arcs for a raw span, using the real Pinyin/Shuangpin
// parser capability (LibIME PinyinEncoder::parseUserPinyin /
// parseUserShuangpin) rather than a duplicated hand-maintained syllable table.
// The production implementation lives behind the LibIME boundary; a test double
// exercises the search algorithm independently of LibIME.
class IChineseArcOracle {
public:
    virtual ~IChineseArcOracle() = default;
    // Return valid Chinese arcs starting exactly at `begin`, each spanning at
    // most [begin, begin + maxSpan) bytes. Must return only parser-supported
    // spans.
    virtual std::vector<SegmentationArc>
    arcsAt(std::string_view raw, size_t begin, size_t maxSpan) const = 0;
};

// Supplies English arcs (exact / canonical / completion / correction / proper /
// technical / user) for a raw span. A dictionary hit is evidence, not a hard
// language verdict.
class IEnglishArcOracle {
public:
    virtual ~IEnglishArcOracle() = default;
    virtual std::vector<SegmentationArc>
    arcsAt(std::string_view raw, size_t begin, size_t maxSpan) const = 0;
};

// Members kept per English shape. The product contract (D071) makes the
// canonical respelling and its literal typed surface two distinct readings that
// must coexist, so two per shape is the terminal's sibling quota; M2+r4 reuses
// the same number as its frontier quota S so the two shape protections cannot
// drift apart.
inline constexpr size_t kTerminalSiblingsPerShape = 2;

// A complete segmentation of the raw stream with its accumulated cost.
struct SegmentationPath {
    std::vector<SegmentationArc> arcs;
    double cost = 0.0;
};

// Bounded, deterministic constrained top-K search over the raw-position DAG.
// NOT a language classifier: Chinese and English arcs may overlap at the same
// span and compete purely on evidence. The cost function penalizes weak
// boundaries and unsupported/pathological fragmentation, but never the segment
// count itself, so evidence-backed multiple C/E switches remain competitive.
//
// Retention (Phase 3A-2 diversity contract):
// each frontier's window is partitioned into English-segment retention classes
// (number of maximal consecutive-English runs on the prefix, capped at
// Config::englishSegmentCap, with a last-source bit below the cap) and each
// live class independently keeps its cheapest beamWidth prefixes. A prefix may
// prune another only when both share frontier, capped run count and (below the
// cap) last arc source — the smallest equivalence under which additive cost is
// genuine dominance for every continuation. Terminal top-K reserves the head
// of the highest non-empty English class, then fills by cost with zero-segment
// paths last (placement consumes only English-bearing pool members, so this
// never removes anything the product path could use).
class MixedSegmentationSearch {
public:
    struct Config {
        size_t beamWidth = 8;   // internal; NOT a user-exposed knob
        size_t topK = 8;        // returned paths; preserve direct English arcs
        size_t maxArcSpan = 24; // max raw bytes per arc (bounded lookahead)
        float weakBoundaryWeight = 0.5F;
        // Cmax: maximum distinct English-segment retention classes, derived
        // from the committed multi-switch product contract (=2: RANKCORP C
        // requires GitHub + phone on one path). Prefixes at or above the cap
        // share one collapsed class where the last-source bit can no longer
        // change the capped class trajectory. 0 restores the pre-diversity
        // single cost-sorted window. Internal; NOT a user-exposed knob.
        size_t englishSegmentCap = 2;
    };

    MixedSegmentationSearch();
    explicit MixedSegmentationSearch(Config config);

    // Search the whole raw range [0, raw.size()). Convenience over
    // searchFrom when there is no frozen prefix.
    std::vector<SegmentationPath>
    search(std::string_view raw, const IChineseArcOracle &chinese,
           const IEnglishArcOracle &english) const;

    // Search the uncommitted suffix [begin, raw.size()) so a frozen selected
    // prefix can be skipped. Bounded by beam width; linear in the suffix
    // length, never exponential.
    std::vector<SegmentationPath>
    searchFrom(std::string_view raw, size_t begin,
               const IChineseArcOracle &chinese,
               const IEnglishArcOracle &english) const;

    const Config &config() const { return config_; }

    // The mixed-search cost of one arc, in the units every cross-source
    // comparison of this pipeline uses (§19). Exposed so the M2+ LM-state
    // search prices arcs in the same units instead of re-deriving the formula.
    static double arcCostOf(const SegmentationArc &arc,
                            float weakBoundaryWeight);

    // Terminal window and top-K selection: per-class retention of `beamWidth`
    // arrivals, the reachability reservation of the head of the highest
    // non-empty English class, then the cost-ordered fill with zero-segment
    // paths last. Shared with the M2+ LM-state search so both consumers return
    // the same calibrated survivor set; `arrivals` must already be in the
    // caller's preferred order, since every sort here is stable.
    //
    // `reserveEnglishShapes` (M2+) selects the terminal window by English
    // span shape instead of by retention class: keeping Chinese readings
    // unfrozen inside the M2+ search (architecture item 6) means every reading
    // of one frame shares an identical arc cost, and a cost-only window would
    // keep B Chinese variants of ONE English hypothesis and evict all others
    // before compose. The shape window keeps the variants inside their
    // hypothesis and reserves one slot per shape; it moves no arc cost and
    // scores nothing.
    static std::vector<SegmentationPath>
    selectTerminal(std::vector<SegmentationPath> arrivals, size_t cap,
                   size_t beamWidth, size_t topK,
                   bool reserveEnglishShapes = false);

private:
    double arcCost(const SegmentationArc &arc) const;

    Config config_;
};

} // namespace pinyin

#endif // _FCITX_PINYIN_MIXED_MIXEDSEGMENTATION_H_
