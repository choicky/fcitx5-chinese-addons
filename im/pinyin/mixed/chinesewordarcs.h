/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _FCITX_PINYIN_MIXED_CHINESEWORDARCS_H_
#define _FCITX_PINYIN_MIXED_CHINESEWORDARCS_H_

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <libime/core/languagemodel.h>
#include <libime/core/segmentgraph.h>
#include <libime/pinyin/pinyindictionary.h>

namespace libime {
class PinyinIME;
class PinyinMatchState;
} // namespace libime

namespace pinyin {

enum class ChineseInputMode;

// One LibIME dictionary word that matches a path of the raw segment graph.
// ("A-prime" below is the superseded per-gap Chinese decoding design that this
// whole-raw table replaces; see the terminology note in m2psearch.h.) This is
// the arc shape A-prime's gap solver consumes: `adjust` is the value
// LibIME's `matchPrefix` callback reports for the path (decoder.cpp applies it
// as the node cost, `node.setScore(maxScore + node.cost())`,
// src/libime/core/decoder.cpp:239) and `frameScore` is the
// single-word-plus-adjust score used for frame ordering and pruning.
struct ChineseWordArc {
    size_t from = 0;
    size_t to = 0;
    std::string word;
    libime::WordIndex index = libime::InvalidWordIndex;
    float adjust = 0;
    float frameScore = 0;
    bool correction = false;
    bool unknownSingle = false;
};

// Build the whole-input segment graph for `raw` with the same parser the
// classical decoder uses (parseUserPinyin / parseUserShuangpin).
std::unique_ptr<libime::SegmentGraph>
parseChineseGraph(const libime::PinyinIME *ime, ChineseInputMode mode,
                  std::string_view raw);

// One `matchPrefix` pass over `graph`, appending the words it reports as
// arcs offset by `offset`, then applying the two order-dependent rules the
// pinned decoder applies before any score is read: the unknown-single-syllable
// drop (onlyPath) and the per-(from,to) frameSize prune. Deterministic order:
// (from, to, frameScore desc, word bytes, index, adjust desc, correction).
void collectChineseWordArcs(const libime::PinyinIME *ime,
                            libime::PinyinMatchState &matchState,
                            libime::SegmentGraph &graph, size_t offset,
                            std::vector<ChineseWordArc> &out);

} // namespace pinyin

#endif // _FCITX_PINYIN_MIXED_CHINESEWORDARCS_H_
