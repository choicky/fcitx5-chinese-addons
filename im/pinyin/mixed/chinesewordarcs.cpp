/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "chinesewordarcs.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <utility>

#include <libime/core/userlanguagemodel.h>
#include <libime/pinyin/pinyindecoder.h>
#include <libime/pinyin/pinyinencoder.h>
#include <libime/pinyin/pinyinime.h>
#include <libime/pinyin/pinyinmatchstate.h>
#include <libime/pinyin/shuangpinprofile.h>

#include "chinesearcoracle.h"

namespace pinyin {
namespace {

// The only use of LibIME's protected API in the addon. The adapter is kept
// local so an upstream signature change fails here rather than spreading an
// implementation dependency through the mixed-search code.
class LatticeNodeAdapter final : public libime::PinyinDecoder {
public:
    using libime::PinyinDecoder::PinyinDecoder;
    struct Inspection {
        bool correction = false;
        // LibIME drops an unknown single-syllable word that does not start
        // at the graph start unless it is the first match of its frame
        // (onlyPath). The caller applies that rule after ordering the matches.
        bool unknownSingle = false;
    };
    std::optional<Inspection>
    inspect(const libime::SegmentGraph &graph,
            const libime::LanguageModelBase *m, const libime::WordNode &word,
            libime::SegmentGraphPath path, float adjust,
            std::unique_ptr<libime::LatticeNodeData> data) const {
        const bool fromStart = path.front() == &graph.start();
        std::unique_ptr<libime::LatticeNode> node(createLatticeNode(
            graph, m, word.word(), word.idx(), std::move(path), m->nullState(),
            adjust, std::move(data), /*onlyPath=*/true));
        if (!node) {
            return std::nullopt;
        }
        const auto &pinyinNode = node->as<libime::PinyinLatticeNode>();
        return Inspection{pinyinNode.isCorrection(),
                          !fromStart && m->isUnknown(word.idx(), word.word()) &&
                              pinyinNode.encodedPinyin().size() == 2};
    }
};

} // namespace

std::unique_ptr<libime::SegmentGraph>
parseChineseGraph(const libime::PinyinIME *ime, ChineseInputMode mode,
                  std::string_view raw) {
    const std::string text(raw);
    if (mode == ChineseInputMode::Shuangpin) {
        const auto profile = ime->shuangpinProfile();
        if (!profile) {
            return nullptr;
        }
        return std::make_unique<libime::SegmentGraph>(
            libime::PinyinEncoder::parseUserShuangpin(text, *profile,
                                                      ime->fuzzyFlags()));
    }
    return std::make_unique<libime::SegmentGraph>(
        libime::PinyinEncoder::parseUserPinyin(
            text, ime->correctionProfile().get(), ime->fuzzyFlags()));
}

void collectChineseWordArcs(const libime::PinyinIME *ime,
                            libime::PinyinMatchState &state,
                            libime::SegmentGraph &graph, size_t offset,
                            std::vector<ChineseWordArc> &out) {
    state.clear();
    LatticeNodeAdapter adapter(ime->dict(), ime->model());
    const size_t first = out.size();
    ime->dict()->matchPrefix(
        graph,
        [&](const libime::SegmentGraphPath &path, libime::WordNode &word,
            float adjust, std::unique_ptr<libime::LatticeNodeData> data) {
            if (path.empty()) {
                return;
            }
            const size_t from = path.front()->index() + offset;
            const size_t to = path.back()->index() + offset;
            auto inspection = adapter.inspect(graph, ime->model(), word, path,
                                              adjust, std::move(data));
            if (!inspection) {
                return;
            }
            ChineseWordArc arc;
            arc.from = from;
            arc.to = to;
            arc.word = word.word();
            arc.index = word.idx();
            if (arc.index == libime::InvalidWordIndex) {
                arc.index = ime->model()->index(arc.word);
            }
            arc.adjust = adjust;
            arc.frameScore = ime->model()->singleWordScore(arc.word) + adjust;
            arc.correction = inspection->correction;
            arc.unknownSingle = inspection->unknownSingle;
            out.push_back(std::move(arc));
        },
        {}, &state);
    // matchPrefix reports matches in an order that follows pointer-keyed
    // caches, so it differs between runs. Give the matches a total order
    // (frame, best frame score first, then word bytes) before any decision
    // depends on their order.
    std::sort(out.begin() + first, out.end(),
              [](const ChineseWordArc &a, const ChineseWordArc &b) {
                  if (a.from != b.from) {
                      return a.from < b.from;
                  }
                  if (a.to != b.to) {
                      return a.to < b.to;
                  }
                  if (a.frameScore != b.frameScore) {
                      return a.frameScore > b.frameScore;
                  }
                  if (a.word != b.word) {
                      return a.word < b.word;
                  }
                  if (a.index != b.index) {
                      return a.index < b.index;
                  }
                  if (a.adjust != b.adjust) {
                      return a.adjust > b.adjust;
                  }
                  return a.correction < b.correction;
              });
    std::map<std::pair<size_t, size_t>, std::vector<size_t>> frameArcs;
    {
        std::vector<ChineseWordArc> kept;
        kept.reserve(out.size());
        std::move(out.begin(), out.begin() + first, std::back_inserter(kept));
        for (size_t i = first; i < out.size(); ++i) {
            const bool frameHead = i == first ||
                                   out[i].from != out[i - 1].from ||
                                   out[i].to != out[i - 1].to;
            if (out[i].unknownSingle && !frameHead) {
                continue;
            }
            frameArcs[{out[i].from, out[i].to}].push_back(kept.size());
            kept.push_back(std::move(out[i]));
        }
        out = std::move(kept);
    }
    if (ime->frameSize() != 0) {
        std::vector<char> keep(out.size(), 1);
        for (auto &[frame, indices] : frameArcs) {
            if (frame.first == offset || indices.size() <= ime->frameSize()) {
                continue;
            }
            std::stable_sort(indices.begin(), indices.end(),
                             [&](size_t a, size_t b) {
                                 return out[a].frameScore > out[b].frameScore;
                             });
            for (size_t i = ime->frameSize(); i < indices.size(); ++i) {
                keep[indices[i]] = 0;
            }
        }
        std::vector<ChineseWordArc> filtered;
        filtered.reserve(out.size());
        for (size_t i = 0; i < out.size(); ++i) {
            if (keep[i]) {
                filtered.push_back(std::move(out[i]));
            }
        }
        out = std::move(filtered);
    }
}

} // namespace pinyin
