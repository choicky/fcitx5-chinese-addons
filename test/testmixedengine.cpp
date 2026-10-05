/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "english/englisharcoracle.h"
#include "english/englishcorrectionoracle.h"
#include "english/englishlexicon.h"
#include "english/englishuserlexicon.h"
#include "mixedengine.h"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

using namespace pinyin;

static int failures = 0;
static void check(bool cond, const char *what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// A Chinese oracle that returns empty `resolvedOutput` on every arc. The
// engine's Han resolver callback is what actually produces the surface text
// downstream, mirroring how the real LibIME-backed oracle will behave in
// production (partial-span decodes are made against the whole composition,
// not per-arc).
struct UnresolvedChineseOracle : IChineseArcOracle {
    struct Span {
        size_t begin;
        size_t end;
        std::string han; // expected Han string for testing purposes
        float confidence = 0.85F;
    };
    std::vector<Span> spans;
    std::vector<SegmentationArc> arcsAt(std::string_view raw, size_t begin,
                                        size_t maxSpan) const override {
        std::vector<SegmentationArc> out;
        int rank = 0;
        for (const auto &s : spans) {
            if (s.begin != begin) {
                continue;
            }
            if (s.end > begin + maxSpan || s.end > raw.size()) {
                continue;
            }
            SegmentationArc arc;
            arc.rawBegin = s.begin;
            arc.rawEnd = s.end;
            arc.source = SegmentSource::Chinese;
            arc.provenance = CandidateProvenance::Exact;
            arc.confidence = s.confidence;
            arc.boundaryConfidence = 1.0F;
            arc.sourceLocalRank = rank++;
            // Intentionally empty: the resolver fills this in at compose time.
            out.push_back(arc);
        }
        return out;
    }
};

static std::vector<EnglishLexiconEntry> seed() {
    return {
        {"iphone", "iPhone", 7, true, false, false},
        {"peijian", "配件", 5, false, false, false},
        {"hello", "hello", 8, false, false, false},
        {"world", "world", 8, false, false, false},
    };
}

int main() {
    EnglishLexicon lex;
    lex.loadFromEntries(seed());
    EnglishArcOracle sysEn(&lex);
    EnglishUserLexicon userLex;
    EnglishUserArcOracle userEn(&userLex);
    EnglishCorrectionOracle corrEn(&lex);
    CompositeEnglishArcOracle composite;
    composite.addSource(&sysEn);
    composite.addSource(&userEn);
    composite.addSource(&corrEn);

    // 1. Empty input returns empty (safe, no crash).
    {
        MixedEngine engine;
        UnresolvedChineseOracle ch;
        auto out =
            engine.compute("", ch, composite, [](const SegmentationArc &) {
                return std::string();
            });
        check(out.empty(), "engine: empty raw -> empty result");
    }

    // 2. Full mixed pipeline with Han resolver: 我想买 iPhone 配件.
    {
        std::string raw = "woxiangmaiiphonepeijian"; // 23 bytes
        UnresolvedChineseOracle ch;
        ch.spans = {
            {0, 2, "我"},   {2, 7, "想"},   {7, 10, "买"},
            {16, 19, "配"}, {19, 23, "件"},
        };
        // Han resolver: map (begin,end) -> test-authored Han string.
        ArcResolver han = [&](const SegmentationArc &arc) -> std::string {
            for (const auto &s : ch.spans) {
                if (s.begin == arc.rawBegin && s.end == arc.rawEnd) {
                    return s.han;
                }
            }
            return {};
        };
        MixedEngine engine;
        auto pool = engine.compute(raw, ch, composite, han);
        check(!pool.empty(), "engine: mixed path produces candidates");
        bool foundMixed = false;
        for (const auto &c : pool) {
            if (c.composedText.find("iPhone") != std::string::npos &&
                c.composedText.find("我") != std::string::npos) {
                foundMixed = true;
                break;
            }
        }
        check(foundMixed,
              "engine: mixed Chinese+English candidate surfaces in pool");
        // Ranked in descending order.
        bool sorted = true;
        FeatureBuilder fb;
        UnifiedRanker rk;
        double prev = 1e18;
        for (const auto &c : pool) {
            double s = rk.score(fb.build(c));
            if (s > prev + 1e-6) {
                sorted = false;
            }
            prev = s;
        }
        check(sorted, "engine: pool sorted by descending final score");
    }

    // 3. planCommit emits left-to-right transactions tiling the raw range.
    {
        std::string raw = "woxiangmaiiphone"; // 14 bytes
        UnresolvedChineseOracle ch;
        ch.spans = {
            {0, 2, "我"},
            {2, 7, "想"},
            {7, 10, "买"},
        };
        ArcResolver han = [&](const SegmentationArc &arc) -> std::string {
            for (const auto &s : ch.spans) {
                if (s.begin == arc.rawBegin && s.end == arc.rawEnd) {
                    return s.han;
                }
            }
            return {};
        };
        MixedEngine engine;
        auto pool = engine.compute(raw, ch, composite, han);
        check(!pool.empty(), "planCommit: candidate exists");
        const auto &c = pool.front();
        auto txn = MixedEngine::planCommit(c);
        check(txn.size() == c.segments.size(),
              "planCommit: transaction count matches segment count");
        bool tiles = true;
        size_t cursor = 0;
        for (const auto &t : txn) {
            if (t.rawBegin != cursor) {
                tiles = false;
            }
            cursor = t.rawEnd;
        }
        check(tiles && cursor == raw.size(),
              "planCommit: transactions tile the whole raw range");
        bool anyEnglish = false;
        for (const auto &t : txn) {
            if (t.source == SegmentSource::English) {
                anyEnglish = true;
                check(!t.output.empty(),
                      "planCommit: English transaction carries output");
            }
        }
        check(anyEnglish, "planCommit: English transaction present");
    }

    // 4. applyToState freezes the candidate and advances selection frontier.
    {
        std::string raw = "iphone"; // 6 bytes; English-only path
        UnresolvedChineseOracle ch; // no arcs
        MixedEngine engine;
        ArcResolver emptyResolver = [](const SegmentationArc &) {
            return std::string();
        };
        auto pool = engine.compute(raw, ch, composite, emptyResolver);
        check(!pool.empty(), "applyToState: English-only path exists");
        MixedCompositionState state;
        state.setRawInput(raw);
        bool ok = MixedEngine::applyToState(state, pool.front());
        check(ok, "applyToState: succeeds on a valid tiling");
        check(state.isFullySelected(),
              "applyToState: selection frontier advanced to end");
        check(state.committedOutput() == pool.front().composedText,
              "applyToState: committed output matches candidate text");
    }

    // 5. applyToState rejects a candidate that does not tile the suffix.
    {
        std::string raw = "iphonehello"; // 11 bytes
        MixedCompositionState state;
        state.setRawInput(raw);
        UnifiedCandidate bogus;
        bogus.composedText = "iPhone";
        MixedSegment seg;
        seg.rawBegin = 0;
        seg.rawEnd = 6;
        seg.output = "iPhone";
        seg.source = SegmentSource::English;
        bogus.segments.push_back(seg);
        bool ok = MixedEngine::applyToState(state, bogus);
        check(!ok, "applyToState: rejects non-tiling candidate");
        check(state.selectionFrontier() == 0,
              "applyToState: state unchanged on rejection");
    }

    // 6. Pool cap is respected (maxSize).
    {
        std::string raw = "woxiangmaiiphonepeijian";
        UnresolvedChineseOracle ch;
        ch.spans = {
            {0, 2, "我"},   {2, 7, "想"},   {7, 10, "买"},
            {16, 19, "配"}, {19, 23, "件"},
        };
        ArcResolver han = [&](const SegmentationArc &arc) -> std::string {
            for (const auto &s : ch.spans) {
                if (s.begin == arc.rawBegin && s.end == arc.rawEnd) {
                    return s.han;
                }
            }
            return {};
        };
        MixedEngine::Config cfg;
        cfg.pool.maxSize = 2;
        cfg.search.topK = 4;
        MixedEngine engine(cfg);
        auto pool = engine.compute(raw, ch, composite, han);
        check(pool.size() <= 2, "engine: pool respects maxSize cap");
    }

    // 7. Composite oracle merges sources (System+User+Correction).
    {
        check(composite.sourceCount() == 3,
              "composite: three English sources registered");
        auto arcs = composite.arcsAt("iphone", 0, 24);
        bool hasExact = false;
        for (const auto &a : arcs) {
            if (a.source == SegmentSource::English &&
                (a.provenance == CandidateProvenance::Exact ||
                 a.provenance == CandidateProvenance::Canonical)) {
                hasExact = true;
            }
        }
        check(hasExact, "composite: at least one English system arc at 0");
    }

    if (failures > 0) {
        std::fprintf(stderr, "testmixedengine: %d failure(s)\n", failures);
        return 1;
    }
    std::fprintf(stderr, "testmixedengine: OK\n");
    return 0;
}
