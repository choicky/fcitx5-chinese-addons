/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _FCITX_PINYIN_MIXED_MIXEDCOMPOSITIONSTATE_H_
#define _FCITX_PINYIN_MIXED_MIXEDCOMPOSITIONSTATE_H_

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace pinyin {

enum class SegmentSource {
    Chinese,
    English,
};

enum class CandidateProvenance {
    Unknown,
    Exact,
    Canonical,
    Completion,
    Correction,
    CustomPhrase,
};

// A single decoded span of the raw code stream produced by one candidate
// source. All offsets are BYTE offsets into the raw input, never UTF-8
// character counts: raw pinyin length and Han output length differ and must
// not be conflated (instruction §9).
struct MixedSegment {
    size_t rawBegin = 0;
    size_t rawEnd = 0;
    SegmentSource source = SegmentSource::Chinese;
    CandidateProvenance provenance = CandidateProvenance::Exact;
    std::string output;
    // Source-local rank within the producing candidate source (>=0). Used for
    // the two-level ranking contract; never mixed with other sources' raw
    // scores directly.
    int sourceLocalRank = 0;
    // Source-local normalized confidence in [0, 1].
    float confidence = 0.0F;
    // Frozen by a confirmed selection; immutable thereafter.
    bool selected = false;

    size_t rawLength() const { return rawEnd - rawBegin; }
};

// Maps a raw byte range to the composed-output byte range it produced.
struct RawOutputSpan {
    size_t rawBegin = 0;
    size_t rawEnd = 0;
    size_t outputBegin = 0;
    size_t outputEnd = 0;
};

// Source of truth for mixed Chinese-English composition (instruction §9).
// Owns raw input, the segment decomposition of the uncommitted suffix and the
// selection frontier. Selected prefix is immutable; the remaining suffix stays
// re-decodable.
//
// This type is deliberately free of LibIME/Fcitx dependencies so the state
// model and alignment invariants can be validated independently of the
// decoding sources that populate it.
class MixedCompositionState {
public:
    MixedCompositionState() = default;

    void setRawInput(std::string_view raw);
    const std::string &rawInput() const { return rawInput_; }

    size_t selectionFrontier() const { return selectionFrontier_; }
    std::string_view selectedRaw() const;
    std::string_view remainingRaw() const;
    bool isFullySelected() const;

    // Monotonically advance the selection frontier to consume a raw prefix.
    // The new frontier must not exceed the raw length and must land on a
    // segment boundary (or the end of input): a global selection is expressed
    // as whole composed segments, each translated downstream into a
    // source-local transaction (LibIME selectCandidatesToCursor for Han,
    // selectCustom for English). Returns false and leaves state unchanged on
    // any violation.
    bool consumeSelection(size_t newFrontier);

    // Replace the decomposition of the uncommitted suffix [selectionFrontier_,
    // rawInput_.size()). Segments must be sorted and tile the suffix
    // contiguously with no gaps or overlaps. Returns false and leaves state
    // unchanged on any violation.
    bool setSegments(std::vector<MixedSegment> segments);

    const std::vector<MixedSegment> &segments() const { return segments_; }

    // Concatenated output of the selected (frozen) prefix.
    std::string committedOutput() const;
    // Output of the not-yet-selected suffix decomposition.
    std::string pendingOutput() const;

    // Compose selected + pending outputs and produce the raw↔output byte
    // alignment. A segment's output byte count is unrelated to its raw byte
    // count, so this walk, not a length assumption, is authoritative.
    std::vector<RawOutputSpan> buildAlignment() const;

private:
    std::string rawInput_;
    size_t selectionFrontier_ = 0;
    std::vector<MixedSegment> segments_;
};

} // namespace pinyin

#endif // _FCITX_PINYIN_MIXED_MIXEDCOMPOSITIONSTATE_H_
