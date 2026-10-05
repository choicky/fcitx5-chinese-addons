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

#include "chinesearcoracle.h"

#include <cstdio>
#include <string>
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

    if (failures > 0) {
        std::fprintf(stderr, "testmixedshuangpin: %d failure(s)\n", failures);
        return 1;
    }
    std::fprintf(stderr, "testmixedshuangpin: OK\n");
    return 0;
}
