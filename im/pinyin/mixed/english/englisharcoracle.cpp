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
    // Spans shorter than minSpanLength never participate as English evidence
    // (single-letter SCOWL "words" are pinyin-initial noise; see header).
    const size_t minEnd =
        begin + std::max<std::size_t>(1, config_.minSpanLength);
    for (size_t e = minEnd; e <= endLimit; ++e) {
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
            if (!exactEvidence) {
                // Product contract (D071): a canonical re-spelling must not
                // erase the literal form the user typed — "chatgpt" yields
                // both "ChatGPT" and "chatgpt". The literal rides the same
                // dictionary hit as a parallel arc (distinct
                // sourceLocalRank), so it composes, dedups
                // (composedText-keyed) and ranks through the normal
                // pipeline. Canonical provenance keeps ranker feature
                // parity with the display arc; the weaker boundary
                // confidence only breaks the cost tie so the canonical
                // display stays deterministically ahead of the literal.
                SegmentationArc literal;
                literal.rawBegin = arc.rawBegin;
                literal.rawEnd = arc.rawEnd;
                literal.source = SegmentSource::English;
                literal.provenance = CandidateProvenance::Canonical;
                literal.confidence = arc.confidence;
                literal.boundaryConfidence = 0.90F;
                literal.sourceLocalRank = localRank++;
                literal.resolvedOutput = std::string(span);
                out.push_back(literal);
            }
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
