/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "english/englisharcoracle.h"
#include "english/englishcorrectionoracle.h"
#include "english/englishlexicon.h"
#include "english/englishuserlexicon.h"
#include "mixedcompositionstate.h"
#include "mixedsegmentation.h"
#include "rewriter.h"
#include "segmentcomposer.h"
#include "unifiedranker.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace pinyin;

static int failures = 0;
static void check(bool cond, const char *what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// Test-only Chinese oracle that returns a fixed Han-style output for the
// entire span. This is NOT a substitute for the real LibIME-backed Chinese
// oracle added in Batch 7; it exists only so Batch 6 can exercise the
// composition/pool/rank/rewrite pipeline independently of LibIME.
struct FakeChineseOracle : IChineseArcOracle {
    std::vector<std::pair<size_t, std::string>> spans; // (end, han) pairs
    std::vector<SegmentationArc> arcsAt(std::string_view raw, size_t begin,
                                        size_t maxSpan) const override {
        std::vector<SegmentationArc> out;
        for (std::size_t i = 0; i < spans.size(); ++i) {
            const auto &[end, han] = spans[i];
            if (end <= begin || end > begin + maxSpan || end > raw.size()) {
                continue;
            }
            SegmentationArc arc;
            arc.rawBegin = begin;
            arc.rawEnd = end;
            arc.source = SegmentSource::Chinese;
            arc.provenance = CandidateProvenance::Exact;
            arc.confidence = 0.85F;
            arc.boundaryConfidence = 1.0F;
            arc.sourceLocalRank = static_cast<int>(i);
            arc.resolvedOutput = han;
            out.push_back(arc);
        }
        return out;
    }
};

static std::vector<EnglishLexiconEntry> seed() {
    // Test-only data. Tier values authored here.
    return {
        {"wo", "我", 9, false, false, false},
        {"xiang", "想", 8, false, false, false},
        {"mai", "买", 7, false, false, false},
        {"peijian", "配件", 5, false, false, false},
        {"iphone", "iPhone", 7, true, false, false},
        {"hello", "hello", 8, false, false, false},
        {"help", "help", 7, false, false, false},
        {"world", "world", 8, false, false, false},
    };
}

int main() {
    EnglishLexicon lex;
    lex.loadFromEntries(seed());

    // 1. SegmentComposer: single English arc path composes to display word.
    {
        SegmentationPath path;
        SegmentationArc arc;
        arc.rawBegin = 0;
        arc.rawEnd = 6;
        arc.source = SegmentSource::English;
        arc.provenance = CandidateProvenance::Exact;
        arc.confidence = 0.9F;
        arc.resolvedOutput = "iPhone";
        path.arcs.push_back(arc);
        path.cost = 0.1;
        SegmentComposer composer;
        UnifiedCandidate c;
        check(composer.compose(path, c), "compose: single arc path ok");
        check(c.composedText == "iPhone", "compose: single arc text");
        check(c.segments.size() == 1, "compose: one segment");
        check(c.arcCount == 1, "compose: arcCount");
    }

    // 2. SegmentComposer rejects non-contiguous paths and unresolved arcs.
    {
        SegmentationPath path;
        SegmentationArc a;
        a.rawBegin = 0;
        a.rawEnd = 3;
        a.source = SegmentSource::English;
        a.resolvedOutput = "abc";
        SegmentationArc b;
        b.rawBegin = 5; // gap
        b.rawEnd = 8;
        b.source = SegmentSource::English;
        b.resolvedOutput = "def";
        path.arcs = {a, b};
        SegmentComposer composer;
        UnifiedCandidate c;
        check(!composer.compose(path, c), "compose: gap path rejected");
    }
    {
        SegmentationPath path;
        SegmentationArc a;
        a.rawBegin = 0;
        a.rawEnd = 3;
        a.resolvedOutput = ""; // unresolved
        path.arcs = {a};
        SegmentComposer composer;
        UnifiedCandidate c;
        check(!composer.compose(path, c), "compose: unresolved arc rejected");
    }

    // 3. CandidatePool dedup preserves insertion order for equal texts.
    {
        CandidatePool pool;
        UnifiedCandidate a;
        a.composedText = "hello";
        a.provenances = {CandidateProvenance::Exact};
        UnifiedCandidate b;
        b.composedText = "hello";
        b.provenances = {CandidateProvenance::Canonical};
        UnifiedCandidate c;
        c.composedText = "help";
        c.provenances = {CandidateProvenance::Exact};
        check(pool.insert(std::move(a)), "pool: first insert returns true");
        check(!pool.insert(std::move(b)), "pool: duplicate rejected");
        check(pool.insert(std::move(c)), "pool: distinct accepted");
        check(pool.size() == 2, "pool: size after inserts");
        check(pool.items()[0].provenances.front() == CandidateProvenance::Exact,
              "pool: first-insert provenance preserved");
    }

    // 4. CandidatePool evictToCap trims the tail.
    {
        CandidatePool::Config cfg;
        cfg.maxSize = 2;
        CandidatePool pool(cfg);
        for (int i = 0; i < 5; ++i) {
            UnifiedCandidate c;
            c.composedText = "w" + std::to_string(i);
            pool.insert(std::move(c));
        }
        check(pool.size() == 5, "pool: no cap during insert");
        pool.evictToCap();
        check(pool.size() == 2, "pool: evictToCap trims to maxSize");
        check(pool.items()[0].composedText == "w0" &&
                  pool.items()[1].composedText == "w1",
              "pool: evictToCap trims tail, keeps head");
    }

    // 5. FeatureBuilder extracts correct counts and switches.
    {
        SegmentationPath path;
        // wo(0,2)+xiang(2,7)+mai(7,10)+iphone(10,16)+peijian(16,23)
        struct Piece {
            size_t b, e;
            SegmentSource src;
            CandidateProvenance prov;
            float conf;
            const char *out;
        };
        const Piece pieces[] = {
            {0, 2, SegmentSource::Chinese, CandidateProvenance::Exact, 0.85F,
             "我"},
            {2, 7, SegmentSource::Chinese, CandidateProvenance::Exact, 0.85F,
             "想"},
            {7, 10, SegmentSource::Chinese, CandidateProvenance::Exact, 0.85F,
             "买"},
            {10, 16, SegmentSource::English, CandidateProvenance::Exact, 0.9F,
             "iPhone"},
            {16, 23, SegmentSource::Chinese, CandidateProvenance::Exact, 0.85F,
             "配件"},
        };
        for (const auto &p : pieces) {
            SegmentationArc a;
            a.rawBegin = p.b;
            a.rawEnd = p.e;
            a.source = p.src;
            a.provenance = p.prov;
            a.confidence = p.conf;
            a.resolvedOutput = p.out;
            path.arcs.push_back(a);
        }
        SegmentComposer composer;
        UnifiedCandidate c;
        check(composer.compose(path, c), "features: compose ok");
        FeatureBuilder fb;
        RankFeatures f = fb.build(c);
        check(f.arcCount == 5, "features: arcCount");
        check(f.chineseArcs == 4 && f.englishArcs == 1, "features: source mix");
        check(f.exactArcs == 5, "features: exact count");
        check(f.boundarySwitches == 2, "features: two C→E→C boundary switches");
    }

    // 6. UnifiedRanker: an Exact-heavy path beats a Completion-heavy one.
    {
        UnifiedCandidate exact;
        SegmentationPath p;
        SegmentationArc a;
        a.rawBegin = 0;
        a.rawEnd = 6;
        a.source = SegmentSource::English;
        a.provenance = CandidateProvenance::Exact;
        a.confidence = 0.9F;
        a.resolvedOutput = "iPhone";
        p.arcs = {a};
        p.cost = 0.1;
        SegmentComposer composer;
        composer.compose(p, exact);

        UnifiedCandidate weak;
        SegmentationPath q;
        SegmentationArc b;
        b.rawBegin = 0;
        b.rawEnd = 4;
        b.source = SegmentSource::English;
        b.provenance = CandidateProvenance::Completion;
        b.confidence = 0.5F;
        b.resolvedOutput = "iPho"; // short prefix
        SegmentationArc c;
        c.rawBegin = 4;
        c.rawEnd = 6;
        c.source = SegmentSource::English;
        c.provenance = CandidateProvenance::Completion;
        c.confidence = 0.4F;
        c.resolvedOutput = "ne";
        q.arcs = {b, c};
        q.cost = 0.9;
        composer.compose(q, weak);

        UnifiedRanker ranker;
        std::vector<UnifiedCandidate> pool{weak, exact};
        auto ranked = ranker.rank(pool);
        check(ranked.size() == 2, "ranker: pool preserved");
        check(ranked.front().composedText == "iPhone",
              "ranker: exact path beats weak completion path");
    }

    // 7. UnifiedRanker with empty pool: safe, no throw.
    {
        UnifiedRanker ranker;
        std::vector<UnifiedCandidate> empty;
        auto ranked = ranker.rank(empty);
        check(ranked.empty(), "ranker: empty pool ok");
    }

    // 8. Rewriter: default (autoSpace OFF) is a no-op.
    {
        Rewriter rw;
        UnifiedCandidate c;
        SegmentationArc a;
        a.rawBegin = 0;
        a.rawEnd = 2;
        a.source = SegmentSource::Chinese;
        a.provenance = CandidateProvenance::Exact;
        a.resolvedOutput = "我";
        SegmentationArc b;
        b.rawBegin = 2;
        b.rawEnd = 8;
        b.source = SegmentSource::English;
        b.provenance = CandidateProvenance::Exact;
        b.resolvedOutput = "iPhone";
        SegmentationPath p;
        p.arcs = {a, b};
        SegmentComposer composer;
        composer.compose(p, c);
        auto rw0 = rw.rewrite(c);
        check(rw0.composedText == c.composedText,
              "rewriter: OFF is a no-op (§23 default)");
    }

    // 9. Rewriter: autoSpace ON inserts at each C↔E switch.
    {
        RewriterConfig cfg;
        cfg.autoSpaceAtBoundary = true;
        Rewriter rw(cfg);
        UnifiedCandidate c;
        SegmentationArc a;
        a.rawBegin = 0;
        a.rawEnd = 2;
        a.source = SegmentSource::Chinese;
        a.resolvedOutput = "我";
        SegmentationArc b;
        b.rawBegin = 2;
        b.rawEnd = 8;
        b.source = SegmentSource::English;
        b.resolvedOutput = "iPhone";
        SegmentationPath p;
        p.arcs = {a, b};
        SegmentComposer composer;
        composer.compose(p, c);
        auto out = rw.rewrite(c);
        check(out.composedText == "我 iPhone",
              "rewriter: ON inserts single ASCII space at C→E");
        check(out.segments.size() == 2, "rewriter: segment count preserved");
        check(out.alignment.size() == 2, "rewriter: alignment preserved");
        check(out.alignment[1].outputBegin == 4 &&
                  out.alignment[1].outputEnd == 10,
              "rewriter: alignment offsets recomputed");
    }

    // 10. Rewriter idempotence: rewriting a rewritten candidate again
    //     produces identical output.
    {
        RewriterConfig cfg;
        cfg.autoSpaceAtBoundary = true;
        Rewriter rw(cfg);
        UnifiedCandidate c;
        SegmentationArc a;
        a.rawBegin = 0;
        a.rawEnd = 2;
        a.source = SegmentSource::Chinese;
        a.resolvedOutput = "我";
        SegmentationArc b;
        b.rawBegin = 2;
        b.rawEnd = 8;
        b.source = SegmentSource::English;
        b.resolvedOutput = "iPhone";
        SegmentationPath p;
        p.arcs = {a, b};
        SegmentComposer composer;
        composer.compose(p, c);
        auto once = rw.rewrite(c);
        auto twice = rw.rewrite(once);
        // second rewrite sees no C↔E switch because segments carry the
        // same sources — no additional space should be added.
        check(once.composedText == twice.composedText, "rewriter: idempotent");
    }

    // 11. SegmentComposer englishTerminal (fusion.7 device blockers A/C):
    //     a shadowing English completion or a two-byte abbreviation may not be
    //     followed by another English arc, but may still be followed by Chinese.
    {
        SegmentComposer composer;

        // [Completion "Kubernetes" over 0..5][Exact English "nets" 5..9] was
        // the device malformed "Kubernetesnets…": the completion prints more
        // bytes than it consumes, so a second English arc re-tiles shadowed
        // raw. Must be rejected.
        {
            SegmentationPath p;
            SegmentationArc a;
            a.rawBegin = 0;
            a.rawEnd = 5;
            a.source = SegmentSource::English;
            a.provenance = CandidateProvenance::Completion;
            a.resolvedOutput = "Kubernetes"; // longer than the 5 raw bytes
            SegmentationArc b;
            b.rawBegin = 5;
            b.rawEnd = 9;
            b.source = SegmentSource::English;
            b.provenance = CandidateProvenance::Exact;
            b.resolvedOutput = "nets";
            p.arcs = {a, b};
            UnifiedCandidate c;
            check(!composer.compose(p, c),
                  "composer: shadowing completion + second English rejected (C)");
        }
        // [Canonical abbreviation "UI" 0..2][English "she" 2..5] was the
        // malformed "UIshe…": a two-byte Latin read is a whole token, not the
        // start of a letter run. Must be rejected.
        {
            SegmentationPath p;
            SegmentationArc a;
            a.rawBegin = 0;
            a.rawEnd = 2;
            a.source = SegmentSource::English;
            a.provenance = CandidateProvenance::Canonical;
            a.resolvedOutput = "UI";
            SegmentationArc b;
            b.rawBegin = 2;
            b.rawEnd = 5;
            b.source = SegmentSource::English;
            b.provenance = CandidateProvenance::Exact;
            b.resolvedOutput = "she";
            p.arcs = {a, b};
            UnifiedCandidate c;
            check(!composer.compose(p, c),
                  "composer: two-byte abbreviation + English rejected (A)");
        }
        // Legitimate mixed result preserved: [Completion "Kubernetes" 0..5]
        // followed by Chinese 5..11 composes to "Kubernetes集群".
        {
            SegmentationPath p;
            SegmentationArc a;
            a.rawBegin = 0;
            a.rawEnd = 5;
            a.source = SegmentSource::English;
            a.provenance = CandidateProvenance::Completion;
            a.resolvedOutput = "Kubernetes";
            SegmentationArc b;
            b.rawBegin = 5;
            b.rawEnd = 11;
            b.source = SegmentSource::Chinese;
            b.provenance = CandidateProvenance::Exact;
            b.resolvedOutput = "集群";
            p.arcs = {a, b};
            UnifiedCandidate c;
            check(composer.compose(p, c),
                  "composer: shadowing completion + Chinese accepted");
            check(c.composedText == "Kubernetes集群",
                  "composer: completion + Chinese text");
        }
        // Control: two non-shadowing >=3-byte exact English arcs still tile
        // (the terminal rule must not over-block ordinary letter runs).
        {
            SegmentationPath p;
            SegmentationArc a;
            a.rawBegin = 0;
            a.rawEnd = 5;
            a.source = SegmentSource::English;
            a.provenance = CandidateProvenance::Exact;
            a.resolvedOutput = "hello";
            SegmentationArc b;
            b.rawBegin = 5;
            b.rawEnd = 10;
            b.source = SegmentSource::English;
            b.provenance = CandidateProvenance::Exact;
            b.resolvedOutput = "world";
            p.arcs = {a, b};
            UnifiedCandidate c;
            check(composer.compose(p, c),
                  "composer: plain exact English letter run still accepted");
        }
    }

    // 12. UnifiedRanker::isBoundedInsertionClass (fusion.7 device blocker B):
    //     a leading canonical English word plus a Chinese remainder that
    //     exactly consumes the raw is now a bounded insertion; non-Chinese or
    //     gapped remainders are not.
    {
        SegmentComposer composer;
        // [Canonical "OpenAI" 0..6][Chinese "模型" 6..12], rawSize 12.
        UnifiedCandidate leadChinese;
        {
            SegmentationPath p;
            SegmentationArc a;
            a.rawBegin = 0;
            a.rawEnd = 6;
            a.source = SegmentSource::English;
            a.provenance = CandidateProvenance::Canonical;
            a.resolvedOutput = "OpenAI";
            SegmentationArc b;
            b.rawBegin = 6;
            b.rawEnd = 12;
            b.source = SegmentSource::Chinese;
            b.provenance = CandidateProvenance::Exact;
            b.resolvedOutput = "模型";
            p.arcs = {a, b};
            check(composer.compose(p, leadChinese),
                  "ranker-B: lead English + Chinese composes");
        }
        check(UnifiedRanker::isBoundedInsertionClass(leadChinese, 12),
              "ranker-B: [canonical English][Chinese] is a bounded insertion");
        // Single whole-raw canonical arc (pre-existing member of the class).
        UnifiedCandidate single;
        {
            SegmentationPath p;
            SegmentationArc a;
            a.rawBegin = 0;
            a.rawEnd = 6;
            a.source = SegmentSource::English;
            a.provenance = CandidateProvenance::Canonical;
            a.resolvedOutput = "OpenAI";
            p.arcs = {a};
            composer.compose(p, single);
        }
        check(UnifiedRanker::isBoundedInsertionClass(single, 6),
              "ranker-B: whole-raw single canonical arc stays a member");
        // fusion.14: the same evidence when the canonical word TRAILS the
        // Chinese part — Ziranma "woxdmliphone" -> [我想买][iPhone], where
        // the Chinese decoder also accepts "ip ho ne" as a forced 蠢货呢.
        // Before this case existed the fused candidate was buried behind the
        // entire classical list (measured: index 127).
        UnifiedCandidate trailChinese;
        {
            SegmentationPath p;
            SegmentationArc a;
            a.rawBegin = 0;
            a.rawEnd = 6;
            a.source = SegmentSource::Chinese;
            a.provenance = CandidateProvenance::Exact;
            a.resolvedOutput = "我想买";
            SegmentationArc b;
            b.rawBegin = 6;
            b.rawEnd = 12;
            b.source = SegmentSource::English;
            b.provenance = CandidateProvenance::Canonical;
            b.resolvedOutput = "iPhone";
            p.arcs = {a, b};
            check(composer.compose(p, trailChinese),
                  "ranker-B: Chinese + trailing English composes");
        }
        check(UnifiedRanker::isBoundedInsertionClass(trailChinese, 12),
              "ranker-B: [Chinese][canonical English] is a bounded insertion");
        // Two English arcs are outside the class wherever they sit.
        {
            UnifiedCandidate bad = trailChinese;
            bad.sources[0] = SegmentSource::English;
            check(!UnifiedRanker::isBoundedInsertionClass(bad, 12),
                  "ranker-B: two English arcs rejected");
        }
        // A trailing word whose surface equals the typed span is Exact, and
        // exactness inside a Chinese reading is rule (d)'s business, not this
        // class: ambiguous lowercase overlaps must keep the old placement.
        {
            UnifiedCandidate bad = trailChinese;
            bad.provenances[1] = CandidateProvenance::Exact;
            check(!UnifiedRanker::isBoundedInsertionClass(bad, 12),
                  "ranker-B: trailing Exact word is not the canonical class");
        }
        // A trailing word that stops short of the end is not a whole-raw
        // reading.
        {
            UnifiedCandidate bad = trailChinese;
            bad.alignment[1].rawEnd = 11;
            check(!UnifiedRanker::isBoundedInsertionClass(bad, 12),
                  "ranker-B: trailing tiling that misses the raw end rejected");
        }
        // English remainder (not Chinese) is rejected by structure.
        {
            UnifiedCandidate bad = leadChinese;
            bad.sources[1] = SegmentSource::English;
            check(!UnifiedRanker::isBoundedInsertionClass(bad, 12),
                  "ranker-B: non-Chinese remainder rejected");
        }
        // Gapped tiling is rejected (must consume the raw contiguously).
        {
            UnifiedCandidate bad = leadChinese;
            bad.alignment[1].rawBegin = 7;
            check(!UnifiedRanker::isBoundedInsertionClass(bad, 12),
                  "ranker-B: gapped remainder rejected");
        }
    }

    // 13. EnglishArcOracle two-byte abbreviation admission (fusion.7 device
    //     blocker A): the span floor keeps two-byte English out EXCEPT an exact
    //     dictionary hit whose surface is all-uppercase; below-floor
    //     completions stay suppressed.
    {
        EnglishLexicon shortLex;
        shortLex.loadFromEntries({
            {"ai", "AI", 9, true, false, false},
            {"air", "air", 7, false, false, false},
            {"us", "us", 6, false, false, false},
            {"hello", "hello", 8, false, false, false},
        });
        EnglishArcOracle oracle(&shortLex);
        const auto hasOutput = [](const std::vector<SegmentationArc> &arcs,
                                  const char *needle) {
            for (const auto &a : arcs) {
                if (a.resolvedOutput == needle) {
                    return true;
                }
            }
            return false;
        };
        const auto hasCompletion = [](const std::vector<SegmentationArc> &arcs) {
            for (const auto &a : arcs) {
                if (a.provenance == CandidateProvenance::Completion) {
                    return true;
                }
            }
            return false;
        };
        // "ai" -> all-uppercase abbreviation admitted as canonical "AI".
        auto aiArcs = oracle.arcsAt("ai", 0, 2);
        check(hasOutput(aiArcs, "AI"),
              "oracle-A: two-byte uppercase abbreviation admitted");
        check(!hasCompletion(aiArcs),
              "oracle-A: two-byte prefix completion suppressed");
        // "us" -> lowercase two-byte word stays gated by the floor.
        auto usArcs = oracle.arcsAt("us", 0, 2);
        check(!hasOutput(usArcs, "us"),
              "oracle-A: lowercase two-byte word not admitted");
        // Control: at/above the floor, completions still emit.
        auto helArcs = oracle.arcsAt("hello", 0, 3);
        check(hasCompletion(helArcs),
              "oracle-A: completion emission intact above the floor");
    }

    if (failures == 0) {
        std::printf(
            "Pipeline (composer/pool/ranker/rewriter): ALL TESTS PASSED\n");
        return 0;
    }
    std::fprintf(stderr, "Pipeline: %d failure(s)\n", failures);
    return 1;
}
