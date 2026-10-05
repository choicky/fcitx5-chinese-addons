/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "english/englishcorrectionoracle.h"
#include "english/englishlexicon.h"

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

static std::vector<EnglishLexiconEntry> seed() {
    // Test-only seed data. Each entry's tier is hand-authored by this
    // project's contributors.
    return {
        {"hello", "hello", 8, false, false, false},
        {"help", "help", 7, false, false, false},
        {"world", "world", 8, false, false, false},
        {"word", "word", 7, false, false, false},
        {"work", "work", 7, false, false, false},
        {"test", "test", 6, false, false, false},
        {"best", "best", 6, false, false, false},
        {"cat", "cat", 5, false, false, false},
        {"car", "car", 5, false, false, false},
        {"cap", "cap", 4, false, false, false},
        {"apply", "apply", 5, false, false, false},
        {"apple", "apple", 7, false, false, false},
    };
}

static bool hasCorrection(const std::vector<SegmentationArc> &arcs, size_t b,
                          size_t e) {
    for (const auto &a : arcs) {
        if (a.rawBegin == b && a.rawEnd == e &&
            a.provenance == CandidateProvenance::Correction) {
            return true;
        }
    }
    return false;
}

int main() {
    EnglishLexicon lex;
    lex.loadFromEntries(seed());
    EnglishCorrectionOracle oracle(&lex);

    // 1. Adjacency sanity: 'g' is adjacent to 'h' and 'f', 'q' adjacent to
    //    'w' only, 'm' adjacent to 'n' only.
    {
        auto adjG = EnglishCorrectionOracle::qwertyAdjacent('g');
        bool gHasH = false, gHasF = false;
        for (char c : adjG) {
            if (c == 'h') {
                gHasH = true;
            }
            if (c == 'f') {
                gHasF = true;
            }
        }
        check(gHasH && gHasF, "adjacency: g touches h and f");
        auto adjQ = EnglishCorrectionOracle::qwertyAdjacent('q');
        check(adjQ.size() == 1 && adjQ[0] == 'w', "adjacency: q touches w");
        auto adjM = EnglishCorrectionOracle::qwertyAdjacent('m');
        check(adjM.size() == 1 && adjM[0] == 'n', "adjacency: m touches n");
        auto adjUpper = EnglishCorrectionOracle::qwertyAdjacent('H');
        bool upperHasG = false;
        for (char c : adjUpper) {
            if (c == 'g') {
                upperHasG = true;
            }
        }
        check(upperHasG, "adjacency: uppercase input folded");
    }

    // 2. Neighbor enumeration: bounded and excludes input itself.
    {
        auto ns = EnglishCorrectionOracle::neighbors("ab");
        // 'a' has adj {s}; 'b' has adj {v,n}. Also transposition "ba".
        // Expect at least 3 unique neighbors, no "ab".
        check(ns.size() >= 3, "neighbors: at least expected count");
        bool selfIncluded = false;
        for (const auto &n : ns) {
            if (n == "ab") {
                selfIncluded = true;
            }
        }
        check(!selfIncluded, "neighbors: excludes input");
    }

    // 3. Positive correction: 'hello' typed as 'gello' (adjacent g→h) should
    //    produce a correction arc on the whole span.
    {
        std::string s = "gello";
        auto arcs = oracle.arcsAt(s, 0, s.size());
        check(hasCorrection(arcs, 0, 5),
              "positive: gello -> correction arc for hello");
    }

    // 4. Positive transposition: 'word' typed as 'owrd' (swap first pair).
    {
        std::string s = "owrd";
        auto arcs = oracle.arcsAt(s, 0, s.size());
        // Either 'word' (transposition) or possibly 'wold' etc — at least
        // one correction arc should be present for this span.
        check(hasCorrection(arcs, 0, 4), "positive: owrd has correction arc");
    }

    // 5. False-correction guard: a span that IS a real English word must
    //    NOT yield any correction arc (§17.4).
    {
        std::string s = "hello";
        auto arcs = oracle.arcsAt(s, 0, s.size());
        check(arcs.empty(), "guard: exact word yields no correction");
    }
    {
        std::string s = "cat";
        auto arcs = oracle.arcsAt(s, 0, s.size());
        check(arcs.empty(), "guard: cat is exact, no correction");
    }

    // 6. False-correction negative: 'xyz' with no nearby lexicon words.
    {
        std::string s = "xyz";
        auto arcs = oracle.arcsAt(s, 0, s.size());
        check(arcs.empty(), "negative: xyz has no lexicon neighbor");
    }

    // 7. Non-adjacent substitution should NOT correct: 'xello' (x not
    //    adjacent to h) should not produce a correction for 'hello'.
    {
        std::string s = "xello";
        auto arcs = oracle.arcsAt(s, 0, s.size());
        // There could still be other corrections like 'cell' or 'celf'?
        // Ensure 'hello' is not among them. Since we do not track arc ->
        // candidate-word here, use the fact that substitution x->z produces
        // 'zello' (no hit) and 'hello' requires x->h, which is NOT adjacent.
        bool producedSomethingForFullSpan = false;
        for (const auto &a : arcs) {
            if (a.rawBegin == 0 && a.rawEnd == 5) {
                producedSomethingForFullSpan = true;
            }
        }
        check(!producedSomethingForFullSpan,
              "non-adjacent: no correction for xello->hello");
    }

    // 8. Confidence cap: no correction arc exceeds 0.85 (§17.4).
    {
        std::string s = "gello";
        auto arcs = oracle.arcsAt(s, 0, s.size());
        bool withinCap = true;
        for (const auto &a : arcs) {
            if (a.provenance == CandidateProvenance::Correction &&
                a.confidence > 0.85F) {
                withinCap = false;
            }
        }
        check(withinCap, "cap: correction confidence <= 0.85");
    }

    // 9. Fan-out cap per span.
    {
        EnglishCorrectionOracle::Config cfg;
        cfg.maxCorrectionsPerSpan = 1;
        EnglishCorrectionOracle capOracle(&lex, cfg);
        // 'appl' has corrections: 'apply', 'apple' (via transposition or
        // adjacent substitution paths). With cap=1, at most one arc per
        // span.
        auto arcs = capOracle.arcsAt("appl", 0, 4);
        std::size_t per4 = 0;
        for (const auto &a : arcs) {
            if (a.rawEnd == 4 &&
                a.provenance == CandidateProvenance::Correction) {
                ++per4;
            }
        }
        check(per4 <= 1, "fanout cap: at most maxCorrectionsPerSpan per span");
    }

    // 10. Empty lexicon yields no arcs and does not throw.
    {
        EnglishLexicon empty;
        EnglishCorrectionOracle e(&empty);
        auto arcs = e.arcsAt("gello", 0, 5);
        check(arcs.empty(), "empty lex: no arcs, safe");
    }

    if (failures == 0) {
        std::printf("EnglishCorrectionOracle: ALL TESTS PASSED\n");
        return 0;
    }
    std::fprintf(stderr, "EnglishCorrectionOracle: %d failure(s)\n", failures);
    return 1;
}
