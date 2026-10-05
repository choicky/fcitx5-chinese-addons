/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "mixedsegmentation.h"

#include <algorithm>

namespace pinyin {

namespace {
float clamp01(float v) {
    if (v < 0.0F) {
        return 0.0F;
    }
    if (v > 1.0F) {
        return 1.0F;
    }
    return v;
}
} // namespace

MixedSegmentationSearch::MixedSegmentationSearch()
    : MixedSegmentationSearch(Config{}) {}

MixedSegmentationSearch::MixedSegmentationSearch(Config config)
    : config_(config) {}

double MixedSegmentationSearch::arcCost(const SegmentationArc &arc) const {
    const double base = 1.0 - static_cast<double>(clamp01(arc.confidence));
    const double weak =
        static_cast<double>(config_.weakBoundaryWeight) *
        (1.0 - static_cast<double>(clamp01(arc.boundaryConfidence)));
    return base + weak;
}

std::vector<SegmentationPath>
MixedSegmentationSearch::search(std::string_view raw,
                                const IChineseArcOracle &chinese,
                                const IEnglishArcOracle &english) const {
    return searchFrom(raw, 0, chinese, english);
}

std::vector<SegmentationPath>
MixedSegmentationSearch::searchFrom(std::string_view raw, size_t begin,
                                    const IChineseArcOracle &chinese,
                                    const IEnglishArcOracle &english) const {
    std::vector<SegmentationPath> result;
    const size_t n = raw.size();
    if (begin > n) {
        return result;
    }
    if (begin == n) {
        result.push_back(SegmentationPath{});
        return result;
    }

    std::vector<std::vector<SegmentationPath>> beam(n + 1);
    beam[begin].push_back(SegmentationPath{});

    const size_t beamWidth = std::max<size_t>(1, config_.beamWidth);

    for (size_t i = begin; i < n; ++i) {
        if (beam[i].empty()) {
            continue;
        }
        // All edges point forward, so by now every predecessor has expanded
        // into beam[i]; prune to the best beamWidth partial paths.
        std::sort(beam[i].begin(), beam[i].end(),
                  [](const SegmentationPath &a, const SegmentationPath &b) {
                      return a.cost < b.cost;
                  });
        if (beam[i].size() > beamWidth) {
            beam[i].resize(beamWidth);
        }

        const std::vector<SegmentationArc> arcs = [&] {
            auto a = chinese.arcsAt(raw, i, config_.maxArcSpan);
            auto e = english.arcsAt(raw, i, config_.maxArcSpan);
            a.insert(a.end(), e.begin(), e.end());
            return a;
        }();

        for (const SegmentationArc &arc : arcs) {
            if (arc.rawBegin != i || arc.rawEnd <= i || arc.rawEnd > n) {
                continue;
            }
            const double step = arcCost(arc);
            for (const SegmentationPath &partial : beam[i]) {
                SegmentationPath extended = partial;
                extended.arcs.push_back(arc);
                extended.cost = partial.cost + step;
                beam[arc.rawEnd].push_back(std::move(extended));
            }
        }
    }

    std::sort(beam[n].begin(), beam[n].end(),
              [](const SegmentationPath &a, const SegmentationPath &b) {
                  return a.cost < b.cost;
              });
    const size_t topK = std::max<size_t>(1, config_.topK);
    if (beam[n].size() > topK) {
        beam[n].resize(topK);
    }
    result = std::move(beam[n]);
    return result;
}

} // namespace pinyin
