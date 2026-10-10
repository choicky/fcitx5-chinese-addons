/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "mixedsegmentation.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

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

// Retention classes: 0 = no English run yet (necessarily ending Chinese);
// for 1 <= r < cap: (r, ending-Chinese) then (r, ending-English); the cap
// class index 2*cap-1 collapses every prefix at or above cap runs regardless
// of last source, because once capped no continuation can leave the cap
// class, so the bit no longer distinguishes class trajectories.
size_t retentionClassCount(size_t cap) { return cap == 0 ? 1 : 2 * cap; }

size_t retentionClassIndex(const SegmentationPath &path, size_t cap) {
    if (cap == 0) {
        return 0;
    }
    size_t runs = 0;
    bool lastEnglish = false;
    for (const auto &arc : path.arcs) {
        const bool english = (arc.source == SegmentSource::English);
        if (english && !lastEnglish) {
            ++runs;
        }
        lastEnglish = english;
    }
    if (runs == 0) {
        return 0;
    }
    if (runs >= cap) {
        return 2 * cap - 1;
    }
    return 2 * runs - (lastEnglish ? 0 : 1);
}

using PathClasses = std::vector<std::vector<SegmentationPath>>;

// Partition a frontier's arrivals into retention classes and keep, within
// each class, the beamWidth cheapest prefixes. stable_sort keeps ties in
// arrival order, so the window outcome is deterministic across runs.
PathClasses classWindow(std::vector<SegmentationPath> &bucket, size_t cap,
                        size_t beamWidth) {
    const size_t numClasses = retentionClassCount(cap);
    PathClasses classes(numClasses);
    if (numClasses == 1) {
        classes[0].swap(bucket);
    } else {
        for (auto &path : bucket) {
            classes[retentionClassIndex(path, cap)].push_back(std::move(path));
        }
        bucket.clear();
    }
    const auto byCost = [](const SegmentationPath &a,
                           const SegmentationPath &b) {
        return a.cost < b.cost;
    };
    for (auto &cls : classes) {
        std::stable_sort(cls.begin(), cls.end(), byCost);
        if (cls.size() > beamWidth) {
            cls.resize(beamWidth);
        }
    }
    return classes;
}

// The ordered English spans of a path: its maximal consecutive-English runs as
// raw intervals. Two completions with the same shape are the same mixed
// product reading with a different decode of the Chinese spans (or a different
// surface of one English hit); two completions with different shapes are
// different English hypotheses.
using EnglishShape = std::vector<std::pair<size_t, size_t>>;

EnglishShape englishShapeOf(const SegmentationPath &path) {
    EnglishShape shape;
    for (const auto &arc : path.arcs) {
        if (arc.source == SegmentSource::English) {
            shape.emplace_back(arc.rawBegin, arc.rawEnd);
        }
    }
    return shape;
}

// The per-shape sibling quota is the header constant kTerminalSiblingsPerShape,
// shared with the M2+r4 frontier guarantee.

// The ordered English surfaces of a path: what its English spans resolve to.
// Two paths with the same surfaces differ only in how the Chinese spans were
// decoded; the canonical respelling and the literal typed surface are the two
// surfaces D071 requires to coexist.
std::vector<std::string> englishSurfacesOf(const SegmentationPath &path) {
    std::vector<std::string> surfaces;
    for (const auto &arc : path.arcs) {
        if (arc.source == SegmentSource::English) {
            surfaces.push_back(arc.resolvedOutput);
        }
    }
    return surfaces;
}

const auto pathByCost = [](const SegmentationPath &a,
                           const SegmentationPath &b) {
    return a.cost < b.cost;
};

// Shape-windowed terminal selection (M2+). Retention by cost alone is not
// enough when the Chinese side is not frozen to one reading per span: every
// reading of one frame shares an identical arc cost, so the B cheapest members
// of a retention class are B variants of ONE English hypothesis and every other
// English hypothesis dies in the terminal window before compose. Grouping by
// English shape keeps those variants inside their hypothesis instead of
// competing across hypotheses. Inside a shape the cheapest members are ordered
// so that each distinct English surface is represented before any surface is
// repeated: the D071 literal sibling is then a survivor of the window rather
// than a cost tie away from disappearing, and no arc cost or score moves. Each
// shape contributes at most kTerminalSiblingsPerShape members, shapes are
// ordered by their cheapest member, and the zero-English shape goes last for
// the same reason the class-keyed selection defers pure-Chinese completions —
// the classical decoder already supplies them after the pool. Every sort is
// stable, so the caller's total order decides ties.
std::vector<SegmentationPath>
selectTerminalByShape(std::vector<SegmentationPath> arrivals, size_t beamWidth,
                      size_t topK) {
    std::map<EnglishShape, std::vector<SegmentationPath>> groups;
    for (auto &path : arrivals) {
        groups[englishShapeOf(path)].push_back(std::move(path));
    }
    const size_t perShape =
        std::min(std::max<size_t>(1, beamWidth), kTerminalSiblingsPerShape);
    std::vector<decltype(groups)::iterator> ordered;
    ordered.reserve(groups.size());
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        auto &paths = it->second;
        std::stable_sort(paths.begin(), paths.end(), pathByCost);
        std::vector<SegmentationPath> ranked;
        ranked.reserve(paths.size());
        std::vector<SegmentationPath> repeated;
        std::set<std::vector<std::string>> seen;
        for (auto &path : paths) {
            if (seen.insert(englishSurfacesOf(path)).second) {
                ranked.push_back(std::move(path));
            } else {
                repeated.push_back(std::move(path));
            }
        }
        for (auto &path : repeated) {
            ranked.push_back(std::move(path));
        }
        if (ranked.size() > perShape) {
            ranked.resize(perShape);
        }
        it->second = std::move(ranked);
        ordered.push_back(it);
    }
    std::stable_sort(
        ordered.begin(), ordered.end(), [](const auto &a, const auto &b) {
            const bool ae = a->first.empty(), be = b->first.empty();
            if (ae != be) {
                return !ae;
            }
            return a->second.front().cost < b->second.front().cost;
        });
    std::vector<SegmentationPath> selected;
    for (const auto &it : ordered) {
        for (auto &path : it->second) {
            if (selected.size() >= topK) {
                break;
            }
            selected.push_back(std::move(path));
        }
        if (selected.size() >= topK) {
            break;
        }
    }
    std::stable_sort(selected.begin(), selected.end(), pathByCost);
    return selected;
}
} // namespace

MixedSegmentationSearch::MixedSegmentationSearch()
    : MixedSegmentationSearch(Config{}) {}

MixedSegmentationSearch::MixedSegmentationSearch(Config config)
    : config_(config) {}

double MixedSegmentationSearch::arcCostOf(const SegmentationArc &arc,
                                          float weakBoundaryWeight) {
    const double base = 1.0 - static_cast<double>(clamp01(arc.confidence));
    const double weak =
        static_cast<double>(weakBoundaryWeight) *
        (1.0 - static_cast<double>(clamp01(arc.boundaryConfidence)));
    return base + weak;
}

double MixedSegmentationSearch::arcCost(const SegmentationArc &arc) const {
    return arcCostOf(arc, config_.weakBoundaryWeight);
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
    const size_t cap = config_.englishSegmentCap;

    for (size_t i = begin; i < n; ++i) {
        if (beam[i].empty()) {
            continue;
        }
        // All edges point forward, so by now every predecessor has expanded
        // into beam[i]; prune to the best beamWidth partial paths per
        // retention class (cross-class pruning would violate the dominance
        // equivalence: a cheaper 0-segment prefix does not dominate the
        // 1-segment prefix that alone can open the next switch).
        auto classes = classWindow(beam[i], cap, beamWidth);
        for (const auto &cls : classes) {
            beam[i].insert(beam[i].end(), cls.begin(), cls.end());
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

    result = selectTerminal(std::move(beam[n]), cap, beamWidth,
                            std::max<size_t>(1, config_.topK));
    return result;
}

std::vector<SegmentationPath> MixedSegmentationSearch::selectTerminal(
    std::vector<SegmentationPath> arrivals, size_t cap, size_t beamWidth,
    size_t topK, bool reserveEnglishShapes) {
    std::vector<SegmentationPath> result;
    if (reserveEnglishShapes) {
        return selectTerminalByShape(std::move(arrivals), beamWidth, topK);
    }
    // Terminal window: same per-class retention, then top-K selection with a
    // single reachability reservation: the head (cheapest member) of the
    // HIGHEST non-empty English-bearing class. The product contract promises
    // reachability of the maximum switch count, so exactly one reservation is
    // required: without it the terminal fill takes only zero-segment
    // completions and every English reading dies before compose. Reserving
    // MORE classes is unsound — each additional reserved slot evicts
    // same-class tied reading variants that the plain cost order keeps, and
    // those variants carry the classical-lead Chinese readings the committed
    // MIXEDHANQ cases assert. When no English class completes, selection is
    // the legacy cost order.
    auto finalClasses = classWindow(arrivals, cap, beamWidth);
    std::vector<SegmentationPath> pool;
    std::vector<std::pair<size_t, size_t>> spans; // [begin,end) per class
    for (const auto &cls : finalClasses) {
        spans.emplace_back(pool.size(), pool.size() + cls.size());
        pool.insert(pool.end(), cls.begin(), cls.end());
    }
    std::vector<bool> taken(pool.size(), false);
    std::vector<SegmentationPath> selected;
    if (cap > 0) {
        for (size_t c = spans.size(); c-- > 1;) {
            if (spans[c].second != spans[c].first) {
                const size_t idx = spans[c].first; // class head = cheapest
                selected.push_back(std::move(pool[idx]));
                taken[idx] = true;
                break;
            }
        }
    }
    while (selected.size() < topK) {
        // Fill precedence: zero-segment (pure-Chinese) members take slots
        // only after every English-bearing class member has been taken.
        // The compose pool exists to deliver mixed evidence — pure-Chinese
        // tilings are redundantly covered by the classical decoder that the
        // placement seam appends after the pool — and arc costs are summed
        // WITHOUT cross-source normalization, so a single long Chinese arc
        // ties cheaply against every mixed completion; plain cost order
        // would therefore re-create the legacy cost-blind crowding-out
        // inside the terminal selection (tied-cheap pure completions evict
        // the English-bearing completion from top-K even though class
        // retention kept it alive).
        // With cap == 0 (or no mixed completion) every pool member is in
        // class 0 and this is exactly the legacy cost order.
        size_t best = pool.size();
        for (int pass = 0; pass < 2 && best == pool.size(); ++pass) {
            for (size_t idx = 0; idx < pool.size(); ++idx) {
                const bool zeroSegment =
                    (idx >= spans[0].first && idx < spans[0].second);
                if ((pass == 0) == zeroSegment) {
                    continue;
                }
                if (taken[idx]) {
                    continue;
                }
                if (best == pool.size() || pool[idx].cost < pool[best].cost) {
                    best = idx;
                }
            }
        }
        if (best == pool.size()) {
            break;
        }
        taken[best] = true;
        selected.push_back(std::move(pool[best]));
    }
    std::stable_sort(selected.begin(), selected.end(),
                     [](const SegmentationPath &a, const SegmentationPath &b) {
                         return a.cost < b.cost;
                     });
    result = std::move(selected);
    return result;
}

} // namespace pinyin
