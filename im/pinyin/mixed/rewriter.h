/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _FCITX_PINYIN_MIXED_REWRITER_H_
#define _FCITX_PINYIN_MIXED_REWRITER_H_

#include <string>
#include <vector>

#include "mixedcompositionstate.h"
#include "segmentcomposer.h"

namespace pinyin {

// Rewriter applies deterministic surface-text transformations AFTER
// ranking. It is deliberately separate from UnifiedRanker (§22: "rewriting
// is not semantic ranking") so that a change to spacing or casing never
// silently reorders the pool. Rewriter is idempotent on already-rewritten
// output.
//
// V1 supports exactly one behavior switch: auto-spacing at a Chinese↔English
// boundary (§23). Default is OFF, preserving existing verified behavior of
// the shipped Pinyin addon (which inserts no auto space in mixed runs).
// The switch is exposed here so Batch 9 can wire it to a config option;
// nothing in the ranker reads this flag.
struct RewriterConfig {
    bool autoSpaceAtBoundary = false; // §23: default OFF
    // Filler used when autoSpaceAtBoundary is true. Kept as an ASCII space
    // by default; overridable so a future Romanization style is not baked
    // into the rewriter's semantics.
    std::string boundarySeparator = " ";
};

class Rewriter {
public:
    Rewriter();
    explicit Rewriter(RewriterConfig config);

    // Rewrite a candidate's composedText in place, returning the same
    // candidate with `composedText` transformed. Also emits the rewritten
    // form through the out-parameter for the fused candidate view.
    UnifiedCandidate rewrite(const UnifiedCandidate &in) const;

    // Rewrite a ranked pool. Preserves order and count; only the composed
    // text is transformed.
    std::vector<UnifiedCandidate>
    rewritePool(const std::vector<UnifiedCandidate> &ranked) const;

    const RewriterConfig &config() const { return config_; }

private:
    RewriterConfig config_;
};

} // namespace pinyin

#endif // _FCITX_PINYIN_MIXED_REWRITER_H_
