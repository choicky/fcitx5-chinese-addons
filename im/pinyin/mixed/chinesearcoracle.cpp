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

#include <libime/core/lattice.h>
#include <libime/core/segmentgraph.h>
#include <libime/core/userlanguagemodel.h>

#include <libime/pinyin/pinyincontext.h>
#include <libime/pinyin/pinyincorrectionprofile.h>
#include <libime/pinyin/pinyindecoder.h>
#include <libime/pinyin/pinyinencoder.h>
#include <libime/pinyin/pinyinime.h>
#include <libime/pinyin/pinyinmatchstate.h>
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

// Phase 3A-1 Option B constants: word arcs cover spans of up to four
// syllables (打开/我想买/配件 and idiom-length words; deeper spans cost
// hundreds of sub-decodes per keystroke for no observed recall gain), and
// each span emits at most this many LM decodes as parallel arcs.
constexpr size_t kWordDecodeMaxSyllables = 4;
constexpr size_t kWordDecodeNbest = 4;

// Unique byte endpoints reachable from `begin` by 1..maxSyllables
// consecutive graph edges. Every edge produced by `parseUserPinyin` /
// `parseUserShuangpin` is exactly one syllable (Phase 3A-1 Part 1 source
// trace), so a span tiled by k edges is a k-syllable span. BFS per depth
// keeps shorter spans first.
std::vector<size_t>
reachableSpanEnds(const std::vector<std::vector<size_t>> &adj, size_t begin,
                  size_t limit, size_t maxSyllables) {
    std::vector<size_t> out;
    std::set<size_t> seen;
    std::vector<size_t> frontier{begin};
    for (size_t depth = 0; depth < maxSyllables && !frontier.empty(); ++depth) {
        std::vector<size_t> next;
        for (const size_t pos : frontier) {
            if (pos >= adj.size()) {
                continue;
            }
            for (const size_t end : adj[pos]) {
                if (end > limit) {
                    break;
                }
                if (seen.insert(end).second) {
                    next.push_back(end);
                }
            }
        }
        out.insert(out.end(), next.begin(), next.end());
        frontier = std::move(next);
    }
    return out;
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
    // Phase 3A-1 Revision B1: classical span decode for LM word arcs.
    // `wordIME` is borrowed from PinyinEngine (same ownership pattern as
    // `correction`); null disables word arcs entirely, leaving behavior
    // bit-identical to the pre-fix oracle. The private input-less
    // `PinyinContext` plus `PinyinMatchState` exist because LibIME's
    // dictionary matching reads fuzzy/shuangpin/correction semantics ONLY
    // from a PinyinMatchState helper: `PinyinMatchContext`
    // (pinyindictionary.cpp:303-325) copies `fuzzyFlags()`,
    // `shuangpinProfile()` and `correctionProfile()` from the helper, and
    // the helper-less `Decoder::decode` overload leaves them at
    // None/nullptr, which made span decodes syllabify Shuangpin edges as
    // Pinyin. This reuses the classical path's own objects; no second
    // decoder or scorer, no LibIME modification.
    libime::PinyinIME *wordIME = nullptr;
    std::unique_ptr<libime::PinyinContext> wordContext;
    std::unique_ptr<libime::PinyinMatchState> wordMatchState;
    // Per-begin-position cache of word arcs, rebuilt whenever the graphs
    // are invalidated (setRaw and every setter that clears them).
    mutable std::vector<std::vector<SegmentationArc>> wordArcs;
    mutable std::vector<char> wordArcsComputed;
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
    d_->wordArcs.clear();
    d_->wordArcsComputed.clear();
}

void LibIMEChineseArcOracle::setMode(ChineseInputMode mode) {
    if (d_->mode == mode) {
        return;
    }
    d_->mode = mode;
    // The word-decode context mirrors the mode: PinyinMatchState reports
    // shuangpinProfile() only while its context has useShuangpin enabled,
    // which is what makes dictionary matching syllabify edges with the
    // Shuangpin table instead of Pinyin.
    if (d_->wordContext) {
        d_->wordContext->setUseShuangpin(mode == ChineseInputMode::Shuangpin);
    }
    // The cached graph was parsed under the previous mode; a Shuangpin graph
    // and a Pinyin graph are different segmentations of the same raw string,
    // so it must not be reused across the switch.
    d_->graph.reset();
    d_->baseGraph.reset();
    d_->endsFromBegin.clear();
    d_->baseEndsFromBegin.clear();
    d_->lastRaw.clear();
    d_->wordArcs.clear();
    d_->wordArcsComputed.clear();
}

void LibIMEChineseArcOracle::setFuzzyFlags(libime::PinyinFuzzyFlags flags) {
    d_->flags = flags;
    d_->graph.reset();
    d_->baseGraph.reset();
    d_->endsFromBegin.clear();
    d_->baseEndsFromBegin.clear();
    d_->lastRaw.clear();
    d_->wordArcs.clear();
    d_->wordArcsComputed.clear();
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
    d_->wordArcs.clear();
    d_->wordArcsComputed.clear();
}

bool LibIMEChineseArcOracle::correctionEnabled() const {
    return d_->mode == ChineseInputMode::Pinyin && d_->correction != nullptr;
}

void LibIMEChineseArcOracle::setWordDecoder(libime::PinyinIME *ime) {
    if (d_->wordIME == ime && d_->wordMatchState) {
        return;
    }
    d_->wordIME = ime;
    d_->wordContext.reset();
    d_->wordMatchState.reset();
    if (ime != nullptr) {
        // Input-less context: never receives user text, only acts as the
        // parameter carrier LibIME's decoder requires (fuzzy flags,
        // shuangpin profile, correction profile all flow through its
        // PinyinMatchState).
        d_->wordContext = std::make_unique<libime::PinyinContext>(ime);
        d_->wordContext->setUseShuangpin(d_->mode ==
                                         ChineseInputMode::Shuangpin);
        d_->wordMatchState =
            std::make_unique<libime::PinyinMatchState>(d_->wordContext.get());
    }
    // Cached word arcs were produced under the previous decode parameters.
    d_->wordArcs.clear();
    d_->wordArcsComputed.clear();
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
    d_->wordArcs.clear();
    d_->wordArcsComputed.clear();
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
        d_->wordArcs.clear();
        d_->wordArcsComputed.clear();
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
    // Phase 3A-1 Revision B1: additionally emit LM word arcs produced by the
    // classical span decode, on top of the structural per-syllable arcs
    // above. With no IME plumbed (standalone oracle tests, corpus test
    // doubles, or graceful degradation) the output is bit-identical to the
    // pre-fix oracle.
    if (d_->wordIME != nullptr && d_->wordMatchState != nullptr) {
        if (d_->wordArcs.empty()) {
            d_->wordArcs.resize(d_->lastRaw.size() + 1);
            d_->wordArcsComputed.assign(d_->lastRaw.size() + 1, 0);
        }
        if (begin < d_->wordArcsComputed.size() &&
            !d_->wordArcsComputed[begin]) {
            d_->wordArcsComputed[begin] = 1;
            auto &cache = d_->wordArcs[begin];
            const bool wordCorrection = d_->mode == ChineseInputMode::Pinyin &&
                                        d_->correction != nullptr;
            if (d_->mode != ChineseInputMode::Shuangpin || d_->sp != nullptr) {
                const auto spanEnds = reachableSpanEnds(
                    d_->endsFromBegin, begin, limit, kWordDecodeMaxSyllables);
                for (const auto end : spanEnds) {
                    // PinyinEncoder parse entry points take std::string by
                    // value; the span is copied once per decode.
                    const std::string span(raw.substr(begin, end - begin));
                    try {
                        std::unique_ptr<libime::SegmentGraph> spanGraph;
                        if (d_->mode == ChineseInputMode::Shuangpin) {
                            spanGraph = std::make_unique<libime::SegmentGraph>(
                                libime::PinyinEncoder::parseUserShuangpin(
                                    span, *d_->sp, d_->flags));
                        } else if (wordCorrection) {
                            spanGraph = std::make_unique<libime::SegmentGraph>(
                                libime::PinyinEncoder::parseUserPinyin(
                                    span, d_->correction, d_->flags));
                        } else {
                            spanGraph = std::make_unique<libime::SegmentGraph>(
                                libime::PinyinEncoder::parseUserPinyin(
                                    span, d_->flags));
                        }
                        libime::Lattice lattice;
                        // The match-state caches (matchedPaths, discarded
                        // nodes) are keyed by SegmentGraphNode pointers of
                        // the throwaway span graph; never let entries
                        // outlive it.
                        d_->wordMatchState->clear();
                        const bool decoded = d_->wordIME->decoder()->decode(
                            lattice, *spanGraph, kWordDecodeNbest,
                            d_->wordIME->model()->nullState(),
                            d_->wordIME->maxDistance(), d_->wordIME->minPath(),
                            d_->wordIME->beamSize(), d_->wordIME->frameSize(),
                            d_->wordMatchState.get());
                        if (!decoded) {
                            continue;
                        }
                        for (size_t i = 0; i < lattice.sentenceSize(); ++i) {
                            const auto &result = lattice.sentence(i);
                            if (result.sentence().empty()) {
                                continue;
                            }
                            const std::string text = result.toString();
                            // Pure-Han gate: a decode spilling a Latin
                            // residue is a forced-fragment artefact, not a
                            // Chinese reading; never emit it as a word arc.
                            if (std::any_of(text.begin(), text.end(),
                                            [](unsigned char ch) {
                                                return ch < 0x80;
                                            })) {
                                continue;
                            }
                            bool viaCorrection = false;
                            if (wordCorrection) {
                                for (const auto *node : result.sentence()) {
                                    if (node->as<libime::PinyinLatticeNode>()
                                            .anyCorrectionOnPath()) {
                                        viaCorrection = true;
                                        break;
                                    }
                                }
                            }
                            SegmentationArc arc;
                            arc.rawBegin = begin;
                            arc.rawEnd = end;
                            arc.source = SegmentSource::Chinese;
                            arc.provenance =
                                viaCorrection ? CandidateProvenance::Correction
                                              : CandidateProvenance::Exact;
                            arc.confidence = viaCorrection ? 0.55F : 0.75F;
                            arc.boundaryConfidence =
                                viaCorrection ? 0.85F : 1.0F;
                            arc.sourceLocalRank = static_cast<int>(i);
                            arc.resolvedOutput = text;
                            cache.push_back(std::move(arc));
                        }
                    } catch (...) {
                        // Parser/decode disagreement on this span drops
                        // only its word arcs; the structural arcs above
                        // remain.
                    }
                }
            }
        }
        if (begin < d_->wordArcs.size()) {
            out.insert(out.end(), d_->wordArcs[begin].begin(),
                       d_->wordArcs[begin].end());
        }
    }
    return out;
}

} // namespace pinyin
