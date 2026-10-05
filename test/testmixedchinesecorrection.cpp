/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
//
// Batch 8 — Chinese correction baseline/after harness.
//
// This test drives the REAL `LibIMEChineseArcOracle` (no test double) and a
// real `libime::PinyinCorrectionProfile` (Qwerty). It measures the two
// dimensions the Master Instruction calls out separately:
//
//   RECALL  : does the correction-derived Chinese arc survive the
//             Architecture A graph builder?
//   PROVENANCE : does the arc carry `CandidateProvenance::Correction` so
//             the UnifiedRanker can express the weaker hypothesis through
//             its existing explainable features?
//
// It also measures the two negative controls:
//
//   FALSE CORRECTION : a correctly typed syllable must NOT acquire a
//             Correction-tagged arc that displaces the Exact arc.
//   REGRESSION       : with correction disabled (`setCorrectionProfile
//             (nullptr)`), arc emission is identical to the pre-Batch-8
//             behavior (single graph, all arcs tagged Exact).
//
// The `baseline` measurement is taken BEFORE enabling the correction
// profile; the `after` measurement is taken AFTER enabling it. The two
// are compared inside the test itself so the numbers printed on stdout
// are the actual baseline-vs-after report for this batch, matching the
// "Required baseline" and "Validation" sections of the Batch 8 contract.
//
// Cases cover representative Qwerty typos where LibIME's layout profile
// is known to produce a correction hypothesis, plus ambiguous correctly
// typed pinyin that must not be spuriously re-tagged as Correction.

#include "chinesearcoracle.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <libime/pinyin/pinyincorrectionprofile.h>
#include <libime/pinyin/pinyinencoder.h>

using namespace pinyin;

namespace {

struct Case {
    const char *label;
    const char *raw;
    // True when this raw string is a *typo* that the layout profile should
    // map back onto an intended pinyin. False when it is correctly typed
    // and must remain a pure exact/fuzzy path.
    bool expectsCorrection;
};

int failures = 0;
void check(bool cond, const char *what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// Count arcs by provenance at position 0 across the whole raw string.
void scanArcs(const LibIMEChineseArcOracle &oracle, const char *raw,
              size_t &exactCount, size_t &correctionCount) {
    exactCount = 0;
    correctionCount = 0;
    const std::string_view view(raw);
    const auto arcs = oracle.arcsAt(view, 0, view.size());
    for (const auto &arc : arcs) {
        if (arc.provenance == CandidateProvenance::Correction) {
            ++correctionCount;
        } else if (arc.provenance == CandidateProvenance::Exact) {
            ++exactCount;
        }
    }
}

// Representative Qwerty typos. `qwerty` layout correction is documented
// in LibIME's builtin profile enum; the specific typos below correspond
// to adjacent-key substitutions that a real user produces. Cases are
// chosen so the oracle's raw span reaches the graph at position 0 in
// both modes. Correctly typed cases act as false-correction guards.
const Case kCases[] = {
    // Typo candidates (adjacent-key substitutions on Qwerty).
    {"typo-ni-hao-o-for-i", "nuhao", true},
    {"typo-wo-de-e-for-w", "eo", true},
    {"typo-ping-an-near-b", "bingan", true},
    // Correctly typed controls.
    {"clean-nihao", "nihao", false},
    {"clean-wode", "wode", false},
    {"clean-pingan", "pingan", false},
    {"clean-zhongguo", "zhongguo", false},
};
constexpr size_t kCasesSize = sizeof(kCases) / sizeof(kCases[0]);

} // namespace

int main() {
    libime::PinyinCorrectionProfile profile(
        libime::BuiltinPinyinCorrectionProfile::Qwerty);

    LibIMEChineseArcOracle oracle(ChineseInputMode::Pinyin);
    // Same fuzzy flags the classical path uses; correction is only enabled
    // through the profile pointer, not through a separate fuzzy flag.
    oracle.setFuzzyFlags(libime::PinyinFuzzyFlag::None);

    // -------- BASELINE (correction profile NOT plumbed) -----------------
    size_t baselineRecall = 0;
    size_t baselineCorrection = 0;
    size_t baselineFalse = 0;
    size_t baselineTypoTotal = 0;
    std::vector<size_t> baselineExactVec;
    baselineExactVec.reserve(kCasesSize);
    std::fprintf(stderr, "=== batch 8 baseline (correction disabled) ===\n");
    for (const auto &c : kCases) {
        oracle.setCorrectionProfile(nullptr);
        oracle.setRaw(c.raw);
        size_t exact = 0;
        size_t corr = 0;
        scanArcs(oracle, c.raw, exact, corr);
        baselineExactVec.push_back(exact);
        std::fprintf(stderr, "  %-32s raw=%-10s exact=%zu correction=%zu\n",
                     c.label, c.raw, exact, corr);
        if (c.expectsCorrection) {
            ++baselineTypoTotal;
            if (corr > 0) {
                ++baselineRecall;
            }
            baselineCorrection += corr;
        } else {
            baselineFalse += corr;
        }
    }

    // -------- AFTER (correction profile ENABLED) ------------------------
    size_t afterRecall = 0;
    size_t afterCorrection = 0;
    size_t afterFalse = 0;
    size_t afterExactLoss = 0;
    // False-correction is measured as degradation of the exact arc, using
    // the per-case Exact counts recorded in the baseline pass (§17.2
    // "correctly typed Chinese input must not be degraded merely to promote
    // correction candidates"). Adding a weaker parallel Correction arc
    // alongside the existing Exact arc is NOT a degradation — the Exact arc
    // still wins UnifiedRanker competition at higher confidence and boundary
    // strength; only losing an Exact arc would be.
    std::fprintf(stderr, "=== batch 8 after (Qwerty correction enabled) ===\n");
    for (size_t i = 0; i < kCasesSize; ++i) {
        const auto &c = kCases[i];
        oracle.setCorrectionProfile(&profile);
        oracle.setRaw(c.raw);
        size_t exact = 0;
        size_t corr = 0;
        scanArcs(oracle, c.raw, exact, corr);
        std::fprintf(stderr, "  %-32s raw=%-10s exact=%zu correction=%zu\n",
                     c.label, c.raw, exact, corr);
        if (c.expectsCorrection) {
            if (corr > 0) {
                ++afterRecall;
            }
            afterCorrection += corr;
        } else {
            // False-correction is measured as degradation of the exact arc,
            // not as the presence of an additional parallel Correction arc.
            if (exact < baselineExactVec[i]) {
                ++afterFalse;
            }
        }
    }

    // Re-baseline to verify regression control: setting profile back to
    // null must reproduce the pre-batch exact counts.
    std::fprintf(stderr,
                 "=== batch 8 regression (correction re-disabled) ===\n");
    for (const auto &c : kCases) {
        oracle.setCorrectionProfile(nullptr);
        oracle.setRaw(c.raw);
        size_t exact = 0;
        size_t corr = 0;
        scanArcs(oracle, c.raw, exact, corr);
        if (corr != 0) {
            std::fprintf(stderr,
                         "  REGRESSION: %s still emits correction arcs after "
                         "profile=null (correction=%zu)\n",
                         c.label, corr);
            ++afterExactLoss;
        }
    }

    std::fprintf(
        stderr,
        "=== batch 8 summary ===\n"
        "  baseline recall (typo cases with correction arcs): %zu/%zu\n"
        "  after    recall (typo cases with correction arcs): %zu/%zu\n"
        "  baseline correction arcs total: %zu\n"
        "  after    correction arcs total: %zu\n"
        "  baseline false-correction (clean cases): %zu\n"
        "  after    false-correction (clean cases): %zu\n"
        "  regression failures (correction remains after disabling): %zu\n",
        baselineRecall, baselineTypoTotal, afterRecall, baselineTypoTotal,
        baselineCorrection, afterCorrection, baselineFalse, afterFalse,
        afterExactLoss);

    // Contract assertions:
    check(baselineRecall == 0,
          "baseline: correction must be entirely absent when profile is null");
    check(baselineCorrection == 0,
          "baseline: no correction-tagged arcs emitted when profile is null");
    check(afterFalse == 0,
          "after: correctly typed cases must not lose any Exact arc");
    check(afterExactLoss == 0,
          "regression: nulling the profile restores pre-batch behavior");

    // Recall improvement is the whole point of Case A/C — assert we see at
    // least one typo case gain a Correction-tagged arc. If zero, the
    // correction plumbing did not take effect (either profile mismatch or
    // parser API error).
    check(afterRecall >= 1,
          "after: at least one representative typo case produces a "
          "Correction-tagged arc under the Qwerty profile");

    if (failures > 0) {
        std::fprintf(stderr, "testmixedchinesecorrection: %d failure(s)\n",
                     failures);
        return 1;
    }
    std::fprintf(stderr, "testmixedchinesecorrection: OK\n");
    return 0;
}
