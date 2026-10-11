/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "englisharcoracle.h"

#include <algorithm>
#include <cctype>

#include "../experiment.h"

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
    // PoC-C1 (EXPERIMENTAL — NOT ACCEPTED PRODUCT BEHAVIOR; default OFF via
    // experiment.h): when the case-gated arm is enabled, contiguous
    // [A-Z]{1,2} spans run through the identical existing tier logic below
    // even though they sit under Gate A's minSpanLength. Lowercase,
    // mixed-case, digit/punctuation and longer spans keep the baseline gate
    // exactly; with the switch off this loop is byte-identical to the
    // accepted product path.
    const auto &exp = shortLatinExperiment();
    const size_t gateAMinEnd =
        begin + std::max<std::size_t>(1, config_.minSpanLength);
    const size_t minEnd =
        exp.caseGatedShortAdmission ? begin + 1 : gateAMinEnd;
    for (size_t e = minEnd; e <= endLimit; ++e) {
        const bool shortUppercase =
            exp.caseGatedShortAdmission && e < gateAMinEnd;
        if (shortUppercase &&
            !isShortUppercaseSpan(raw, begin, e - begin)) {
            // Gate A stays intact for every non-[A-Z]{1,2} short span.
            continue;
        }
        const std::string_view span = raw.substr(begin, e - begin);
        auto folded = EnglishLexicon::foldKey(span);
        if (!folded) {
            // Any non-alphabetic byte (or an out-of-range byte) means this
            // span cannot be an English surface form; further extension will
            // also fail. Stop scanning.
            break;
        }
        const size_t outBefore = out.size();
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
        // ARM_B (PoC-C1, EXPERIMENTAL — default OFF): uppercase raw/OOV
        // literal. The user's explicit case makes the typed surface itself a
        // hypothesis; no lexicon hit is required. Dedup: skip when an
        // already-emitted arc for this exact span carries the same output
        // (the userTypedCase exact path always yields output == span for an
        // [A-Z]{1,2} hit). Exact provenance keeps the user surface
        // authoritative and leaves isBoundedInsertionClass (Canonical |
        // CustomPhrase) untouched.
        if (shortUppercase && exp.uppercaseShortRawOov) {
            const bool alreadyEmitted = std::any_of(
                out.begin() + static_cast<std::ptrdiff_t>(outBefore),
                out.end(), [&](const SegmentationArc &a) {
                    return a.rawEnd == e && a.resolvedOutput == span;
                });
            if (!alreadyEmitted) {
                SegmentationArc oov;
                oov.rawBegin = begin;
                oov.rawEnd = e;
                oov.source = SegmentSource::English;
                oov.provenance = CandidateProvenance::Exact;
                oov.confidence = 0.65F;
                oov.boundaryConfidence = 1.0F;
                oov.sourceLocalRank = localRank++;
                oov.resolvedOutput = std::string(span);
                out.push_back(std::move(oov));
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
            std::size_t emitted = 0;
            for (const auto *ce : completions) {
                if (ce == nullptr) {
                    continue;
                }
                if (ce->key == *folded) {
                    continue;
                }
                if (emitted >= config_.maxCompletionsPerSpan) {
                    break;
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
                ++emitted;
                // Canonical completion surfaces obey the same literal-beside-
                // canonical contract as exact hits. Reserve the second slot
                // from this span's fan-out so the bound remains structural.
                if (ce->display != ce->key &&
                    emitted < config_.maxCompletionsPerSpan) {
                    SegmentationArc literal = arc;
                    literal.provenance = CandidateProvenance::Completion;
                    literal.boundaryConfidence = 0.55F;
                    literal.sourceLocalRank = localRank++;
                    literal.resolvedOutput = ce->key;
                    out.push_back(std::move(literal));
                    ++emitted;
                }
            }
        }
        if (shortUppercase) {
            shortlatindiag::count(shortlatindiag::shortArcsAdmitted,
                                  out.size() - outBefore);
        }
    }
    return out;
}

} // namespace pinyin
