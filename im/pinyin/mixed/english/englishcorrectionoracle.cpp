/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "englishcorrectionoracle.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <unordered_set>

namespace pinyin {

namespace {

// Hand-authored QWERTY adjacency, folded lowercase. Layout data is factual;
// this table is written from scratch, not copied from any specific source.
// Each key's row is a small list of horizontally-adjacent neighbors; no
// diagonals are included so V1 correction stays strictly bounded to
// "user's finger drifted sideways".
const std::array<std::vector<char>, 26> &adjacencyTable() {
    static const std::array<std::vector<char>, 26> table = [] {
        std::array<std::vector<char>, 26> t{};
        auto put = [&](char k, std::vector<char> v) {
            t[static_cast<std::size_t>(k - 'a')] = std::move(v);
        };
        // Row 1: q w e r t y u i o p
        put('q', {'w'});
        put('w', {'q', 'e'});
        put('e', {'w', 'r'});
        put('r', {'e', 't'});
        put('t', {'r', 'y'});
        put('y', {'t', 'u'});
        put('u', {'y', 'i'});
        put('i', {'u', 'o'});
        put('o', {'i', 'p'});
        put('p', {'o'});
        // Row 2: a s d f g h j k l
        put('a', {'s'});
        put('s', {'a', 'd'});
        put('d', {'s', 'f'});
        put('f', {'d', 'g'});
        put('g', {'f', 'h'});
        put('h', {'g', 'j'});
        put('j', {'h', 'k'});
        put('k', {'j', 'l'});
        put('l', {'k'});
        // Row 3: z x c v b n m
        put('z', {'x'});
        put('x', {'z', 'c'});
        put('c', {'x', 'v'});
        put('v', {'c', 'b'});
        put('b', {'v', 'n'});
        put('n', {'b', 'm'});
        put('m', {'n'});
        return t;
    }();
    return table;
}

char foldChar(unsigned char c) {
    if (c >= 'A' && c <= 'Z') {
        return static_cast<char>(c - 'A' + 'a');
    }
    return static_cast<char>(c);
}

bool isAlphaLower(char c) { return c >= 'a' && c <= 'z'; }

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

const std::vector<char> &EnglishCorrectionOracle::qwertyAdjacent(char c) {
    static const std::vector<char> kEmpty;
    const char lower = foldChar(static_cast<unsigned char>(c));
    if (!isAlphaLower(lower)) {
        return kEmpty;
    }
    return adjacencyTable()[static_cast<std::size_t>(lower - 'a')];
}

float EnglishCorrectionOracle::evidenceFor(Kind kind) {
    // Bounded below the maximum System-Lexicon exact evidence (0.965 at tier
    // 9) so a correction NEVER outranks a same-span exact word.
    switch (kind) {
    case Kind::Transposition:
        return 0.65F;
    case Kind::Substitution:
        return 0.55F;
    }
    return 0.0F;
}

std::vector<std::string>
EnglishCorrectionOracle::neighbors(std::string_view folded) {
    std::vector<std::string> out;
    if (folded.size() < 2) {
        return out;
    }
    std::unordered_set<std::string> seen;
    const std::string s(folded);
    const std::size_t n = s.size();
    // Substitutions: for each position, for each QWERTY-adjacent letter,
    // replace and dedupe.
    out.reserve(n * 4 + n);
    for (std::size_t i = 0; i < n; ++i) {
        if (!isAlphaLower(s[i])) {
            continue;
        }
        for (char alt : qwertyAdjacent(s[i])) {
            std::string cand = s;
            cand[i] = alt;
            if (cand != s && seen.insert(cand).second) {
                out.push_back(std::move(cand));
            }
        }
    }
    // Transpositions: swap each adjacent pair where both are lowercase
    // letters and differ.
    for (std::size_t i = 0; i + 1 < n; ++i) {
        if (!isAlphaLower(s[i]) || !isAlphaLower(s[i + 1])) {
            continue;
        }
        if (s[i] == s[i + 1]) {
            continue;
        }
        std::string cand = s;
        std::swap(cand[i], cand[i + 1]);
        if (seen.insert(cand).second) {
            out.push_back(std::move(cand));
        }
    }
    return out;
}

EnglishCorrectionOracle::EnglishCorrectionOracle(
    const EnglishLexicon *systemLex)
    : EnglishCorrectionOracle(systemLex, Config{}) {}

EnglishCorrectionOracle::EnglishCorrectionOracle(
    const EnglishLexicon *systemLex, Config config)
    : systemLex_(systemLex), config_(config) {}

std::vector<SegmentationArc>
EnglishCorrectionOracle::arcsAt(std::string_view raw, size_t begin,
                                size_t maxSpan) const {
    std::vector<SegmentationArc> out;
    if (systemLex_ == nullptr || systemLex_->empty()) {
        return out;
    }
    if (begin >= raw.size()) {
        return out;
    }
    const size_t endLimit =
        std::min(raw.size(), begin + std::max<size_t>(1, maxSpan));
    const std::size_t cap =
        std::max<std::size_t>(1, config_.maxCorrectionsPerSpan);
    const std::size_t minLen = std::max<std::size_t>(2, config_.minSpanLength);

    int localRank = 0;
    for (size_t e = begin + minLen; e <= endLimit; ++e) {
        const std::string_view span = raw.substr(begin, e - begin);
        auto folded = EnglishLexicon::foldKey(span);
        if (!folded) {
            break;
        }
        // False-correction guard: if this span itself is a real word, do
        // NOT emit corrections for it. That is Exact's job (§17.4).
        if (systemLex_->lookup(*folded) != nullptr) {
            continue;
        }
        // Enumerate bounded 1-edit neighbors and lookup each. Track the
        // edit kind per candidate (first-seen wins for substitutions on
        // transpositions).
        std::unordered_set<std::string> emitted;
        const auto subs = [&]() {
            std::vector<std::pair<std::string, Kind>> v;
            for (std::size_t i = 0; i < folded->size(); ++i) {
                const char c = (*folded)[i];
                if (!isAlphaLower(c)) {
                    continue;
                }
                for (char alt : qwertyAdjacent(c)) {
                    std::string cand = *folded;
                    cand[i] = alt;
                    v.emplace_back(std::move(cand), Kind::Substitution);
                }
            }
            for (std::size_t i = 0; i + 1 < folded->size(); ++i) {
                const char a = (*folded)[i];
                const char b = (*folded)[i + 1];
                if (!isAlphaLower(a) || !isAlphaLower(b) || a == b) {
                    continue;
                }
                std::string cand = *folded;
                std::swap(cand[i], cand[i + 1]);
                v.emplace_back(std::move(cand), Kind::Transposition);
            }
            return v;
        }();
        std::size_t produced = 0;
        for (const auto &[cand, kind] : subs) {
            if (produced >= cap) {
                break;
            }
            if (!emitted.insert(cand).second) {
                continue;
            }
            const auto *hit = systemLex_->lookup(cand);
            if (hit == nullptr || hit->literalOnly) {
                continue;
            }
            // Confidence = kind base * exact evidence for the target word.
            // Never exceeds 0.85, well below the 0.965 max exact so a
            // correction cannot outrank the word user actually typed.
            const float base = evidenceFor(kind);
            const float targetEv = systemLex_->exactEvidence(*hit);
            float conf = clampUnit(base * targetEv);
            if (conf > 0.85F) {
                conf = 0.85F;
            }
            SegmentationArc arc;
            arc.rawBegin = begin;
            arc.rawEnd = e;
            arc.source = SegmentSource::English;
            arc.provenance = CandidateProvenance::Correction;
            arc.confidence = conf;
            arc.boundaryConfidence = 0.55F;
            arc.sourceLocalRank = localRank++;
            arc.resolvedOutput = hit->display;
            out.push_back(arc);
            ++produced;
        }
    }
    return out;
}

} // namespace pinyin
