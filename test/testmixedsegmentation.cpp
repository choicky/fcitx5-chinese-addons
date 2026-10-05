/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "mixed/mixedsegmentation.h"

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

    if (failures == 0) {
        std::printf("MixedSegmentationSearch: ALL TESTS PASSED\n");
        return 0;
    }
    std::fprintf(stderr, "MixedSegmentationSearch: %d failure(s)\n", failures);
    return 1;
}
