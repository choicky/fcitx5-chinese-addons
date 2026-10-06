/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "englisharcoracle.h"

#include <algorithm>
#include <cctype>

namespace pinyin {

EnglishArcOracle::EnglishArcOracle(const EnglishLexicon *lex)
    : EnglishArcOracle(lex, Config{}) {}

EnglishArcOracle::EnglishArcOracle(const EnglishLexicon *lex, Config config)
    : lex_(lex), config_(config) {}

std::vector<SegmentationArc> EnglishArcOracle::arcsAt(std::string_view raw,
                                                      size_t begin,
                                                      size_t maxSpan) const {
    std::vector<SegmentationArc> out;
    if (lex_ == nullptr || lex_->empty()) {
        return out;
    }
    if (begin >= raw.size()) {
        return out;
    }
    const size_t endLimit =
        std::min(raw.size(), begin + std::max<size_t>(1, maxSpan));
    if (endLimit <= begin) {
        return out;
    }

    int localRank = 0;
    for (size_t e = begin + 1; e <= endLimit; ++e) {
        const std::string_view span = raw.substr(begin, e - begin);
        auto folded = EnglishLexicon::foldKey(span);
        if (!folded) {
            // Any non-alphabetic byte (or an out-of-range byte) means this
            // span cannot be an English surface form; further extension will
            // also fail. Stop scanning.
            break;
        }
        const auto *exact = lex_->lookup(*folded);
        if (exact != nullptr) {
            // Three-case surface resolution (replaces the retired ISpell
            // upper-raw path, see design §10 / SpellEnabled repoint):
            //  * span already equals the canonical display  -> Exact.
            //  * span carries user-typed capitalisation (any upper byte) and
            //    folds to the dictionary key -> the user's surface is
            //    authoritative ("Apple" stays "Apple", not the dictionary's
            //    "apple"); still Exact — the letters are a dictionary hit.
            //  * all-lowercase span whose canonical display differs ->
            //    Canonical re-spelling ("iphone" -> "iPhone").
            const bool userTypedCase =
                std::any_of(span.begin(), span.end(), [](char c) {
                    return std::isupper(static_cast<unsigned char>(c)) != 0;
                });
            const bool caseMatchesSurface = (span == exact->display);
            std::string output = exact->display;
            if (!caseMatchesSurface && userTypedCase) {
                output = std::string(span);
            }
            const bool exactEvidence = caseMatchesSurface || userTypedCase;
            SegmentationArc arc;
            arc.rawBegin = begin;
            arc.rawEnd = e;
            arc.source = SegmentSource::English;
            arc.provenance = exactEvidence ? CandidateProvenance::Exact
                                           : CandidateProvenance::Canonical;
            arc.confidence = exactEvidence ? lex_->exactEvidence(*exact)
                                           : lex_->canonicalEvidence(*exact);
            arc.boundaryConfidence = exactEvidence ? 1.0F : 0.95F;
            arc.sourceLocalRank = localRank++;
            arc.resolvedOutput = std::move(output);
            out.push_back(arc);
        }
        // Emit completion arcs at any prefix that has at least one non-literal
        // hit. No fixed minimum prefix (§13). Bounded fan-out per span.
        const auto completions =
            lex_->completions(*folded, config_.maxCompletionsPerSpan);
        if (!completions.empty()) {
            // Only emit a completion if the current span isn't already covered
            // by an exact/canonical with the same span (avoid duplicating
            // evidence). But we DO emit completions even when an exact hit
            // exists, because the user may want a longer word (e.g. "he" exact
            // but also completing "hello", "help"). Skip only the case where
            // the exact word itself would be the sole completion (key == span).
            const std::size_t fanout = lex_->completionCount(*folded);
            for (const auto *ce : completions) {
                if (ce == nullptr) {
                    continue;
                }
                if (ce->key == *folded) {
                    continue;
                }
                SegmentationArc arc;
                arc.rawBegin = begin;
                arc.rawEnd = e;
                arc.source = SegmentSource::English;
                arc.provenance = CandidateProvenance::Completion;
                arc.confidence =
                    lex_->completionEvidence(*ce, folded->size(), fanout);
                arc.boundaryConfidence = 0.60F;
                arc.sourceLocalRank = localRank++;
                arc.resolvedOutput = ce->display;
                out.push_back(arc);
            }
        }
    }
    return out;
}

} // namespace pinyin
