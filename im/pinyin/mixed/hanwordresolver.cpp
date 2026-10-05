/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "hanwordresolver.h"

#include <algorithm>
#include <string>
#include <vector>

#include <libime/pinyin/pinyindictionary.h>
#include <libime/pinyin/pinyinencoder.h>

namespace pinyin {

HanWordResolver::HanWordResolver(const libime::PinyinDictionary *dict)
    : dict_(dict) {}

HanWordResolver::~HanWordResolver() = default;

std::string HanWordResolver::resolve(const SegmentationArc &arc,
                                     std::string_view rawSyllable) const {
    if (dict_ == nullptr || rawSyllable.empty()) {
        return {};
    }
    // Only Chinese arcs need Han resolution; anything else is a caller bug.
    if (arc.source != SegmentSource::Chinese) {
        return {};
    }
    const auto encoded =
        libime::PinyinEncoder::encodeOneUserPinyin(std::string(rawSyllable));
    if (encoded.empty()) {
        return {};
    }
    struct Hit {
        std::string hanzi;
        float cost;
    };
    std::vector<Hit> hits;
    // Match every word at this syllable, then pick top-N by cost so the
    // caller's sourceLocalRank selects among parser-valid alternatives
    // without re-running the decoder.
    dict_->matchWords(
        encoded.data(), encoded.size(),
        [&](std::string_view /*key*/, std::string_view value, float cost) {
            hits.push_back({std::string(value), cost});
            return true;
        });
    if (hits.empty()) {
        return {};
    }
    // Deterministic order: sort by cost ascending, then by hanzi bytes
    // ascending so ties do not depend on the dictionary's insertion order.
    std::sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) {
        if (a.cost != b.cost) {
            return a.cost < b.cost;
        }
        return a.hanzi < b.hanzi;
    });
    const std::size_t rank =
        arc.sourceLocalRank < 0 ? 0
                                : static_cast<std::size_t>(arc.sourceLocalRank);
    if (rank >= hits.size()) {
        return hits.back().hanzi;
    }
    return hits[rank].hanzi;
}

ArcResolver HanWordResolver::asArcResolver(std::string_view raw) const {
    const auto *self = this;
    std::string rawOwned(raw);
    return [self, rawOwned = std::move(rawOwned)](
               const SegmentationArc &arc) -> std::string {
        if (arc.rawEnd > rawOwned.size() || arc.rawBegin > arc.rawEnd) {
            return {};
        }
        std::string_view syllable(rawOwned.data() + arc.rawBegin,
                                  arc.rawEnd - arc.rawBegin);
        return self->resolve(arc, syllable);
    };
}

} // namespace pinyin
