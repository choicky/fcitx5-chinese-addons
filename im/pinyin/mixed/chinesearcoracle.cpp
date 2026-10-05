/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "chinesearcoracle.h"

#include <algorithm>
#include <set>
#include <string>
#include <utility>

#include <libime/core/segmentgraph.h>
#include <libime/pinyin/pinyinencoder.h>
#include <libime/pinyin/shuangpinprofile.h>

namespace pinyin {

struct LibIMEChineseArcOracle::Private {
    ChineseInputMode mode = ChineseInputMode::Pinyin;
    const libime::ShuangpinProfile *sp = nullptr;
    libime::PinyinFuzzyFlags flags = libime::PinyinFuzzyFlag::None;
    std::string lastRaw;
    std::unique_ptr<libime::SegmentGraph> graph;
    // Adjacency: for each begin position, the sorted set of end positions
    // reachable by a single graph edge (syllable). Built once per setRaw().
    std::vector<std::vector<size_t>> endsFromBegin;
};

LibIMEChineseArcOracle::LibIMEChineseArcOracle(ChineseInputMode mode)
    : d_(std::make_unique<Private>()) {
    d_->mode = mode;
}

LibIMEChineseArcOracle::~LibIMEChineseArcOracle() = default;

void LibIMEChineseArcOracle::setShuangpinProfile(
    const libime::ShuangpinProfile *profile) {
    d_->sp = profile;
    // Invalidate any cached graph; next setRaw will rebuild.
    d_->graph.reset();
    d_->endsFromBegin.clear();
    d_->lastRaw.clear();
}

void LibIMEChineseArcOracle::setFuzzyFlags(libime::PinyinFuzzyFlags flags) {
    d_->flags = flags;
    d_->graph.reset();
    d_->endsFromBegin.clear();
    d_->lastRaw.clear();
}

bool LibIMEChineseArcOracle::graphValid() const { return d_->graph != nullptr; }

void LibIMEChineseArcOracle::setRaw(std::string_view raw) {
    const std::string next(raw);
    if (next == d_->lastRaw && d_->graph) {
        return;
    }
    d_->lastRaw = next;
    d_->graph.reset();
    d_->endsFromBegin.clear();
    if (next.empty()) {
        return;
    }
    try {
        if (d_->mode == ChineseInputMode::Shuangpin) {
            if (d_->sp == nullptr) {
                return;
            }
            d_->graph = std::make_unique<libime::SegmentGraph>(
                libime::PinyinEncoder::parseUserShuangpin(next, *d_->sp,
                                                          d_->flags));
        } else {
            d_->graph = std::make_unique<libime::SegmentGraph>(
                libime::PinyinEncoder::parseUserPinyin(next, d_->flags));
        }
    } catch (...) {
        // Any LibIME parser error surfaces as an empty graph so the mixed
        // pipeline degrades to classical Chinese only, never crashes.
        d_->graph.reset();
        return;
    }
    d_->endsFromBegin.assign(next.size() + 1, {});
    for (size_t i = 0; i <= next.size(); ++i) {
        std::set<size_t> ends;
        try {
            for (const auto &node : d_->graph->nodes(i)) {
                for (const auto &nxt : node.nexts()) {
                    if (nxt.index() > i && nxt.index() <= next.size()) {
                        ends.insert(nxt.index());
                    }
                }
            }
        } catch (...) {
            // nodes(i) may throw for positions with no graph node; ignore.
        }
        d_->endsFromBegin[i].assign(ends.begin(), ends.end());
    }
}

std::vector<SegmentationArc>
LibIMEChineseArcOracle::arcsAt(std::string_view raw, size_t begin,
                               size_t maxSpan) const {
    std::vector<SegmentationArc> out;
    if (!d_->graph || begin >= raw.size()) {
        return out;
    }
    if (begin >= d_->endsFromBegin.size()) {
        return out;
    }
    // The caller is expected to pass the same raw string as setRaw(); guard
    // against a mismatch rather than silently indexing a stale adjacency.
    if (raw.size() != d_->lastRaw.size()) {
        return out;
    }
    const size_t limit =
        std::min(raw.size(), begin + std::max<size_t>(1, maxSpan));
    int rank = 0;
    for (const auto end : d_->endsFromBegin[begin]) {
        if (end > limit) {
            break;
        }
        SegmentationArc arc;
        arc.rawBegin = begin;
        arc.rawEnd = end;
        arc.source = SegmentSource::Chinese;
        arc.provenance = CandidateProvenance::Exact;
        // Structural evidence: the syllable is parser-valid. LM-based
        // quality lives with the classical decoder behind the Han resolver
        // and is expressed via top-N Han picks (batch 7B fusion seam).
        arc.confidence = 0.75F;
        arc.boundaryConfidence = 1.0F;
        arc.sourceLocalRank = rank++;
        out.push_back(arc);
    }
    return out;
}

} // namespace pinyin
