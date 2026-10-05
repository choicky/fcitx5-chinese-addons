/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "englishuserlexicon.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>

namespace pinyin {

namespace {

bool isAllowedByte(unsigned char c) {
    const bool alpha = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                       (c >= '0' && c <= '9');
    return alpha || c == '\'' || c == '-';
}

std::string toLowerAscii(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c >= 'A' && c <= 'Z') {
            out.push_back(static_cast<char>(c - 'A' + 'a'));
        } else {
            out.push_back(c);
        }
    }
    return out;
}

float clampUnit(float v) {
    if (v < 0.0F) {
        return 0.0F;
    }
    if (v > 0.95F) {
        return 0.95F;
    }
    return v;
}

} // namespace

EnglishUserLexicon::EnglishUserLexicon() = default;

std::size_t EnglishUserLexicon::lowerBound(std::string_view key) const {
    return static_cast<std::size_t>(
        std::lower_bound(entries_.begin(), entries_.end(), key,
                         [](const EnglishUserEntry &a, std::string_view b) {
                             return a.key < b;
                         }) -
        entries_.begin());
}

bool EnglishUserLexicon::load(std::istream &in) {
    entries_.clear();
    std::string line;
    bool sawHeader = false;
    std::uint32_t schema = 0;
    while (std::getline(in, line)) {
        // Leading whitespace is not part of our format; a line starting with
        // '#' is either the schema header or a comment.
        if (!line.empty() && line[0] == '#') {
            const std::string_view prefix = "#schema=";
            if (line.compare(0, prefix.size(), prefix) == 0) {
                const std::string_view val(line.data() + prefix.size(),
                                           line.size() - prefix.size());
                try {
                    schema = static_cast<std::uint32_t>(
                        std::stoul(std::string(val)));
                } catch (...) {
                    return false;
                }
                sawHeader = true;
            }
            continue;
        }
        if (line.empty()) {
            continue;
        }
        if (!sawHeader) {
            // Data row present without schema header: refuse to load. Keeps
            // us forward-compatible against a future schema-introduced file
            // whose version marker we would have missed.
            return false;
        }
        if (schema != kSchemaVersion) {
            // Unknown schema: safe fallback (empty lexicon, no throw).
            return false;
        }
        std::vector<std::string> cols;
        cols.reserve(5);
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
        if (cols.size() < 5) {
            continue;
        }
        EnglishUserEntry e;
        e.key = toLowerAscii(cols[0]);
        if (e.key.empty()) {
            continue;
        }
        bool bad = false;
        for (unsigned char c : e.key) {
            if (!isAllowedByte(c)) {
                bad = true;
                break;
            }
        }
        if (bad) {
            continue;
        }
        try {
            e.count = static_cast<std::uint32_t>(std::stoul(cols[1]));
            e.firstSeenSec = static_cast<std::uint64_t>(std::stoull(cols[2]));
            e.lastUsedSec = static_cast<std::uint64_t>(std::stoull(cols[3]));
        } catch (...) {
            continue;
        }
        e.display = cols[4];
        if (e.display.empty()) {
            e.display = e.key;
        }
        entries_.push_back(std::move(e));
    }
    std::sort(entries_.begin(), entries_.end(),
              [](const EnglishUserEntry &a, const EnglishUserEntry &b) {
                  return a.key < b.key;
              });
    // Collapse any duplicate keys (shouldn't happen given we write only via
    // learn(), but a hand-edited file may contain them). Keep the entry with
    // the larger count / later lastUsed.
    std::vector<EnglishUserEntry> deduped;
    deduped.reserve(entries_.size());
    for (auto &e : entries_) {
        if (!deduped.empty() && deduped.back().key == e.key) {
            auto &prev = deduped.back();
            if (e.count > prev.count ||
                (e.count == prev.count && e.lastUsedSec > prev.lastUsedSec)) {
                prev = std::move(e);
            }
        } else {
            deduped.push_back(std::move(e));
        }
    }
    entries_.swap(deduped);
    return true;
}

void EnglishUserLexicon::save(std::ostream &out) const {
    out << "#schema=" << kSchemaVersion << "\n";
    for (const auto &e : entries_) {
        out << e.key << '\t' << e.count << '\t' << e.firstSeenSec << '\t'
            << e.lastUsedSec << '\t' << e.display << '\n';
    }
}

void EnglishUserLexicon::learn(std::string_view folded,
                               std::string_view display, std::uint64_t nowSec) {
    if (folded.empty()) {
        return;
    }
    std::string key(folded);
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    auto it = std::lower_bound(entries_.begin(), entries_.end(), key,
                               [](const EnglishUserEntry &a,
                                  const std::string &b) { return a.key < b; });
    if (it != entries_.end() && it->key == key) {
        if (it->count < std::numeric_limits<std::uint32_t>::max()) {
            ++it->count;
        }
        it->lastUsedSec = nowSec;
        if (!display.empty()) {
            it->display.assign(display);
        }
        return;
    }
    EnglishUserEntry e;
    e.key = std::move(key);
    e.display = display.empty() ? std::string(folded) : std::string(display);
    e.count = 1;
    e.firstSeenSec = nowSec;
    e.lastUsedSec = nowSec;
    entries_.insert(it, std::move(e));
}

bool EnglishUserLexicon::forget(std::string_view folded) {
    if (folded.empty()) {
        return false;
    }
    std::string key = toLowerAscii(folded);
    auto it = std::lower_bound(entries_.begin(), entries_.end(), key,
                               [](const EnglishUserEntry &a,
                                  const std::string &b) { return a.key < b; });
    if (it != entries_.end() && it->key == key) {
        entries_.erase(it);
        return true;
    }
    return false;
}

void EnglishUserLexicon::reset() { entries_.clear(); }

const EnglishUserEntry *
EnglishUserLexicon::lookup(std::string_view folded) const {
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

float EnglishUserLexicon::evidence(const EnglishUserEntry &entry) const {
    // Saturating in log(count). Capped at 0.95 to leave headroom (§19).
    const float c = static_cast<float>(entry.count);
    const float ratio = std::log1p(c) / std::log1p(static_cast<float>(1000.0));
    return clampUnit(0.55F + 0.40F * ratio);
}

EnglishUserArcOracle::EnglishUserArcOracle(const EnglishUserLexicon *lex)
    : lex_(lex) {}

std::vector<SegmentationArc>
EnglishUserArcOracle::arcsAt(std::string_view raw, size_t begin,
                             size_t maxSpan) const {
    std::vector<SegmentationArc> out;
    if (lex_ == nullptr || lex_->size() == 0) {
        return out;
    }
    if (begin >= raw.size()) {
        return out;
    }
    const size_t endLimit =
        std::min(raw.size(), begin + std::max<size_t>(1, maxSpan));
    int localRank = 0;
    for (size_t e = begin + 1; e <= endLimit; ++e) {
        const std::string_view span = raw.substr(begin, e - begin);
        std::string key;
        key.reserve(span.size());
        bool ok = true;
        for (char c : span) {
            const unsigned char u = static_cast<unsigned char>(c);
            if (!isAllowedByte(u)) {
                ok = false;
                break;
            }
            if (u >= 'A' && u <= 'Z') {
                key.push_back(static_cast<char>(u - 'A' + 'a'));
            } else {
                key.push_back(static_cast<char>(u));
            }
        }
        if (!ok) {
            break;
        }
        const auto *entry = lex_->lookup(key);
        if (entry == nullptr) {
            continue;
        }
        SegmentationArc arc;
        arc.rawBegin = begin;
        arc.rawEnd = e;
        arc.source = SegmentSource::English;
        arc.provenance = CandidateProvenance::CustomPhrase;
        arc.confidence = lex_->evidence(*entry);
        arc.boundaryConfidence = 0.90F;
        arc.sourceLocalRank = localRank++;
        out.push_back(arc);
    }
    return out;
}

} // namespace pinyin
