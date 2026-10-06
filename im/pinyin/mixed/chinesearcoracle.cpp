/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "chinesearcoracle.h"

#include <algorithm>
#include <set>
#include <string>
#include <utility>

#include <libime/core/segmentgraph.h>
#include <libime/pinyin/pinyincorrectionprofile.h>
#include <libime/pinyin/pinyinencoder.h>
#include <libime/pinyin/shuangpinprofile.h>

namespace pinyin {

namespace {

// Collect unique (begin, end) edges from a parsed SegmentGraph. The adjacency
// representation lets `arcsAt` iterate the reachable ends at a given begin
// position in O(log) lookup. Positions beyond the raw length are ignored.
std::vector<std::vector<size_t>> buildAdjacency(const libime::SegmentGraph &g,
                                                size_t rawSize) {
    std::vector<std::vector<size_t>> adj(rawSize + 1);
    for (size_t i = 0; i <= rawSize; ++i) {
        std::set<size_t> ends;
        try {
            for (const auto &node : g.nodes(i)) {
                for (const auto &nxt : node.nexts()) {
                    if (nxt.index() > i && nxt.index() <= rawSize) {
                        ends.insert(nxt.index());
                    }
                }
            }
        } catch (...) {
            // nodes(i) may throw for positions with no graph node; ignore.
        }
        adj[i].assign(ends.begin(), ends.end());
    }
    return adj;
}

} // namespace

struct LibIMEChineseArcOracle::Private {
    ChineseInputMode mode = ChineseInputMode::Pinyin;
    const libime::ShuangpinProfile *sp = nullptr;
    const libime::PinyinCorrectionProfile *correction = nullptr;
    libime::PinyinFuzzyFlags flags = libime::PinyinFuzzyFlag::None;
    std::string lastRaw;
    std::unique_ptr<libime::SegmentGraph> graph;
    // Batch 8: when a correction profile is active in Pinyin mode, this holds
    // the *base* graph (parseUserPinyin with profile = nullptr). Arcs present
    // in `graph` but not in `baseGraph` at the same span are correction-derived
    // and tagged CandidateProvenance::Correction. When correction is inactive,
    // baseGraph is empty and every arc is reported as Exact.
    std::unique_ptr<libime::SegmentGraph> baseGraph;
    // Adjacency: for each begin position, the sorted set of end positions
    // reachable by a single graph edge (syllable). Built once per setRaw().
    std::vector<std::vector<size_t>> endsFromBegin;
    std::vector<std::vector<size_t>> baseEndsFromBegin;
};

LibIMEChineseArcOracle::LibIMEChineseArcOracle(ChineseInputMode mode)
    : d_(std::make_unique<Private>()) {
    d_->mode = mode;
}

LibIMEChineseArcOracle::~LibIMEChineseArcOracle() = default;

void LibIMEChineseArcOracle::setShuangpinProfile(
    const libime::ShuangpinProfile *profile) {
    d_->sp = profile;
    // Invalidate any cached graph; next setRaw will rebuild.
    d_->graph.reset();
    d_->baseGraph.reset();
    d_->endsFromBegin.clear();
    d_->baseEndsFromBegin.clear();
    d_->lastRaw.clear();
}

void LibIMEChineseArcOracle::setMode(ChineseInputMode mode) {
    if (d_->mode == mode) {
        return;
    }
    d_->mode = mode;
    // The cached graph was parsed under the previous mode; a Shuangpin graph
    // and a Pinyin graph are different segmentations of the same raw string,
    // so it must not be reused across the switch.
    d_->graph.reset();
    d_->baseGraph.reset();
    d_->endsFromBegin.clear();
    d_->baseEndsFromBegin.clear();
    d_->lastRaw.clear();
}

void LibIMEChineseArcOracle::setFuzzyFlags(libime::PinyinFuzzyFlags flags) {
    d_->flags = flags;
    d_->graph.reset();
    d_->baseGraph.reset();
    d_->endsFromBegin.clear();
    d_->baseEndsFromBegin.clear();
    d_->lastRaw.clear();
}

void LibIMEChineseArcOracle::setCorrectionProfile(
    const libime::PinyinCorrectionProfile *profile) {
    if (d_->correction == profile) {
        return;
    }
    d_->correction = profile;
    // Mirror the classical wiring in `PinyinEngine::populateConfig`
    // (pinyin.cpp:1290-1293): setting a correction profile also turns on the
    // `PinyinFuzzyFlag::Correction` bit; clearing the profile also clears the
    // bit. Without this invariant the profile pointer is present but
    // `PinyinEncoder::stringToSyllablesWithFuzzyFlags` /
    // `parseUserPinyin(pinyin, profile, flags)` will not return any
    // Correction-tagged interpretation (verified against LibIME 1.1.17). This
    // is a coupling the caller already observes in the classical path, so it
    // is not a new architectural decision — it just makes the mixed oracle
    // self-consistent for the fusion seam.
    if (profile != nullptr) {
        d_->flags |= libime::PinyinFuzzyFlag::Correction;
    } else {
        d_->flags &=
            ~libime::PinyinFuzzyFlags(libime::PinyinFuzzyFlag::Correction);
    }
    d_->graph.reset();
    d_->baseGraph.reset();
    d_->endsFromBegin.clear();
    d_->baseEndsFromBegin.clear();
    d_->lastRaw.clear();
}

bool LibIMEChineseArcOracle::correctionEnabled() const {
    return d_->mode == ChineseInputMode::Pinyin && d_->correction != nullptr;
}

bool LibIMEChineseArcOracle::graphValid() const { return d_->graph != nullptr; }

void LibIMEChineseArcOracle::setRaw(std::string_view raw) {
    const std::string next(raw);
    if (next == d_->lastRaw && d_->graph) {
        return;
    }
    d_->lastRaw = next;
    d_->graph.reset();
    d_->baseGraph.reset();
    d_->endsFromBegin.clear();
    d_->baseEndsFromBegin.clear();
    if (next.empty()) {
        return;
    }
    const bool correctionActive =
        d_->mode == ChineseInputMode::Pinyin && d_->correction != nullptr;
    try {
        if (d_->mode == ChineseInputMode::Shuangpin) {
            if (d_->sp == nullptr) {
                return;
            }
            // Shuangpin correction is already baked into the ShuangpinProfile
            // key map at construction time (see pinyin.cpp:1338, 1345 which
            // pass `ime_->correctionProfile()` into the ShuangpinProfile
            // constructor). The public LibIME API does not expose a separate
            // ShuangpinCorrection profile argument on `parseUserShuangpin`
            // and does not surface per-arc correction provenance. Recorded
            // ceiling; no fabricated signal.
            d_->graph = std::make_unique<libime::SegmentGraph>(
                libime::PinyinEncoder::parseUserShuangpin(next, *d_->sp,
                                                          d_->flags));
            d_->endsFromBegin = buildAdjacency(*d_->graph, next.size());
            return;
        }
        // Pinyin mode.
        if (correctionActive) {
            // Two graphs: base (no correction) vs corrected (with profile).
            // Difference is correction-only arcs. This preserves the
            // classical exact + fuzzy arcs in `baseGraph` untouched and adds
            // only what LibIME's real correction machinery can produce.
            d_->graph = std::make_unique<libime::SegmentGraph>(
                libime::PinyinEncoder::parseUserPinyin(next, d_->correction,
                                                       d_->flags));
            d_->baseGraph = std::make_unique<libime::SegmentGraph>(
                libime::PinyinEncoder::parseUserPinyin(next, nullptr,
                                                       d_->flags));
            d_->endsFromBegin = buildAdjacency(*d_->graph, next.size());
            d_->baseEndsFromBegin = buildAdjacency(*d_->baseGraph, next.size());
        } else {
            d_->graph = std::make_unique<libime::SegmentGraph>(
                libime::PinyinEncoder::parseUserPinyin(next, d_->flags));
            d_->endsFromBegin = buildAdjacency(*d_->graph, next.size());
        }
    } catch (...) {
        // Any LibIME parser error surfaces as an empty graph so the mixed
        // pipeline degrades to classical Chinese only, never crashes.
        d_->graph.reset();
        d_->baseGraph.reset();
        d_->endsFromBegin.clear();
        d_->baseEndsFromBegin.clear();
        return;
    }
}

std::vector<SegmentationArc>
LibIMEChineseArcOracle::arcsAt(std::string_view raw, size_t begin,
                               size_t maxSpan) const {
    std::vector<SegmentationArc> out;
    if (!d_->graph || begin >= raw.size()) {
        return out;
    }
    if (begin >= d_->endsFromBegin.size()) {
        return out;
    }
    // The caller is expected to pass the same raw string as setRaw(); guard
    // against a mismatch rather than silently indexing a stale adjacency.
    if (raw.size() != d_->lastRaw.size()) {
        return out;
    }
    const bool correctionActive = correctionEnabled();
    const size_t limit =
        std::min(raw.size(), begin + std::max<size_t>(1, maxSpan));
    int rank = 0;
    for (const auto end : d_->endsFromBegin[begin]) {
        if (end > limit) {
            break;
        }
        // Batch 8: for a span that the parser accepted, enumerate the
        // real (initial, final) interpretations LibIME produces for this
        // raw syllable, split by whether they require the layout
        // correction profile. Two spans that look identical positionally
        // (`nu` exact at [0,2) and `ni` via Qwerty correction at [0,2))
        // can share a graph edge; a span-only base-vs-corrected graph diff
        // would hide the correction reading entirely. Splitting by the
        // per-arc interpretation set preserves BOTH hypotheses as parallel
        // arcs with distinct `provenance` so UnifiedRanker can compete
        // them through its existing explainable features (§19 correction
        // arc count and boundary confidence), without any LibIME decoder
        // change.
        auto pushArc = [&](CandidateProvenance prov, float conf,
                           float boundary) {
            SegmentationArc arc;
            arc.rawBegin = begin;
            arc.rawEnd = end;
            arc.source = SegmentSource::Chinese;
            arc.provenance = prov;
            arc.confidence = conf;
            arc.boundaryConfidence = boundary;
            arc.sourceLocalRank = rank++;
            out.push_back(arc);
        };
        if (!correctionActive) {
            pushArc(CandidateProvenance::Exact, 0.75F, 1.0F);
            continue;
        }
        const std::string_view syllable = raw.substr(begin, end - begin);
        bool hasNonCorrection = false;
        bool hasCorrection = false;
        try {
            const auto its =
                libime::PinyinEncoder::stringToSyllablesWithFuzzyFlags(
                    syllable, d_->correction, d_->flags);
            for (const auto &initialGroup : its) {
                for (const auto &finalEntry : initialGroup.second) {
                    if (finalEntry.second.test(
                            libime::PinyinFuzzyFlag::Correction)) {
                        hasCorrection = true;
                    } else {
                        hasNonCorrection = true;
                    }
                }
            }
        } catch (...) {
            // Parser disagreement: fall through to Exact so the mixed
            // pipeline degrades to the pre-batch behavior on this arc.
            hasNonCorrection = true;
        }
        if (hasNonCorrection) {
            // Structural evidence from the classical graph edge. LM-based
            // Han quality lives behind the HanWordResolver and is expressed
            // via top-N Han picks (batch 7B fusion seam).
            pushArc(CandidateProvenance::Exact, 0.75F, 1.0F);
        }
        if (hasCorrection) {
            // Layout-correction interpretation is available for this span.
            // Lower arc confidence so the UnifiedRanker can express the
            // weaker hypothesis through its explainable features (already
            // wired at unifiedranker.cpp:45, 87 for CandidateProvenance::
            // Correction: boundary 0.55).
            pushArc(CandidateProvenance::Correction, 0.55F, 0.85F);
        }
        if (!hasNonCorrection && !hasCorrection) {
            // Corrected graph accepted this span but the interpretation
            // enumeration yielded nothing (unexpected; classical path
            // remains safe with a single Exact arc).
            pushArc(CandidateProvenance::Exact, 0.75F, 1.0F);
        }
    }
    return out;
}

} // namespace pinyin
