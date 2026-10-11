/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _FCITX_PINYIN_MIXED_EXPERIMENT_H_
#define _FCITX_PINYIN_MIXED_EXPERIMENT_H_

// PoC-C1 (EXPERIMENTAL — NOT ACCEPTED PRODUCT BEHAVIOR).
//
// Centralized, deterministic switches for the case-gated short Latin
// admission experiment. The arm is read exactly once from the process
// environment at static initialization; there is no product UI, no settings
// surface, no per-key lookup. Default (unset) is BASELINE: every gate below
// behaves byte-identically to the accepted M2+/r4 product path.
//
//   FCITX_MIXED_SHORT_LATIN_ARM = unset|"baseline"  -> BASELINE (default)
//                                   "a"             -> ARM_A
//                                   "b"             -> ARM_B (implies ARM_A)
//   FCITX_MIXED_SHORT_LATIN_DIAG = "1"              -> enable the
//       OFF-by-default, behavior-neutral diagnostic counters; aggregates are
//       dumped once to stderr at process exit.
//
// ARM_A (caseGatedShortAdmission): contiguous [A-Z]{1,2} spans may pass
// through the EXISTING English sourcing (exact/canonical/literal/completion
// tiers, unchanged scoring) even though they sit below Gate A's
// minSpanLength, and the §9 lead rule (Gate D) treats such an explicitly
// admitted span as lead evidence instead of re-rejecting it for <3 bytes.
//
// ARM_B (uppercaseShortRawOov, strict superset of ARM_A — one pipeline, one
// extra switch): additionally emits ONE raw/OOV literal hypothesis per
// admitted [A-Z]{1,2} span: the user's surface verbatim, no lexicon hit
// required, deduplicated against an identical already-emitted arc.

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace pinyin {

struct ShortLatinExperiment {
    bool caseGatedShortAdmission = false;
    bool uppercaseShortRawOov = false;
    bool diagnostics = false;
};

inline const ShortLatinExperiment &shortLatinExperiment() {
    static const ShortLatinExperiment exp = [] {
        ShortLatinExperiment out;
        const char *arm = std::getenv("FCITX_MIXED_SHORT_LATIN_ARM");
        const std::string_view v =
            arm ? std::string_view(arm) : std::string_view();
        if (v == "a") {
            out.caseGatedShortAdmission = true;
        } else if (v == "b") {
            out.caseGatedShortAdmission = true;
            out.uppercaseShortRawOov = true;
        }
        const char *diag = std::getenv("FCITX_MIXED_SHORT_LATIN_DIAG");
        out.diagnostics = diag != nullptr && std::string_view(diag) == "1";
        return out;
    }();
    return exp;
}

// The single admission shape of PoC-C1: len in {1,2} and every byte of
// raw[begin, begin+len) a contiguous uppercase ASCII letter.
inline bool isShortUppercaseSpan(std::string_view raw, size_t begin,
                                 size_t len) {
    if (len == 0 || len > 2 || begin + len > raw.size()) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        const char c = raw[begin + i];
        if (c < 'A' || c > 'Z') {
            return false;
        }
    }
    return true;
}

namespace shortlatindiag {

inline std::atomic<unsigned long long> shortArcsAdmitted{0};
inline std::atomic<unsigned long long> englishEdges{0};
inline std::atomic<unsigned long long> totalEdges{0};
inline std::atomic<unsigned long long> retentionKept{0};
inline std::atomic<unsigned long long> retentionDropped{0};
inline std::atomic<unsigned long long> poolInsertAttempted{0};
inline std::atomic<unsigned long long> poolInsertAccepted{0};
inline std::atomic<unsigned long long> poolEvicted{0};
inline std::atomic<unsigned long long> poolEvictedChineseOnly{0};

inline void dump() {
    if (!shortLatinExperiment().diagnostics) {
        return;
    }
    const auto &exp = shortLatinExperiment();
    std::fprintf(stderr,
                 "[shortlatin-diag] arm=%s shortArcsAdmitted=%llu "
                 "englishEdges=%llu totalEdges=%llu retentionKept=%llu "
                 "retentionDropped=%llu poolInsertAttempted=%llu "
                 "poolInsertAccepted=%llu poolEvicted=%llu "
                 "poolEvictedChineseOnly=%llu\n",
                 exp.uppercaseShortRawOov
                     ? "b"
                     : (exp.caseGatedShortAdmission ? "a" : "baseline"),
                 shortArcsAdmitted.load(), englishEdges.load(),
                 totalEdges.load(), retentionKept.load(),
                 retentionDropped.load(), poolInsertAttempted.load(),
                 poolInsertAccepted.load(), poolEvicted.load(),
                 poolEvictedChineseOnly.load());
}

inline void ensureDumpRegistered() {
    static const bool registered = [] {
        std::atexit(+[] { dump(); });
        return true;
    }();
    (void)registered;
}

// Behavior-neutral counter bump: no-op unless diagnostics are enabled.
inline void count(std::atomic<unsigned long long> &c,
                  unsigned long long n = 1) {
    if (!shortLatinExperiment().diagnostics) {
        return;
    }
    ensureDumpRegistered();
    c.fetch_add(n, std::memory_order_relaxed);
}

} // namespace shortlatindiag

} // namespace pinyin

#endif // _FCITX_PINYIN_MIXED_EXPERIMENT_H_
