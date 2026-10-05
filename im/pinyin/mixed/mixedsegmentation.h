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
class MixedSegmentationSearch {
public:
    struct Config {
        size_t beamWidth = 8;   // internal; NOT a user-exposed knob
        size_t topK = 4;        // returned paths
        size_t maxArcSpan = 24; // max raw bytes per arc (bounded lookahead)
        float weakBoundaryWeight = 0.5F;
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

private:
    double arcCost(const SegmentationArc &arc) const;

    Config config_;
};

} // namespace pinyin

#endif // _FCITX_PINYIN_MIXED_MIXEDSEGMENTATION_H_
