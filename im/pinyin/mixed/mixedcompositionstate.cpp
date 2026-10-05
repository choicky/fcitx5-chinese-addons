/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "mixedcompositionstate.h"

#include <algorithm>

namespace pinyin {

void MixedCompositionState::setRawInput(std::string_view raw) {
    rawInput_.assign(raw);
    if (selectionFrontier_ > rawInput_.size()) {
        selectionFrontier_ = rawInput_.size();
    }
    // Any change to the raw stream invalidates the suffix decomposition; the
    // caller recomputes segments for the remaining input.
    segments_.clear();
}

std::string_view MixedCompositionState::selectedRaw() const {
    return std::string_view(rawInput_).substr(0, selectionFrontier_);
}

std::string_view MixedCompositionState::remainingRaw() const {
    return std::string_view(rawInput_).substr(selectionFrontier_);
}

bool MixedCompositionState::isFullySelected() const {
    return selectionFrontier_ == rawInput_.size();
}

bool MixedCompositionState::consumeSelection(size_t newFrontier) {
    if (newFrontier < selectionFrontier_ || newFrontier > rawInput_.size()) {
        return false;
    }
    if (newFrontier == selectionFrontier_) {
        return true;
    }

    bool onBoundary = (newFrontier == rawInput_.size());
    for (const auto &seg : segments_) {
        if (seg.rawEnd == newFrontier) {
            onBoundary = true;
            break;
        }
    }
    if (!onBoundary) {
        return false;
    }

    selectionFrontier_ = newFrontier;
    for (auto &seg : segments_) {
        if (seg.rawEnd <= selectionFrontier_) {
            seg.selected = true;
        }
    }
    return true;
}

bool MixedCompositionState::setSegments(std::vector<MixedSegment> segments) {
    std::sort(segments.begin(), segments.end(),
              [](const MixedSegment &a, const MixedSegment &b) {
                  return a.rawBegin < b.rawBegin;
              });

    const size_t suffixBegin = selectionFrontier_;
    const size_t suffixEnd = rawInput_.size();
    size_t cursor = suffixBegin;
    for (const auto &seg : segments) {
        if (seg.rawEnd <= seg.rawBegin) {
            return false;
        }
        if (seg.rawBegin != cursor) {
            return false;
        }
        if (seg.rawEnd > suffixEnd) {
            return false;
        }
        cursor = seg.rawEnd;
    }
    if (cursor != suffixEnd) {
        return false;
    }

    // Preserve the frozen selected prefix if the caller re-supplies it; the
    // decomposition only covers the uncommitted suffix.
    std::vector<MixedSegment> frozen;
    for (const auto &seg : segments_) {
        if (seg.selected) {
            frozen.push_back(seg);
        }
    }
    segments_ = std::move(frozen);
    for (auto &seg : segments) {
        seg.selected = false;
        segments_.push_back(std::move(seg));
    }
    return true;
}

std::string MixedCompositionState::committedOutput() const {
    std::string out;
    for (const auto &seg : segments_) {
        if (seg.selected) {
            out += seg.output;
        }
    }
    return out;
}

std::string MixedCompositionState::pendingOutput() const {
    std::string out;
    for (const auto &seg : segments_) {
        if (!seg.selected) {
            out += seg.output;
        }
    }
    return out;
}

std::vector<RawOutputSpan> MixedCompositionState::buildAlignment() const {
    std::vector<RawOutputSpan> spans;
    size_t outputCursor = 0;
    for (const auto &seg : segments_) {
        RawOutputSpan span;
        span.rawBegin = seg.rawBegin;
        span.rawEnd = seg.rawEnd;
        span.outputBegin = outputCursor;
        outputCursor += seg.output.size();
        span.outputEnd = outputCursor;
        spans.push_back(span);
    }
    return spans;
}

} // namespace pinyin
