/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _FCITX_PINYIN_MIXED_HANWORDRESOLVER_H_
#define _FCITX_PINYIN_MIXED_HANWORDRESOLVER_H_

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "mixedsegmentation.h"
#include "segmentcomposer.h"

namespace libime {
class PinyinDictionary;
}

namespace pinyin {

// Turns a Chinese `SegmentationArc` (populated by LibIMEChineseArcOracle with
// empty `resolvedOutput`) into a concrete Han surface string. The primary
// implementation delegates to PinyinDictionary::matchWords with the
// single-syllable encoding of the arc's raw span; per-candidate Han choice
// honours source-local rank (a top-N pick) so the unified pool sees parallel
// arcs over the same span with distinct ranks, per §19.
//
// A null dictionary or an unmatched syllable returns empty, which the
// composer rejects (never silently composes to an empty string).
class HanWordResolver {
public:
    explicit HanWordResolver(const libime::PinyinDictionary *dict);
    ~HanWordResolver();

    // Resolve one arc to its Han output. Uses `arc.sourceLocalRank` as the
    // top-N pick (0 = best). Falls back to the best hit when rank exceeds
    // the number of matchWords results.
    std::string resolve(const SegmentationArc &arc,
                        std::string_view rawSyllable) const;

    // Convenience: an ArcResolver lambda ready to plug into SegmentComposer.
    // Captures `this`; the returned function's lifetime must not outlive the
    // resolver.
    ArcResolver asArcResolver(std::string_view raw) const;

    bool valid() const { return dict_ != nullptr; }

private:
    const libime::PinyinDictionary *dict_;
};

} // namespace pinyin

#endif // _FCITX_PINYIN_MIXED_HANWORDRESOLVER_H_
