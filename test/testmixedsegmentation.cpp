/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "mixed/mixedsegmentation.h"

#include <chrono>
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

// Test-only arc tables. These are NOT a product syllable table: the shipped
// Chinese oracle delegates to LibIME's real parser. Here they exist purely to
// exercise the bounded search algorithm deterministically.
struct ChineseOracle : IChineseArcOracle {
    std::vector<SegmentationArc> arcs;
    std::vector<SegmentationArc> arcsAt(std::string_view, size_t begin,
                                        size_t) const override {
        std::vector<SegmentationArc> out;
        for (const auto &arc : arcs) {
            if (arc.rawBegin == begin) {
                out.push_back(arc);
            }
        }
        return out;
    }
};
struct EnglishOracle : IEnglishArcOracle {
    std::vector<SegmentationArc> arcs;
    std::vector<SegmentationArc> arcsAt(std::string_view, size_t begin,
                                        size_t) const override {
        std::vector<SegmentationArc> out;
        for (const auto &arc : arcs) {
            if (arc.rawBegin == begin) {
                out.push_back(arc);
            }
        }
        return out;
    }
};

static bool pathContainsArc(const SegmentationPath &p, size_t b, size_t e,
                            SegmentSource src) {
    for (const auto &arc : p.arcs) {
        if (arc.rawBegin == b && arc.rawEnd == e && arc.source == src) {
            return true;
        }
    }
    return false;
}

int main() {
    // 1. Basic mixed: Chinese cannot cover [10,16) so the English arc must win,
    //    while Chinese syllables cover the rest.
    {
        std::string raw = "woxiangmaiiphonepeijian"; // 23 bytes
        ChineseOracle ch;
        ch.arcs = {
            {0, 2, SegmentSource::Chinese, CandidateProvenance::Exact, 0.5F,
             1.0F, 0, ""},
            {2, 7, SegmentSource::Chinese, CandidateProvenance::Exact, 0.5F,
             1.0F, 0, ""},
            {7, 10, SegmentSource::Chinese, CandidateProvenance::Exact, 0.5F,
             1.0F, 0, ""},
            {16, 19, SegmentSource::Chinese, CandidateProvenance::Exact, 0.5F,
             1.0F, 0, ""},
            {19, 23, SegmentSource::Chinese, CandidateProvenance::Exact, 0.5F,
             1.0F, 0, ""},
        };
        EnglishOracle en;
        en.arcs = {{10, 16, SegmentSource::English, CandidateProvenance::Exact,
                    0.95F, 1.0F, 0, ""}};

        MixedSegmentationSearch search;
        auto paths = search.search(raw, ch, en);
        check(!paths.empty(), "mixed: a complete path exists");
        check(paths.size() <= 4, "mixed: top-K bound respected");
        // costs ascending
        bool sorted = true;
        for (size_t i = 1; i < paths.size(); ++i) {
            if (paths[i].cost < paths[i - 1].cost - 1e-9) {
                sorted = false;
            }
        }
        check(sorted, "mixed: paths sorted by cost ascending");
        check(pathContainsArc(paths.front(), 10, 16, SegmentSource::English),
              "mixed: English 'iphone' arc chosen for [10,16)");
        // whole range tiled: sum of arc spans covers 0..23 contiguously
        size_t cursor = 0;
        bool tiling = true;
        for (const auto &arc : paths.front().arcs) {
            if (arc.rawBegin != cursor) {
                tiling = false;
            }
            cursor = arc.rawEnd;
        }
        check(tiling && cursor == raw.size(), "mixed: path tiles whole range");
    }

    // 2. Ambiguity "ai": Chinese and English interpretations must BOTH survive
    //    (no hard language routing).
    {
        std::string raw = "ai"; // 2 bytes
        ChineseOracle ch;
        ch.arcs = {{0, 2, SegmentSource::Chinese, CandidateProvenance::Exact,
                    0.6F, 1.0F, 0, ""}};
        EnglishOracle en;
        en.arcs = {{0, 2, SegmentSource::English, CandidateProvenance::Exact,
                    0.6F, 1.0F, 0, ""}};
        MixedSegmentationSearch search;
        auto paths = search.search(raw, ch, en);
        bool hasCh = false, hasEn = false;
        for (const auto &p : paths) {
            if (pathContainsArc(p, 0, 2, SegmentSource::Chinese)) {
                hasCh = true;
            }
            if (pathContainsArc(p, 0, 2, SegmentSource::English)) {
                hasEn = true;
            }
        }
        check(hasCh, "ambiguity: Chinese interpretation of 'ai' survives");
        check(hasEn, "ambiguity: English interpretation of 'ai' survives");
    }

    // 3. Evidence-backed more segments beats a weak-boundary few-segment path
    //    (segment count itself is not penalized).
    {
        std::string raw = "abcdef"; // 6 bytes
        // Path A: two high-confidence English arcs (cost .05 each)
        EnglishOracle en;
        en.arcs = {{0, 3, SegmentSource::English, CandidateProvenance::Exact,
                    0.95F, 1.0F, 0, ""},
                   {3, 6, SegmentSource::English, CandidateProvenance::Exact,
                    0.95F, 1.0F, 0, ""}};
        // Path B: one low-confidence arc with a weak boundary
        ChineseOracle ch;
        ch.arcs = {{0, 6, SegmentSource::Chinese, CandidateProvenance::Exact,
                    0.1F, 0.1F, 0, ""}};
        MixedSegmentationSearch search;
        auto paths = search.search(raw, ch, en);
        check(!paths.empty(), "count-test: path exists");
        check(pathContainsArc(paths.front(), 0, 3, SegmentSource::English) &&
                  pathContainsArc(paths.front(), 3, 6, SegmentSource::English),
              "count-test: two evidence-backed arcs outrank one weak arc");
    }

    // 4. No complete tiling possible -> empty (bounded, not a crash).
    {
        std::string raw = "xyz";
        ChineseOracle ch;
        EnglishOracle en;
        MixedSegmentationSearch search;
        auto paths = search.search(raw, ch, en);
        check(paths.empty(), "no-coverage: empty result");
    }

    // 5. Frozen-prefix skip: searchFrom(10) covers only [10,23).
    {
        std::string raw = "woxiangmaiiphonepeijian";
        EnglishOracle en;
        en.arcs = {{10, 16, SegmentSource::English, CandidateProvenance::Exact,
                    0.95F, 1.0F, 0, ""},
                   {16, 23, SegmentSource::English, CandidateProvenance::Exact,
                    0.95F, 1.0F, 0, ""}};
        ChineseOracle ch; // no arcs -> only English path from begin
        MixedSegmentationSearch search;
        auto paths = search.searchFrom(raw, 10, ch, en);
        check(!paths.empty() && paths.front().arcs.front().rawBegin == 10,
              "searchFrom: starts at frozen-prefix boundary");
        check(paths.front().arcs.back().rawEnd == raw.size(),
              "searchFrom: covers to end");
    }

    // 6. Forward-progress guard: a back-arc must be ignored (no infinite loop).
    {
        std::string raw = "abcd";
        EnglishOracle en;
        en.arcs = {
            {4, 2, SegmentSource::English, CandidateProvenance::Exact, 0.9F,
             1.0F, 0, ""}, // invalid: rawEnd < rawBegin and > i guard
            {0, 2, SegmentSource::English, CandidateProvenance::Exact, 0.9F,
             1.0F, 0, ""},
            {2, 4, SegmentSource::English, CandidateProvenance::Exact, 0.9F,
             1.0F, 0, ""},
        };
        ChineseOracle ch;
        MixedSegmentationSearch search;
        auto paths = search.search(raw, ch, en);
        check(!paths.empty() && paths.front().arcs.size() == 2,
              "progress-guard: back-arc ignored, valid tiling found");
    }

    // S1 (Phase 3A-2 §9, structural RED — mechanism proven by the run14
    // beam trace, witness-B class): a two-English-segment reading exists in
    // the DAG, but the single cost-sorted window lets cheap broad Chinese
    // arcs of segment-class 0 evict the one-run prefix that is the ONLY
    // carrier of the second switch. Chinese alternatives are deliberately
    // much cheaper; the tables carry no real words (the doubles ignore raw
    // content), so no dictionary/product evidence is encoded.
    {
        std::string raw = "abcdefghijklmnopqrst"; // 20 bytes
        ChineseOracle ch;
        auto cheap = [](size_t b, size_t e, int rank) {
            return SegmentationArc{b,
                                   e,
                                   SegmentSource::Chinese,
                                   CandidateProvenance::Exact,
                                   0.75F,
                                   1.0F,
                                   rank,
                                   ""};
        };
        ch.arcs = {
            cheap(0, 4, 0),   cheap(0, 6, 0),   cheap(0, 8, 0),
            cheap(0, 8, 1),   cheap(0, 8, 2),   cheap(0, 8, 3),
            cheap(8, 10, 0),  cheap(8, 10, 1),  cheap(8, 10, 2),
            cheap(8, 10, 3),  cheap(8, 12, 0),  cheap(8, 12, 1),
            cheap(8, 12, 2),  cheap(8, 12, 3),  cheap(10, 12, 0),
            cheap(10, 12, 1), cheap(10, 12, 2), cheap(10, 12, 3),
            cheap(10, 14, 0), cheap(10, 14, 1), cheap(10, 14, 2),
            cheap(10, 14, 3), cheap(10, 16, 0), cheap(10, 16, 1),
            cheap(10, 16, 2), cheap(10, 16, 3), cheap(12, 16, 0),
            cheap(12, 16, 1), cheap(12, 16, 2), cheap(12, 16, 3),
            cheap(14, 16, 0), cheap(14, 16, 1), cheap(14, 16, 2),
            cheap(14, 16, 3), cheap(16, 18, 0), cheap(16, 18, 1),
            cheap(16, 18, 2), cheap(16, 18, 3), cheap(16, 20, 0),
            cheap(16, 20, 1), cheap(16, 20, 2), cheap(16, 20, 3),
            cheap(18, 20, 0), cheap(18, 20, 1), cheap(18, 20, 2),
            cheap(18, 20, 3),
        };
        EnglishOracle en;
        en.arcs = {
            {4, 8, SegmentSource::English, CandidateProvenance::Exact, 0.9F,
             1.0F, 0, ""},
            {12, 16, SegmentSource::English, CandidateProvenance::Exact, 0.9F,
             1.0F, 0, ""},
        };
        MixedSegmentationSearch search;
        auto paths = search.search(raw, ch, en);
        bool twoRunReachable = false;
        for (const auto &p : paths) {
            if (pathContainsArc(p, 4, 8, SegmentSource::English) &&
                pathContainsArc(p, 12, 16, SegmentSource::English)) {
                twoRunReachable = true;
            }
        }
        check(twoRunReachable,
              "S1: two-segment reading survives a cheaper zero-segment flood");
    }

    // S2 (Phase 3A-2 §9, structural RED — the last-source bit): within the
    // SAME run-count class, a prefix ending mid-English-run and a prefix
    // that just closed its run with Chinese have different class
    // trajectories for identical continuations (only the Chinese-ending
    // prefix can open a new segment with the single available English arc).
    // Eight cheaper same-count endings-E prefixes therefore evict the
    // sole two-segment carrier. Same-count merge = run-count-only classing;
    // this case separates it from the adopted (count, last-source) key.
    {
        std::string raw = "abcdefghijklmnopqrst"; // 20 bytes
        ChineseOracle ch;
        ch.arcs = {
            {0, 4, SegmentSource::Chinese, CandidateProvenance::Exact, 0.75F,
             1.0F, 0, ""},
            {0, 4, SegmentSource::Chinese, CandidateProvenance::Exact, 0.75F,
             1.0F, 1, ""},
            {0, 4, SegmentSource::Chinese, CandidateProvenance::Exact, 0.75F,
             1.0F, 2, ""},
            {0, 4, SegmentSource::Chinese, CandidateProvenance::Exact, 0.75F,
             1.0F, 3, ""},
            {0, 4, SegmentSource::Chinese, CandidateProvenance::Exact, 0.75F,
             1.0F, 4, ""},
            {0, 4, SegmentSource::Chinese, CandidateProvenance::Exact, 0.75F,
             1.0F, 5, ""},
            {0, 4, SegmentSource::Chinese, CandidateProvenance::Exact, 0.75F,
             1.0F, 6, ""},
            {0, 4, SegmentSource::Chinese, CandidateProvenance::Exact, 0.75F,
             1.0F, 7, ""},
            {8, 12, SegmentSource::Chinese, CandidateProvenance::Exact, 0.6F,
             0.9F, 0, ""},
            {16, 20, SegmentSource::Chinese, CandidateProvenance::Exact, 0.75F,
             1.0F, 0, ""},
        };
        EnglishOracle en;
        en.arcs = {
            {4, 8, SegmentSource::English, CandidateProvenance::Exact, 0.9F,
             1.0F, 0, ""},
            {8, 12, SegmentSource::English, CandidateProvenance::Exact, 0.98F,
             1.0F, 0, ""}, // extends run 1: same count, ending English
            {12, 16, SegmentSource::English, CandidateProvenance::Exact, 0.9F,
             1.0F, 0, ""}, // opens segment 2 only from a Chinese ending
        };
        MixedSegmentationSearch search;
        auto paths = search.search(raw, ch, en);
        bool twoRunReachable = false;
        for (const auto &p : paths) {
            // A genuine SECOND segment needs the C->E transition at 12:
            // E(4-8) then Chinese tiling of [8,12) then E(12-16). A single
            // consecutive English run E(4-16) also contains both arcs but
            // is one segment, which is not what this class state guards.
            if (pathContainsArc(p, 4, 8, SegmentSource::English) &&
                pathContainsArc(p, 8, 12, SegmentSource::Chinese) &&
                pathContainsArc(p, 12, 16, SegmentSource::English)) {
                twoRunReachable = true;
            }
        }
        check(twoRunReachable,
              "S2: same-count prefixes ending in different sources do not "
              "prune one another");
    }

    // C0/C1 (commit-readiness, englishSegmentCap knob + fill precedence):
    // one English completion [C,E] costs far more than every pure-Chinese
    // completion (confidence-only pricing, no dictionary content). C0: with
    // cap=0 the search must reproduce the legacy pre-diversity behavior —
    // the single cost-sorted terminal window evicts the English completion.
    // C1: under the classed default, class-precedence fill must give EVERY
    // top-K slot to English-bearing members while such members exist (72
    // cheaper zero-segment arrivals take no slot).
    {
        std::string raw = "abcdefgh"; // 8 bytes
        ChineseOracle ch;
        for (size_t b = 0; b < 8; b += 2) {
            for (int rank = 0; rank < 9; ++rank) {
                ch.arcs.push_back({b, b + 2, SegmentSource::Chinese,
                                   CandidateProvenance::Exact, 0.99F, 1.0F,
                                   rank, ""});
            }
        }
        EnglishOracle en;
        en.arcs = {
            {2, 8, SegmentSource::English, CandidateProvenance::Exact, 0.0F,
             0.0F, 0, ""},
        };
        auto legacyCfg = MixedSegmentationSearch::Config{};
        legacyCfg.englishSegmentCap = 0;
        MixedSegmentationSearch legacy(legacyCfg);
        auto paths = legacy.search(raw, ch, en);
        bool legacyEnglish = false;
        for (const auto &p : paths) {
            if (pathContainsArc(p, 2, 8, SegmentSource::English)) {
                legacyEnglish = true;
            }
        }
        check(!legacyEnglish && paths.size() == legacyCfg.topK,
              "C0: englishSegmentCap=0 restores the legacy cost-sorted "
              "window, terminal eviction included");
        MixedSegmentationSearch classed;
        paths = classed.search(raw, ch, en);
        bool allSlotsEnglish = !paths.empty();
        for (const auto &p : paths) {
            if (!pathContainsArc(p, 2, 8, SegmentSource::English)) {
                allSlotsEnglish = false;
            }
        }
        check(allSlotsEnglish,
              "C1: class-precedence fill gives every top-K slot to an "
              "English-bearing member while such members exist");
    }

    // K1/K2 (top-K edge semantics of the reservation): raw with one-run
    // (cost ~1.53), pure (0.04) and two-run (3.02) completions. The reserved
    // head is the head of the HIGHEST non-empty English class, so at topK=1
    // the returned path must be the two-segment reading — not the cheapest
    // English-bearing member, and not the cheapest path. At topK=2 the second
    // slot goes to the next English-bearing member by cost, still ahead of
    // any zero-segment completion.
    {
        std::string raw = "abcdefgh"; // 8 bytes
        ChineseOracle ch;
        auto c = [](size_t b, size_t e) {
            return SegmentationArc{b,
                                   e,
                                   SegmentSource::Chinese,
                                   CandidateProvenance::Exact,
                                   0.99F,
                                   1.0F,
                                   0,
                                   ""};
        };
        ch.arcs = {c(0, 2), c(2, 4), c(4, 6), c(6, 8)};
        EnglishOracle en;
        auto e = [](size_t b, size_t epos) {
            return SegmentationArc{b,
                                   epos,
                                   SegmentSource::English,
                                   CandidateProvenance::Exact,
                                   0.0F,
                                   0.0F,
                                   0,
                                   ""};
        };
        en.arcs = {e(2, 4), e(6, 8)};
        auto k1Cfg = MixedSegmentationSearch::Config{};
        k1Cfg.topK = 1;
        MixedSegmentationSearch k1(k1Cfg);
        auto paths = k1.search(raw, ch, en);
        check(paths.size() == 1 &&
                  pathContainsArc(paths[0], 2, 4, SegmentSource::English) &&
                  pathContainsArc(paths[0], 6, 8, SegmentSource::English),
              "K1: topK=1 returns the reserved highest-English-class head "
              "(the two-segment reading), not the cheapest path");
        auto k2Cfg = MixedSegmentationSearch::Config{};
        k2Cfg.topK = 2;
        MixedSegmentationSearch k2(k2Cfg);
        paths = k2.search(raw, ch, en);
        bool k2AllEnglish = paths.size() == 2;
        bool k2HasTwoRun = false;
        for (const auto &p : paths) {
            const bool one = pathContainsArc(p, 2, 4, SegmentSource::English);
            const bool two = pathContainsArc(p, 6, 8, SegmentSource::English);
            if (!one && !two) {
                k2AllEnglish = false;
            }
            if (one && two) {
                k2HasTwoRun = true;
            }
        }
        check(k2AllEnglish && k2HasTwoRun,
              "K2: topK=2 keeps the reserved reading and fills the second "
              "slot with the next English-bearing member before any "
              "zero-segment completion");
    }

    // TN (no-English-completion behavior): with no English arcs at all the
    // reservation is skipped (no non-empty English class) and the selection
    // is exactly the legacy cost order. The overlapping C[2,6) arc gives two
    // complete tilings so the cost ordering is actually exercised: the cheap
    // 3-arc path must precede the expensive 4-arc one.
    {
        std::string raw = "abcdefgh"; // 8 bytes
        ChineseOracle ch;
        ch.arcs = {
            {0, 2, SegmentSource::Chinese, CandidateProvenance::Exact, 0.99F,
             1.0F, 0, ""},
            {2, 4, SegmentSource::Chinese, CandidateProvenance::Exact, 0.5F,
             1.0F, 0, ""},
            {2, 6, SegmentSource::Chinese, CandidateProvenance::Exact, 0.99F,
             1.0F, 0, ""},
            {4, 6, SegmentSource::Chinese, CandidateProvenance::Exact, 0.99F,
             1.0F, 0, ""},
            {6, 8, SegmentSource::Chinese, CandidateProvenance::Exact, 0.99F,
             1.0F, 0, ""},
        };
        EnglishOracle en;
        MixedSegmentationSearch classed;
        const auto paths = classed.search(raw, ch, en);
        bool ordered = paths.size() == 2;
        for (size_t i = 1; i < paths.size(); ++i) {
            if (paths[i - 1].cost > paths[i].cost) {
                ordered = false;
            }
        }
        for (const auto &p : paths) {
            for (const auto &a : p.arcs) {
                if (a.source != SegmentSource::Chinese) {
                    ordered = false;
                }
            }
        }
        check(ordered,
              "TN: with no English completion, terminal selection is the "
              "legacy cost order");
    }

    // CB (Cmax / Cmax+1 cap-class boundary): a run-EXTENSION reading (E
    // [12,16) followed by E [16,18) inside the SAME second segment) shares
    // the capped class with cheaper already-closed two-segment prefixes.
    // The product contract promises reachability of at most Cmax=2 switches;
    // above the cap the last-source bit only distinguishes run-extension
    // VARIANT TEXT inside the protected second segment, which every class
    // window bounds at beamWidth by the same policy that prunes same-class
    // variants below the cap. So at cap=2 (Cmax) the extension carrier is
    // legitimately pruned — asserted here as the documented out-of-guarantee
    // shape — while at cap=3 (Cmax+1) the bit is retained below that cap and
    // the extension reading is reachable. Both legs pin the collapse to
    // happen exactly at the cap; the ≥3-segment readings the collapse can
    // also affect are never asserted (outside the contract).
    {
        std::string raw = "abcdefghijklmnopqrstuvwx"; // 24 bytes
        ChineseOracle ch;
        auto c = [](size_t b, size_t e) {
            return SegmentationArc{b,
                                   e,
                                   SegmentSource::Chinese,
                                   CandidateProvenance::Exact,
                                   0.99F,
                                   1.0F,
                                   0,
                                   ""};
        };
        ch.arcs = {c(0, 4), c(8, 12), c(14, 16), c(18, 24)};
        EnglishOracle en;
        auto e = [](size_t b, size_t epos, float conf, int rank) {
            return SegmentationArc{b,
                                   epos,
                                   SegmentSource::English,
                                   CandidateProvenance::Exact,
                                   conf,
                                   1.0F,
                                   rank,
                                   ""};
        };
        en.arcs = {
            e(4, 8, 0.9F, 0),    e(12, 14, 0.9F, 0), // cheap second-run closers
            e(12, 14, 0.9F, 1),  e(12, 14, 0.9F, 2),
            e(12, 16, 0.05F, 0), // expensive extension carrier
            e(16, 18, 0.9F, 0),
        };
        auto cbCfg = MixedSegmentationSearch::Config{};
        cbCfg.beamWidth = 2;
        cbCfg.englishSegmentCap = 2;
        MixedSegmentationSearch capped(cbCfg);
        auto paths = capped.search(raw, ch, en);
        bool extSurvived = false;
        bool twoSegmentReserved = false;
        for (const auto &p : paths) {
            if (pathContainsArc(p, 12, 16, SegmentSource::English)) {
                extSurvived = true;
            }
            size_t runs = 0;
            bool lastEnglish = false;
            for (const auto &a : p.arcs) {
                const bool eng = (a.source == SegmentSource::English);
                if (eng && !lastEnglish) {
                    ++runs;
                }
                lastEnglish = eng;
            }
            if (runs >= 2) {
                twoSegmentReserved = true;
            }
        }
        check(!extSurvived && twoSegmentReserved,
              "CB2: at cap=Cmax the collapsed class prunes the extension "
              "carrier (documented out-of-guarantee) while ≥2-segment "
              "completions stay reserved");
        cbCfg.englishSegmentCap = 3;
        MixedSegmentationSearch widened(cbCfg);
        paths = widened.search(raw, ch, en);
        bool extReachable = false;
        for (const auto &p : paths) {
            if (pathContainsArc(p, 4, 8, SegmentSource::English) &&
                pathContainsArc(p, 12, 16, SegmentSource::English) &&
                pathContainsArc(p, 16, 18, SegmentSource::English)) {
                extReachable = true;
            }
        }
        check(extReachable,
              "CB3: at cap=Cmax+1 the last-source bit exists below that cap "
              "and the same extension reading is reachable");
    }

    // §14 micro-benchmark (measurement only, no pass/fail): same synthetic
    // arc families, legacy single window (cap=0) vs classed retention
    // (cap=2), cold and warm, on a long mixed raw. Oracles are the same
    // test doubles, so this isolates the search loop's constant factor.
    {
        std::string raw;
        while (raw.size() < 64) {
            raw += "abcdefghijkl";
        }
        raw.resize(64);
        ChineseOracle ch;
        EnglishOracle en;
        for (size_t b = 0; b < raw.size(); b += 2) {
            for (size_t len : {2U, 4U, 6U}) {
                if (b + len <= raw.size()) {
                    ch.arcs.push_back({b, b + len, SegmentSource::Chinese,
                                       CandidateProvenance::Exact, 0.75F, 1.0F,
                                       static_cast<int>(len), ""});
                }
            }
            if (b % 8 == 0 && b + 6 <= raw.size()) {
                en.arcs.push_back({b, b + 6, SegmentSource::English,
                                   CandidateProvenance::Exact, 0.9F, 1.0F, 0,
                                   ""});
            }
        }
        auto bench = [&](const char *name,
                         const MixedSegmentationSearch &search) {
            std::vector<double> us;
            for (int i = 0; i < 200; ++i) {
                auto t0 = std::chrono::steady_clock::now();
                auto paths = search.search(raw, ch, en);
                auto t1 = std::chrono::steady_clock::now();
                (void)paths.size();
                us.push_back(
                    std::chrono::duration<double, std::micro>(t1 - t0).count());
            }
            std::sort(us.begin(), us.end());
            std::fprintf(stderr,
                         "BENCH %s n=%zu iters=200 p50=%.1fus "
                         "p95=%.1fus max=%.1fus\n",
                         name, raw.size(), us[us.size() / 2],
                         us[us.size() * 95 / 100], us.back());
        };
        auto classedCfg = MixedSegmentationSearch::Config{};
        auto legacyCfg = MixedSegmentationSearch::Config{};
        legacyCfg.englishSegmentCap = 0;
        bench("legacy-cap0", MixedSegmentationSearch(legacyCfg));
        bench("classed-cap2", MixedSegmentationSearch(classedCfg));
    }

    if (failures == 0) {
        std::printf("MixedSegmentationSearch: ALL TESTS PASSED\n");
        return 0;
    }
    std::fprintf(stderr, "MixedSegmentationSearch: %d failure(s)\n", failures);
    return 1;
}
