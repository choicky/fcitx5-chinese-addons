/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "mixed/mixedcompositionstate.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace pinyin;

static int failures = 0;
static void check(bool cond, const char *what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

static MixedSegment seg(size_t b, size_t e, SegmentSource s, std::string o,
                        int rank = 0, float conf = 0.5F,
                        CandidateProvenance p = CandidateProvenance::Exact) {
    MixedSegment m;
    m.rawBegin = b;
    m.rawEnd = e;
    m.source = s;
    m.output = std::move(o);
    m.sourceLocalRank = rank;
    m.confidence = conf;
    m.provenance = p;
    return m;
}

int main() {
    const std::string raw = "woxiangmaiiphonepeijian"; // 23 bytes
    const std::string han1 =
        "\xe6\x88\x91\xe6\x83\xb3\xe4\xb9\xb0";          // 我想买 (9 bytes)
    const std::string han2 = "\xe9\x85\x8d\xe4\xbb\xb6"; // 配件 (6 bytes)

    // 1. initial state
    MixedCompositionState st;
    st.setRawInput(raw);
    check(st.rawInput() == raw, "raw stored");
    check(st.selectionFrontier() == 0, "frontier starts 0");
    check(st.remainingRaw() == std::string_view(raw), "remaining == full");
    check(st.selectedRaw().empty(), "selected empty");
    check(!st.isFullySelected(), "not fully selected");

    // 2. decomposition tiles the whole raw range
    check(st.setSegments({seg(0, 10, SegmentSource::Chinese, han1),
                          seg(10, 16, SegmentSource::English, "iPhone"),
                          seg(16, 23, SegmentSource::Chinese, han2)}),
          "setSegments valid tiling");
    check(st.committedOutput().empty(), "nothing committed yet");
    check(st.pendingOutput() == han1 + "iPhone" + han2, "pending == composed");

    // 3. alignment proves raw length != output byte length (no conflation)
    auto align = st.buildAlignment();
    check(align.size() == 3, "three alignment spans");
    check(align[0].rawEnd - align[0].rawBegin == 10, "span0 raw len 10");
    check(align[0].outputEnd - align[0].outputBegin == han1.size(),
          "span0 output bytes 9");
    check(align[1].outputEnd - align[1].outputBegin == 6, "iPhone output 6");
    check(align[2].rawBegin == 16 && align[2].rawEnd == 23, "span2 raw 16..23");

    // 4. partial selection consumes a monotonic raw prefix at boundaries
    check(st.consumeSelection(0), "consume to current frontier is a no-op");
    check(!st.consumeSelection(5), "mid-segment selection rejected");
    check(st.selectionFrontier() == 0, "rejected selection left frontier");
    check(st.consumeSelection(10), "select 我想买 prefix");
    check(st.selectionFrontier() == 10, "frontier advanced to 10");
    check(st.selectedRaw() == std::string_view("woxiangmai"), "selected raw");
    check(st.remainingRaw() == std::string_view("iphonepeijian"),
          "remaining suffix re-decodable");
    check(st.committedOutput() == han1, "committed == 我想买");
    check(st.segments()[0].selected, "span0 frozen");
    check(st.segments()[1].selected == false, "English suffix not frozen");

    // 5. monotonic guard: cannot roll the frontier back via selection
    check(!st.consumeSelection(0), "non-monotonic selection rejected");
    check(!st.consumeSelection(99), "out-of-range selection rejected");
    check(st.selectionFrontier() == 10, "frontier unchanged after rejects");

    // 6. continue selecting across the English segment then commit
    check(st.consumeSelection(16), "select English segment");
    check(st.consumeSelection(23), "select final Han segment");
    check(st.isFullySelected(), "fully selected");
    check(st.committedOutput() == han1 + "iPhone" + han2, "all committed");

    // 7. re-decomposing the suffix after a partial selection
    MixedCompositionState st2;
    st2.setRawInput(raw);
    check(st2.setSegments({seg(0, 10, SegmentSource::Chinese, han1),
                           seg(10, 16, SegmentSource::English, "iPhone"),
                           seg(16, 23, SegmentSource::Chinese, han2)}),
          "st2 tiling");
    check(st2.consumeSelection(10), "st2 freeze prefix");
    // suffix decomposition must cover [10,23) only
    check(st2.setSegments({seg(10, 16, SegmentSource::English, "iPhone"),
                           seg(16, 23, SegmentSource::Chinese, han2)}),
          "st2 re-decompose suffix");
    check(st2.committedOutput() == han1, "st2 prefix still frozen");
    check(st2.segments().size() == 3, "st2 keeps frozen + 2 suffix segments");

    // 8. invalid tilings rejected, state preserved
    MixedCompositionState st3;
    st3.setRawInput("abcdef");
    check(!st3.setSegments({seg(0, 3, SegmentSource::English, "abc"),
                            seg(4, 6, SegmentSource::English, "ef")}),
          "gap rejected");
    check(st3.segments().empty(), "segments unchanged after gap reject");
    check(!st3.setSegments({seg(0, 4, SegmentSource::English, "abcd"),
                            seg(2, 6, SegmentSource::English, "cdef")}),
          "overlap rejected");
    check(st3.setSegments({seg(0, 3, SegmentSource::English, "abc"),
                           seg(3, 6, SegmentSource::English, "def")}),
          "contiguous accepted");

    // 9. backspace semantics: shorter raw clamps frontier + clears suffix deco
    MixedCompositionState st4;
    st4.setRawInput(raw);
    st4.setSegments({seg(0, 10, SegmentSource::Chinese, han1),
                     seg(10, 16, SegmentSource::English, "iPhone"),
                     seg(16, 23, SegmentSource::Chinese, han2)});
    st4.consumeSelection(10);
    st4.setRawInput("woxiangmaiiphone"); // user deleted back into frozen prefix
    check(st4.selectionFrontier() <= st4.rawInput().size(),
          "frontier clamped to size");
    check(st4.segments().empty(), "suffix decomposition invalidated");

    if (failures == 0) {
        std::printf("MixedCompositionState: ALL TESTS PASSED\n");
        return 0;
    }
    std::fprintf(stderr, "MixedCompositionState: %d failure(s)\n", failures);
    return 1;
}
