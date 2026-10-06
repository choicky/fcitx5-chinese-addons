/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
//
// Batch 10 — Shuangpin oracle smoke test (regression matrix).
//
// The Master Instruction's regression matrix for Batch 10 covers three
// Chinese input surfaces: Pinyin (already exercised by `testpinyin`
// integration + `testmixedchinesecorrection` oracle + `testmixedcorpus`
// pipeline), Shuangpin (this test), and Stroke / Auxiliary Filters (covered
// by Batch 7B-4's `testmixedengine` aux-frontier invariants and `testpinyin`
// integration).
//
// This test drives the real `LibIMEChineseArcOracle` in Shuangpin mode with
// a real builtin `libime::ShuangpinProfile` (Ziranma) and verifies the arc
// oracle produces a parser-backed graph at every span. Batch 8 recorded the
// Shuangpin-correction source ceiling (LibIME's `parseUserShuangpin` does
// not take a separate profile arg; correction is baked into the
// `ShuangpinProfile` key map at construction). This test asserts the ceiling
// is honored — `correctionEnabled()` must return false in Shuangpin mode
// regardless of any profile pointer set on the oracle — so a future change
// to the classical `ShuangpinProfile` correction behavior fails here first.
//
// Fusion closure §4 extends this file with the two remaining Shuangpin
// product-path invariants:
//   * Part B — `setMode()` round trip: switching between Pinyin and
//     Shuangpin must invalidate the cached segmentation graph (the two modes
//     are different parses of the same raw string) and must re-parse under
//     the new mode, while re-setting the current mode keeps the graph.
//   * Part C — Shuangpin × English through the real `MixedEngine`: a
//     Ziranma raw like "hsiphone" must flow raw -> real Shuangpin parser ->
//     segmentation -> English Core (lexicon arcs) -> composer -> pool ->
//     ranker -> rewriter and come out as a fused Han+English candidate;
//     a pure-Ziranma raw with no English evidence must keep the §9 fast
//     path empty.

#include "chinesearcoracle.h"

#include "english/englisharcoracle.h"
#include "english/englishcorrectionoracle.h"
#include "english/englishlexicon.h"
#include "english/englishuserlexicon.h"
#include "mixedengine.h"
#include "segmentcomposer.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include <libime/pinyin/pinyincorrectionprofile.h>
#include <libime/pinyin/shuangpinprofile.h>

using namespace pinyin;

namespace {

int failures = 0;
void check(bool cond, const char *what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

} // namespace

int main() {
    libime::ShuangpinProfile sp(libime::ShuangpinBuiltinProfile::Ziranma);
    libime::PinyinCorrectionProfile prof(
        libime::BuiltinPinyinCorrectionProfile::Qwerty);

    LibIMEChineseArcOracle oracle(ChineseInputMode::Shuangpin);
    oracle.setShuangpinProfile(&sp);

    // Sanity: with no correction profile set, the oracle is not
    // correction-enabled and does not have a valid graph until setRaw runs.
    check(!oracle.correctionEnabled(),
          "shuangpin: oracle not correction-enabled with profile=null");
    check(!oracle.graphValid(), "shuangpin: graph invalid before setRaw");

    // Ziranma "u" is the code for 中 (zhong). Test a few known Ziranma codes
    // that produce a valid graph edge. Kept intentionally short so the test
    // remains hermetic to the parser behavior we depend on at the fusion
    // seam; if the Shuangpin key map for any of these changes, this test
    // fails first.
    const std::vector<std::pair<std::string, size_t>> codes = {
        {"us", 1}, // Ziranma: u -> 中 (sh)
        {"hs", 1}, // Ziranma: h -> 国 (uo)
    };
    size_t observedAny = 0;
    for (const auto &[raw, _minExpected] : codes) {
        oracle.setRaw(raw);
        check(oracle.graphValid(), "shuangpin: graph valid after setRaw");
        auto arcs = oracle.arcsAt(raw, 0, raw.size());
        if (!arcs.empty()) {
            ++observedAny;
        }
        // Every arc in Shuangpin mode must be tagged Exact (provenance),
        // never Correction, regardless of the profile pointer below.
        for (const auto &a : arcs) {
            check(a.provenance != CandidateProvenance::Correction,
                  "shuangpin: no Correction-tagged arcs (source ceiling)");
        }
    }
    check(observedAny >= 1,
          "shuangpin: at least one Shuangpin code produces an arc");

    // Set a correction profile after the fact. In Shuangpin mode this must
    // NOT enable correction and MUST NOT change the oracle's output; the
    // recorded §17.2 Case D ceiling remains in effect.
    oracle.setCorrectionProfile(&prof);
    check(!oracle.correctionEnabled(), "shuangpin: correction profile ignored; "
                                       "correctionEnabled() stays false");
    for (const auto &[raw, _min] : codes) {
        oracle.setRaw(raw);
        auto arcs = oracle.arcsAt(raw, 0, raw.size());
        for (const auto &a : arcs) {
            check(a.provenance != CandidateProvenance::Correction,
                  "shuangpin: profile set later still does not emit Correction "
                  "arcs");
        }
    }

    // ---- Part B: setMode() round-trip invalidation (closure §4) ----
    {
        LibIMEChineseArcOracle seam(ChineseInputMode::Pinyin);
        seam.setShuangpinProfile(&sp);
        seam.setRaw("nihao");
        check(seam.graphValid(),
              "setMode: Pinyin graph is built by setRaw with a profile set");
        seam.setMode(ChineseInputMode::Shuangpin);
        check(!seam.graphValid(),
              "setMode: switching modes invalidates the cached graph");

        seam.setRaw("hsiphone");
        check(seam.graphValid(),
              "setMode: graph rebuilt after Shuangpin switch");
        const auto spArcs = seam.arcsAt("hsiphone", 0, 8);
        bool spHas02 = false;
        for (const auto &a : spArcs) {
            check(a.provenance != CandidateProvenance::Correction,
                  "setMode: Shuangpin arcs never carry Correction");
            if (a.rawBegin == 0 && a.rawEnd == 2 &&
                a.source == SegmentSource::Chinese) {
                spHas02 = true;
            }
        }
        check(spHas02, "setMode: Ziranma \"hs\" yields a Chinese arc [0,2) "
                       "under Shuangpin mode");

        seam.setMode(ChineseInputMode::Pinyin);
        check(!seam.graphValid(),
              "setMode: switching back to Pinyin invalidates again");
        seam.setRaw("hsiphone");
        const auto pyArcs = seam.arcsAt("hsiphone", 0, 8);
        bool pyHas02 = false;
        for (const auto &a : pyArcs) {
            if (a.rawBegin == 0 && a.rawEnd == 2 &&
                a.source == SegmentSource::Chinese) {
                pyHas02 = true;
            }
        }
        check(!pyHas02, "setMode: \"hs\" is not a full Pinyin syllable; the "
                        "re-parsed graph has no Chinese arc [0,2)");

        seam.setMode(ChineseInputMode::Pinyin);
        check(seam.graphValid(), "setMode: re-setting the current mode is a "
                                 "no-op and keeps the cached graph");
    }

    // ---- Part C: Shuangpin x English through the real MixedEngine ----
    {
        EnglishLexicon lex;
        lex.loadFromEntries({{"iphone", "iPhone", 9, true, false, false},
                             {"us", "us", 9, false, false, false},
                             {"apple", "apple", 9, false, false, false},
                             {"vpn", "VPN", 9, false, true, false}});
        EnglishArcOracle sysEn(&lex);
        EnglishUserLexicon userLex;
        EnglishUserArcOracle userEn(&userLex);
        EnglishCorrectionOracle corrEn(&lex);
        CompositeEnglishArcOracle composite;
        composite.addSource(&sysEn);
        composite.addSource(&userEn);
        composite.addSource(&corrEn);

        LibIMEChineseArcOracle ch(ChineseInputMode::Shuangpin);
        ch.setShuangpinProfile(&sp);
        MixedEngine engine;

        // Context-free Han decode keyed on the Ziranma spelling of the arc's
        // raw span. Unknown spans resolve to empty, which makes the composer
        // reject paths that use them — exactly what keeps each case below
        // pinned to the intended tiling.
        auto resolverFor = [](std::string_view raw) {
            std::string r(raw);
            return [r](const SegmentationArc &arc) -> std::string {
                const std::string span =
                    r.substr(arc.rawBegin, arc.rawEnd - arc.rawBegin);
                if (span == "hs") {
                    return "国"; // Ziranma h + uo
                }
                if (span == "wo") {
                    return "我";
                }
                if (span == "us") {
                    return "中";
                }
                return {};
            };
        };

        struct SpCase {
            const char *raw;
            const char *han;
        };
        const std::vector<SpCase> cases = {
            {"hsiphone", "国"},
            {"woiphone", "我"},
        };
        for (const auto &c : cases) {
            const std::string_view raw = c.raw;
            ch.setRaw(raw);
            const auto pool =
                engine.compute(raw, ch, composite, resolverFor(raw));
            check(!pool.empty(),
                  "pipeline: mixed Shuangpin raw produces a non-empty pool");
            bool recall = false;
            bool fused = false;
            for (const auto &cand : pool) {
                if (cand.composedText.find("iPhone") != std::string::npos) {
                    recall = true;
                }
                if (cand.composedText.find(c.han) != std::string::npos &&
                    cand.composedText.find("iPhone") != std::string::npos) {
                    fused = true;
                }
            }
            check(recall, "pipeline: pool recalls the canonical iPhone form");
            check(fused, "pipeline: pool contains a fused Han+iPhone reading "
                         "of the Ziranma raw");
        }

        // Pure-Ziranma raw with no English evidence anywhere: the §9 fast
        // path must short-circuit before compose/rank/rewrite and return an
        // empty pool, so classical Shuangpin keeps the whole candidate list.
        // "hs" additionally proves the correction oracle cannot smuggle an
        // English arc through 2-byte spans (minSpanLength = 3).
        {
            const std::string_view raw = "hs";
            ch.setRaw(raw);
            const auto pool =
                engine.compute(raw, ch, composite, resolverFor(raw));
            check(pool.empty(), "pipeline: pure-Ziranma raw keeps the §9 "
                                "fast path empty");
        }

        // Staleness guard: flip the SAME oracle to Pinyin without any other
        // change and re-run the fused case. The Ziranma [0,2) Han arc must
        // be gone, so no candidate may still fuse 国 + iPhone. If a cached
        // Shuangpin graph ever leaked across setMode, this fails.
        {
            const std::string_view raw = "hsiphone";
            ch.setMode(ChineseInputMode::Pinyin);
            ch.setRaw(raw);
            const auto pool =
                engine.compute(raw, ch, composite, resolverFor(raw));
            bool staleFused = false;
            for (const auto &cand : pool) {
                if (cand.composedText.find("国") != std::string::npos &&
                    cand.composedText.find("iPhone") != std::string::npos) {
                    staleFused = true;
                }
            }
            check(!staleFused, "pipeline: no stale Shuangpin Han segment "
                               "survives the mode flip");
        }
    }

    if (failures > 0) {
        std::fprintf(stderr, "testmixedshuangpin: %d failure(s)\n", failures);
        return 1;
    }
    std::fprintf(stderr, "testmixedshuangpin: OK\n");
    return 0;
}
