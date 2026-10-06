/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "englishlexicon.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace pinyin {

namespace {

bool isAsciiAlpha(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}
bool isAsciiDigit(unsigned char c) { return c >= '0' && c <= '9'; }

char foldChar(unsigned char c) {
    if (c >= 'A' && c <= 'Z') {
        return static_cast<char>(c - 'A' + 'a');
    }
    return static_cast<char>(c);
}

bool isAllowedSurfaceByte(unsigned char c) {
    return isAsciiAlpha(c) || isAsciiDigit(c) || c == '\'' || c == '-';
}

float clampUnit(float v) {
    if (v < 0.0F) {
        return 0.0F;
    }
    if (v > 1.0F) {
        return 1.0F;
    }
    return v;
}

} // namespace

EnglishLexicon::EnglishLexicon() = default;

void EnglishLexicon::clear() { entries_.clear(); }

std::optional<std::string> EnglishLexicon::foldKey(std::string_view raw) {
    if (raw.empty()) {
        return std::nullopt;
    }
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!isAllowedSurfaceByte(u)) {
            return std::nullopt;
        }
        out.push_back(foldChar(u));
    }
    return out;
}

void EnglishLexicon::loadFromEntries(std::vector<EnglishLexiconEntry> entries) {
    entries_.clear();
    entries_.reserve(entries.size());
    for (auto &e : entries) {
        // Normalize the key to folded ASCII so lookups are consistent even
        // if a caller passed a Title/UPPER key.
        if (auto folded = foldKey(e.key)) {
            e.key = *folded;
        } else {
            continue;
        }
        if (e.tier > 9) {
            e.tier = 9;
        }
        entries_.push_back(std::move(e));
    }
    std::sort(entries_.begin(), entries_.end(),
              [](const EnglishLexiconEntry &a, const EnglishLexiconEntry &b) {
                  return a.key < b.key;
              });
}

void EnglishLexicon::load(std::istream &in) {
    std::vector<EnglishLexiconEntry> parsed;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        // Split on TAB.
        std::vector<std::string> cols;
        cols.reserve(4);
        std::size_t start = 0;
        while (true) {
            const auto tab = line.find('\t', start);
            if (tab == std::string::npos) {
                cols.emplace_back(line.substr(start));
                break;
            }
            cols.emplace_back(line.substr(start, tab - start));
            start = tab + 1;
        }
        if (cols.size() < 2) {
            continue;
        }
        EnglishLexiconEntry e;
        e.key = std::move(cols[0]);
        try {
            const int tier = std::stoi(cols[1]);
            e.tier = static_cast<std::uint16_t>(tier < 0 ? 0 : tier);
        } catch (...) {
            continue;
        }
        if (cols.size() >= 3 && !cols[2].empty()) {
            e.display = std::move(cols[2]);
        }
        if (cols.size() >= 4) {
            std::string_view flags = cols[3];
            std::size_t pos = 0;
            while (pos <= flags.size()) {
                auto comma = flags.find(',', pos);
                std::string_view tok = (comma == std::string_view::npos)
                                           ? flags.substr(pos)
                                           : flags.substr(pos, comma - pos);
                if (tok == "p") {
                    e.proper = true;
                } else if (tok == "t") {
                    e.technical = true;
                } else if (tok == "l") {
                    e.literalOnly = true;
                }
                if (comma == std::string_view::npos) {
                    break;
                }
                pos = comma + 1;
            }
        }
        if (e.display.empty()) {
            e.display = e.key;
        }
        parsed.push_back(std::move(e));
    }
    loadFromEntries(std::move(parsed));
}

std::size_t EnglishLexicon::lowerBound(std::string_view key) const {
    return static_cast<std::size_t>(
        std::lower_bound(entries_.begin(), entries_.end(), key,
                         [](const EnglishLexiconEntry &a, std::string_view b) {
                             return a.key < b;
                         }) -
        entries_.begin());
}

std::size_t EnglishLexicon::upperBound(std::string_view key) const {
    return static_cast<std::size_t>(
        std::upper_bound(entries_.begin(), entries_.end(), key,
                         [](std::string_view a, const EnglishLexiconEntry &b) {
                             return a < b.key;
                         }) -
        entries_.begin());
}

const EnglishLexiconEntry *
EnglishLexicon::lookup(std::string_view folded) const {
    if (folded.empty()) {
        return nullptr;
    }
    const auto lo = lowerBound(folded);
    if (lo >= entries_.size()) {
        return nullptr;
    }
    if (entries_[lo].key == folded) {
        return &entries_[lo];
    }
    return nullptr;
}

bool EnglishLexicon::isExact(std::string_view raw) const {
    if (entries_.empty()) {
        return false;
    }
    auto folded = foldKey(raw);
    if (!folded) {
        return false;
    }
    return lookup(*folded) != nullptr;
}

std::optional<std::string>
EnglishLexicon::canonicalize(std::string_view raw) const {
    auto folded = foldKey(raw);
    if (!folded) {
        return std::nullopt;
    }
    const auto *e = lookup(*folded);
    if (!e) {
        return std::nullopt;
    }
    return e->display;
}

std::vector<const EnglishLexiconEntry *>
EnglishLexicon::completions(std::string_view foldedPrefix,
                            std::size_t maxResults) const {
    std::vector<const EnglishLexiconEntry *> out;
    if (foldedPrefix.empty() || maxResults == 0 || entries_.empty()) {
        return out;
    }
    // Collect the prefix range under a scan budget, then keep the best
    // `maxResults` by frequency tier rather than the first in lexicographic
    // order. Feature-level rationale (fusion closure §6): completion arcs
    // are scored by completionEvidence, which is tier-driven (§13); at
    // production scale a lexicographic cut let dictionary neighbours
    // ("applause", "applet", ...) crowd out the dominant word for the
    // prefix ("apple", tier 9) inside the same cap. The budget keeps the
    // worst-case per-span cost linear for 1-2 letter prefixes whose ranges
    // are huge; ties break by key so the choice stays deterministic.
    constexpr std::size_t kCompletionScanBudget = 512;
    const auto lo = lowerBound(foldedPrefix);
    std::size_t scanned = 0;
    for (std::size_t i = lo;
         i < entries_.size() && scanned < kCompletionScanBudget; ++i) {
        const auto &e = entries_[i];
        if (e.key.size() <= foldedPrefix.size()) {
            continue;
        }
        if (e.key.compare(0, foldedPrefix.size(), foldedPrefix) != 0) {
            break;
        }
        if (e.literalOnly) {
            continue;
        }
        ++scanned;
        out.push_back(&e);
    }
    if (out.size() > maxResults) {
        // Tier first; inside one tier the SHORTEST completion wins before
        // key order. Feature-level note: the production resource ties most
        // common words at tier 9, so a pure key tie-break would re-lean on
        // lexicography (applaud* ahead of apple). Among equally frequent
        // completions the shortest is the most likely intended word for the
        // prefix the user actually typed.
        std::sort(
            out.begin(), out.end(),
            [](const EnglishLexiconEntry *l, const EnglishLexiconEntry *r) {
                if (l->tier != r->tier) {
                    return l->tier > r->tier;
                }
                if (l->key.size() != r->key.size()) {
                    return l->key.size() < r->key.size();
                }
                return l->key < r->key;
            });
        out.resize(maxResults);
    }
    return out;
}

std::size_t
EnglishLexicon::completionCount(std::string_view foldedPrefix) const {
    if (foldedPrefix.empty() || entries_.empty()) {
        return 0;
    }
    std::size_t cnt = 0;
    const auto lo = lowerBound(foldedPrefix);
    for (std::size_t i = lo; i < entries_.size(); ++i) {
        const auto &e = entries_[i];
        if (e.key.size() <= foldedPrefix.size()) {
            continue;
        }
        if (e.key.compare(0, foldedPrefix.size(), foldedPrefix) != 0) {
            break;
        }
        if (e.literalOnly) {
            continue;
        }
        ++cnt;
    }
    return cnt;
}

float EnglishLexicon::exactEvidence(const EnglishLexiconEntry &entry) const {
    if (entry.literalOnly) {
        return clampUnit(0.55F);
    }
    const float t = static_cast<float>(std::min<std::uint16_t>(entry.tier, 9));
    return clampUnit(0.65F + 0.035F * t);
}

float EnglishLexicon::canonicalEvidence(
    const EnglishLexiconEntry &entry) const {
    return clampUnit(exactEvidence(entry) * 0.95F);
}

float EnglishLexicon::completionEvidence(const EnglishLexiconEntry &entry,
                                         std::size_t foldedPrefixLen,
                                         std::size_t completions) const {
    const float t = static_cast<float>(std::min<std::uint16_t>(entry.tier, 9));
    const float prefixLen = static_cast<float>(foldedPrefixLen);
    const float specificity =
        std::min(0.10F, 0.02F * std::max(0.0F, prefixLen - 1.0F));
    const float fanoutPenalty =
        std::min(0.10F, 0.002F * static_cast<float>(completions));
    return clampUnit(0.45F + 0.035F * t + specificity - fanoutPenalty);
}

} // namespace pinyin
