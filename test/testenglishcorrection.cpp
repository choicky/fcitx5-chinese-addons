/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "english/englishcorrectionoracle.h"
#include "english/englishlexicon.h"

#include <cstdio>
#include <fstream>
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

    // 11. fusion.13 correction floor on the PRODUCTION resource with the
    //     PRODUCTION config (the default Config — pinyin.cpp constructs the
    //     oracle through the single-argument constructor, so what the product
    //     uses IS this default). Spans of three bytes are never corrected;
    //     spans of four and more still are. The preconditions are asserted
    //     loudly first: if the shipped lexicon stopped containing the
    //     3-letter neighbours that made this observable, or the 4-letter
    //     control stopped correcting, the case below would silently stop
    //     testing anything.
    {
        EnglishLexicon realLex;
#ifdef FUSION_ENGLISH_LEXICON_PATH
        {
            std::ifstream in(FUSION_ENGLISH_LEXICON_PATH);
            check(in.is_open(), "floor: shipped english_lexicon.tsv readable");
            realLex.load(in);
        }
#else
        check(false, "floor: FUSION_ENGLISH_LEXICON_PATH not defined");
#endif
        check(realLex.size() >= 20000,
              "floor: shipped resource has >= 20k entries");

        // Precondition A: every 3-letter raw below is NOT itself a lexicon
        // word (so the oracle's false-correction guard cannot be the reason
        // it stops correcting) and DOES have a real lexicon neighbour one
        // QWERTY edit away (so "no arcs" below can only come from the floor,
        // not from a dead neighbourhood). Both halves are measured facts of
        // the shipped resource, asserted loudly.
        struct ThreeCase {
            const char *raw;
            const char *neighbor;
        };
        const std::vector<ThreeCase> threes = {
            {"xat", "cat"}, {"eht", "eth"}, {"fpr", "for"}, {"jat", "hat"},
            {"kot", "lot"}, {"mof", "mod"}, {"pof", "pod"},
        };
        for (const auto &t : threes) {
            const auto foldedRaw = EnglishLexicon::foldKey(t.raw);
            check(foldedRaw && realLex.lookup(*foldedRaw) == nullptr,
                  (std::string("floor precondition: raw '") + t.raw +
                   "' is not a lexicon word")
                      .c_str());
            const auto folded = EnglishLexicon::foldKey(t.neighbor);
            check(folded && realLex.lookup(*folded) != nullptr,
                  (std::string("floor precondition: neighbour '") + t.neighbor +
                   "' is in the shipped lexicon")
                      .c_str());
        }

        // Precondition B: the same oracle with the OLD floor (3) DOES emit a
        // correction for every one of these spans, so the case below is live
        // and the neighbourhood is reachable.
        {
            EnglishCorrectionOracle::Config old3;
            old3.minSpanLength = 3;
            EnglishCorrectionOracle probe(&realLex, old3);
            std::size_t corrected = 0;
            for (const auto &t : threes) {
                const std::string s = t.raw;
                auto arcs = probe.arcsAt(s, 0, s.size());
                for (const auto &a : arcs) {
                    if (a.provenance == CandidateProvenance::Correction) {
                        ++corrected;
                        break;
                    }
                }
            }
            check(corrected == threes.size(),
                  "floor precondition: with minSpanLength=3 all seven "
                  "3-letter spans DO correct (else this test proves nothing)");
        }

        // The product config: no Correction arc for any 3-letter span —
        // neither a literal abbreviation typed on purpose (etf, cpi, ...)
        // nor a genuine 3-letter mistype (xat, eht, ...), whose corrections
        // precondition B just showed to be reachable.
        EnglishCorrectionOracle production(&realLex);
        for (const char *raw :
             {"xat", "eht", "fpr", "jat", "kot", "mof", "pof", "etf", "cpi",
              "vix", "fof", "kpi", "the", "and", "xyz"}) {
            const std::string s = raw;
            auto arcs = production.arcsAt(s, 0, s.size());
            bool anyCorrection = false;
            for (const auto &a : arcs) {
                if (a.provenance == CandidateProvenance::Correction) {
                    anyCorrection = true;
                }
            }
            check(!anyCorrection, (std::string("floor: 3-letter raw '") + raw +
                                   "' emits no Correction arc")
                                      .c_str());
        }

        // 4-letter spans still correct (transposition control taht -> that,
        // plus the substitution controls gello -> hello and owrd -> word).
        {
            const std::string s = "taht";
            auto arcs = production.arcsAt(s, 0, s.size());
            bool hitsThat = false;
            for (const auto &a : arcs) {
                if (a.provenance == CandidateProvenance::Correction &&
                    a.rawBegin == 0 && a.rawEnd == 4 &&
                    a.resolvedOutput == "that") {
                    hitsThat = true;
                }
            }
            check(hitsThat, "floor: the 4-letter control taht still gets a "
                            "Correction arc for 'that'");
            check(hasCorrection(production.arcsAt("gello", 0, 5), 0, 5),
                  "floor: the 5-letter control gello still corrects");
            check(hasCorrection(production.arcsAt("owrd", 0, 4), 0, 4),
                  "floor: the 4-letter control owrd still corrects");
        }
    }

    if (failures == 0) {
        std::printf("EnglishCorrectionOracle: ALL TESTS PASSED\n");
        return 0;
    }
    std::fprintf(stderr, "EnglishCorrectionOracle: %d failure(s)\n", failures);
    return 1;
}
