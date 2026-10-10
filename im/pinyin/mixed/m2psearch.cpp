/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "m2psearch.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <libime/pinyin/pinyincontext.h>
#include <libime/pinyin/pinyinime.h>
#include <libime/pinyin/pinyinmatchstate.h>
#include <libime/pinyin/shuangpinprofile.h>

#include "chinesearcoracle.h"

namespace pinyin {
namespace {

// One edge of the mixed hypothesis graph of a key. Chinese edges are dictionary
// words over the whole-raw segment graph; English edges are the arcs the
// existing admission filters admitted. Both carry the SegmentationArc the
// composer, ranker and placement consume.
struct M2PEdge {
    SegmentationArc arc;
    bool chinese = false;
    bool correction = false;
    float adjust = 0.0F;
    const libime::WordNode *node = nullptr; // stable WordNode in the table
};

// M2+r4 section 4.1: the English-shape signature of a path prefix. The terminal
// already groups arrivals by englishShapeOf, the ordered list of the raw
// intervals of their English arcs, and it protects two members of every shape
// (the canonical respelling and its literal surface must coexist, D071). The
// frontier guarantee needs the same grouping at every position, where
// re-walking the arc list per state would cost more than the window it feeds,
// so the signature is a 64-bit hash maintained incrementally along the path: an
// English arc combines its (rawBegin, rawEnd), a Chinese arc inherits its
// parent's value. Two surfaces of one span share a signature on purpose — that
// is what makes them one shape here and at the terminal.
inline constexpr uint64_t M2P_HASH_STEP = 0x9e3779b97f4a7c15ULL;
// The signature of "no English arc yet", i.e. the empty shape, and of no
// English surface yet. A class-0 state carries the empty shape and nothing else
// can.
inline constexpr uint64_t M2P_EMPTY_SHAPE = 0xcbf29ce484222325ULL;
inline constexpr uint64_t M2P_EMPTY_SURFACES = 0x100000001b3ULL;

uint64_t m2pHashMix(uint64_t hash, uint64_t value) {
    uint64_t mixed = (hash ^ value) + M2P_HASH_STEP;
    mixed ^= mixed >> 30;
    mixed *= 0xbf58476d1ce4e5b9ULL;
    mixed ^= mixed >> 27;
    mixed *= 0x94d049bb133111ebULL;
    mixed ^= mixed >> 31;
    return mixed;
}

uint64_t m2pExtendShape(uint64_t hash, size_t begin, size_t end) {
    return m2pHashMix(m2pHashMix(hash, begin), end);
}

uint64_t m2pExtendSurfaces(uint64_t hash, const std::string &text) {
    uint64_t value = m2pHashMix(hash, text.size());
    for (const char c : text) {
        value = m2pHashMix(value, static_cast<unsigned char>(c));
    }
    return value;
}

// One path state (section 2 item 2).
struct M2PState {
    double cost = 0.0;    // mixed-search structural cost, A-prime units
    float lmScore = 0.0F; // LM log-prob of the Chinese words, higher is better
    // Raw bytes covered by English arcs; an English arc carries no LM word, so
    // it is priced per byte inside the LM term (M2+b section 2 item 1).
    double englishBytes = 0.0;
    libime::State lm;  // cross-word LM context at this raw position
    size_t from = 0;   // raw position the state was reached from
    int arc = -1;      // edge used to arrive
    int previous = -1; // index of the parent state within states[from]
    bool lastEnglish = false;
    size_t englishRuns = 0; // maximal consecutive-English runs on the prefix
    bool inChineseRun = false;
    bool runCorrection =
        false;               // the current Chinese run already paid its delta
    bool correction = false; // any correction arc on the whole prefix
    // M2+r4: the English-shape signature and the ordered English surfaces of
    // this prefix (see m2pExtendShape). Never read by a comparator that existed
    // before r4, so an r4-off search is bit-identical.
    uint64_t shapeHash = M2P_EMPTY_SHAPE;
    uint64_t surfaceHash = M2P_EMPTY_SURFACES;
};

// The frame key of the state prune (see frameOf below): the span a state
// arrived on, plus its surface when that span is English.
using M2PFrameKey = std::tuple<size_t, size_t, std::string>;

// Mixed-surface run-split purity (test/testpinyin.cpp:2859, R13): a candidate
// must alternate Han runs and ASCII *letter* runs, so an English dictionary hit
// whose display carries an apostrophe or a digit ("phone's") is not a legal
// mixed reading. A-prime hid those readings behind its pool cap; the bounded
// pass surfaces them because it keeps the whole English side, so the invariant
// is enforced where the arcs enter this search instead of by accident.
bool lettersOnlySurface(const std::string &text) {
    return std::all_of(text.begin(), text.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    });
}

// Retention classes, identical to Phase 3A-2's mapping but computed from the
// incremental counters instead of re-walking the arc list.
size_t classIndex(size_t runs, bool lastEnglish, size_t cap) {
    if (cap == 0) {
        return 0;
    }
    if (runs == 0) {
        return 0;
    }
    if (runs >= cap) {
        return 2 * cap - 1;
    }
    return 2 * runs - (lastEnglish ? 0 : 1);
}

// The exact bucket A-prime's gap solver used when it turned a gap reading into
// a mixed-search arc (chinesegapsolver.cpp: confidence 0.55/0.75,
// boundaryConfidence 0.85/1.0, provenance Correction/Exact).
SegmentationArc chineseRunProbe(bool correction) {
    SegmentationArc arc;
    arc.confidence = correction ? 0.55F : 0.75F;
    arc.boundaryConfidence = correction ? 0.85F : 1.0F;
    return arc;
}
// The terminal selection and the hand-off to the composer both use the same
// `total` cost (see M2P_DEFAULT_LM_LAMBDA / KAPPA docs): the D075-validated r4
// baseline ranks arrivals by the LM-aware total and passes that same value
// downstream, so a reconstructed path carries `total` as its cost from the
// moment it is built.

// The M2+ word-arc builder: parseChineseGraph over `raw`, one
// collectChineseWordArcs pass with offsets shifted by `offset`, the pure-Han
// guard and the per-(from,to) reading bound. build() applies it to the whole
// raw; M2+r3's gapWordArcs applies it unchanged to one gap substring.
bool buildWordArcs(const libime::PinyinIME *ime, ChineseInputMode mode,
                   libime::PinyinMatchState &matchState, std::string_view raw,
                   size_t offset, std::vector<ChineseWordArc> &arcs,
                   std::vector<char> *arcStarts = nullptr) {
    auto graph = parseChineseGraph(ime, mode, raw);
    if (!graph) {
        return false;
    }
    // Section 2 item 1: ONE matchPrefix over the segment graph of `raw`.
    collectChineseWordArcs(ime, matchState, *graph, offset, arcs);
    if (arcStarts) {
        // The r3 suffix re-parse rule reads the positions at which a whole-raw
        // arc starts before any M2+-only filter, so record it here.
        arcStarts->assign(raw.size() + 1, 0);
        for (const auto &arc : arcs) {
            if (arc.from < arcStarts->size()) {
                (*arcStarts)[arc.from] = 1;
            }
        }
    }
    // A-prime emitted a gap reading only when its text was pure Han
    // (chinesegapsolver.cpp: any byte < 0x80 rejects the reading), so a word
    // the dictionary could not turn into Han never became mixed output. Same
    // guard at the edge table, so M2+ cannot surface what A-prime filtered.
    const auto pureHan = [](const ChineseWordArc &arc) {
        return !std::any_of(arc.word.begin(), arc.word.end(),
                            [](unsigned char c) { return c < 0x80; });
    };
    arcs.erase(std::remove_if(arcs.begin(), arcs.end(),
                              [&pureHan](const ChineseWordArc &arc) {
                                  return !pureHan(arc);
                              }),
               arcs.end());
    // Per-(from,to) reading bound. The collector already ordered the arcs by
    // (from, to, frameScore desc, total order), so the first bounded prefix of
    // each contiguous frame group is its best-scoring set.
    {
        std::vector<ChineseWordArc> kept;
        kept.reserve(arcs.size());
        std::pair<size_t, size_t> frame = {0, 0};
        size_t inFrame = 0;
        const size_t maxReadings = m2pMaxReadingsPerFrame();
        for (auto &arc : arcs) {
            const std::pair<size_t, size_t> here = {arc.from, arc.to};
            if (here != frame) {
                frame = here;
                inFrame = 0;
            }
            if (++inFrame > maxReadings) {
                continue;
            }
            kept.push_back(std::move(arc));
        }
        arcs = std::move(kept);
    }
    return true;
}
} // namespace

bool mixedM2PEnabled() {
    // M2+ is the product mixed search path (D075).
    return true;
}

bool mixedM2PR3Enabled() {
    // r3 is the accepted rule set behind the r4 policy.
    return true;
}

bool mixedM2PR3ActiveFor(ChineseInputMode mode) {
    return mode == ChineseInputMode::Shuangpin;
}

bool mixedM2PR4Enabled() {
    // r4 (English-shape-aware bounded frontier retention) is the D075-accepted
    // product policy.
    return true;
}

size_t m2pMaxReadingsPerFrame() { return M2P_MAX_READINGS_PER_FRAME; }
size_t m2pMaxStatesPerFrame() { return M2P_MAX_STATES_PER_FRAME; }

MixedLmStateSearch::Config mixedM2PConfig() {
    static const MixedLmStateSearch::Config config = [] {
        MixedLmStateSearch::Config out;
        out.retentionBeam = M2P_DEFAULT_RETENTION_BEAM;
        out.lmLambda = M2P_DEFAULT_LM_LAMBDA;
        out.englishKappa = M2P_DEFAULT_ENGLISH_KAPPA;
        out.englishStatePolicy = MixedLmStateSearch::EnglishStatePolicy::Reset;
        return out;
    }();
    return config;
}

ChineseWordArcTable::ChineseWordArcTable(libime::PinyinIME *ime) : ime_(ime) {
    if (!ime_) {
        return;
    }
    model_ = ime_->model();
    context_ = std::make_unique<libime::PinyinContext>(ime_);
    matchState_ = std::make_unique<libime::PinyinMatchState>(context_.get());
    eos_ = std::make_unique<libime::WordNode>(std::string(),
                                              model_->endSentence());
}

ChineseWordArcTable::~ChineseWordArcTable() = default;

void ChineseWordArcTable::setMode(ChineseInputMode mode) {
    if (mode_ == mode) {
        return;
    }
    mode_ = mode;
    context_->setUseShuangpin(mode == ChineseInputMode::Shuangpin);
    raw_.clear();
    arcs_.clear();
    nodes_.clear();
    wholeArcStarts_.clear();
    gapTables_.clear();
}

bool ChineseWordArcTable::build(std::string_view raw) {
    // The raw is recorded even when the table ends up with no Chinese word: a
    // key whose Chinese side is unexplainable still has to run the pass over
    // its English edges.
    raw_.assign(raw);
    arcs_.clear();
    nodes_.clear();
    wholeArcStarts_.assign(raw.size() + 1, 0);
    gapTables_.clear();
    if (!ime_ || raw.empty()) {
        return false;
    }
    if (!buildWordArcs(ime_, mode_, *matchState_, raw, 0, arcs_,
                       &wholeArcStarts_)) {
        return false;
    }
    for (const auto &arc : arcs_) {
        nodes_.emplace_back(arc.word, arc.index);
    }
    return !arcs_.empty();
}

const std::vector<ChineseWordArc> &
ChineseWordArcTable::gapWordArcs(size_t b, size_t s) const {
    auto [it, inserted] = gapTables_.try_emplace({b, s});
    if (inserted && ime_ && b < s && s <= raw_.size()) {
        buildWordArcs(ime_, mode_, *matchState_,
                      std::string_view(raw_).substr(b, s - b), b,
                      it->second.arcs);
        for (const auto &arc : it->second.arcs) {
            it->second.words.emplace_back(arc.word, arc.index);
        }
    }
    return it->second.arcs;
}

MixedLmStateSearch::MixedLmStateSearch(Config config,
                                       const libime::LanguageModelBase *model,
                                       const ChineseWordArcTable &chinese,
                                       const libime::State &seed)
    : config_(config), model_(model), chinese_(&chinese), seed_(seed) {}

libime::State
MixedLmStateSearch::transitionAcrossEnglish(const libime::State &before) const {
    switch (config_.englishStatePolicy) {
    case EnglishStatePolicy::Preserve:
        return before;
    case EnglishStatePolicy::Reset:
        break;
    }
    return model_->nullState();
}

std::vector<SegmentationPath> MixedLmStateSearch::search(
    std::string_view raw, const IEnglishArcOracle &english, size_t topK) const {
    std::vector<SegmentationPath> result;
    const size_t n = raw.size();
    // M2+r4: the English-shape-aware frontier guarantee (the D075-accepted
    // product policy, see mixedM2PR4Enabled).
    const bool r4 = mixedM2PR4Enabled();
    if (n == 0 || !model_ || !chinese_ || chinese_->raw() != raw) {
        return result;
    }

    // ---- the one graph of this key: Chinese word edges + English edges ----
    std::vector<M2PEdge> edges;
    edges.reserve(chinese_->arcs().size() + 16);
    std::vector<std::vector<int>> byFrom(n + 1);
    for (const auto &word : chinese_->arcs()) {
        if (word.from >= n || word.to > n || word.to <= word.from) {
            continue;
        }
        M2PEdge edge;
        edge.arc.rawBegin = word.from;
        edge.arc.rawEnd = word.to;
        edge.arc.source = SegmentSource::Chinese;
        edge.arc.provenance = word.correction ? CandidateProvenance::Correction
                                              : CandidateProvenance::Exact;
        edge.arc.resolvedOutput = word.word;
        edge.chinese = true;
        edge.correction = word.correction;
        edge.adjust = word.adjust;
        edge.node = &chinese_->node(
            static_cast<size_t>(&word - chinese_->arcs().data()));
        byFrom[word.from].push_back(static_cast<int>(edges.size()));
        edges.push_back(std::move(edge));
    }
    // Section 2 item 10: the English side is the A-prime side unchanged.
    for (size_t i = 0; i < n; ++i) {
        for (const auto &arc : english.arcsAt(raw, i, n - i)) {
            if (arc.source != SegmentSource::English || arc.rawBegin != i ||
                arc.rawEnd <= i || arc.rawEnd > n) {
                continue;
            }
            if (!lettersOnlySurface(arc.resolvedOutput)) {
                continue;
            }
            M2PEdge edge;
            edge.arc = arc;
            byFrom[i].push_back(static_cast<int>(edges.size()));
            edges.push_back(std::move(edge));
        }
    }

    // M2+r3 (the accepted rule set behind the r4 policy): for each distinct
    // end offset b < n of an admitted English arc, append the arcs of a suffix
    // re-parse of [b, n) whenever the whole-raw parse has no Chinese arc
    // starting at b, deduplicated by (start, end, word) against the whole-raw
    // arcs and each other. The appended arcs enter after the English edges, so
    // the existing edges keep their indices. The rule is scoped to shuangpin:
    // in pinyin no suffix table is parsed or appended and the graph stays the
    // whole-raw-plus-English one.
    const bool r3Active = mixedM2PR3ActiveFor(chinese_->mode());
    if (r3Active) {
        std::set<std::tuple<size_t, size_t, std::string>> seen;
        for (const auto &word : chinese_->arcs()) {
            seen.emplace(word.from, word.to, word.word);
        }
        const auto appendTable = [&](size_t b, size_t s) {
            const auto &arcs = chinese_->gapWordArcs(b, s);
            for (size_t k = 0; k < arcs.size(); ++k) {
                const auto &word = arcs[k];
                if (word.from >= n || word.to > n || word.to <= word.from ||
                    !seen.emplace(word.from, word.to, word.word).second) {
                    continue;
                }
                M2PEdge edge;
                edge.arc.rawBegin = word.from;
                edge.arc.rawEnd = word.to;
                edge.arc.source = SegmentSource::Chinese;
                edge.arc.provenance = word.correction
                                          ? CandidateProvenance::Correction
                                          : CandidateProvenance::Exact;
                edge.arc.resolvedOutput = word.word;
                edge.chinese = true;
                edge.correction = word.correction;
                edge.adjust = word.adjust;
                edge.node = &chinese_->gapNode(b, s, k);
                byFrom[word.from].push_back(static_cast<int>(edges.size()));
                edges.push_back(std::move(edge));
            }
        };
        std::vector<char> isEnd(n + 1, 0);
        std::vector<size_t> ends;
        for (const auto &edge : edges) {
            if (edge.chinese || edge.arc.rawEnd >= n ||
                isEnd[edge.arc.rawEnd]) {
                continue;
            }
            isEnd[edge.arc.rawEnd] = 1;
            ends.push_back(edge.arc.rawEnd);
        }
        std::sort(ends.begin(), ends.end());
        const auto &wholeStarts = chinese_->wholeRawArcStarts();
        for (const size_t b : ends) {
            if (wholeStarts[b]) {
                continue;
            }
            appendTable(b, n);
        }
    }

    const double runBase = MixedSegmentationSearch::arcCostOf(
        chineseRunProbe(false), config_.weakBoundaryWeight);
    const double correctionDelta =
        MixedSegmentationSearch::arcCostOf(chineseRunProbe(true),
                                           config_.weakBoundaryWeight) -
        runBase;

    const size_t beam = std::max<size_t>(1, config_.retentionBeam);
    const size_t cap = config_.englishSegmentCap;
    const size_t numClasses = cap == 0 ? 1 : 2 * cap;

    std::vector<std::vector<M2PState>> states(n + 1);
    states[0].emplace_back();
    states[0].front().lm = seed_;

    // The frame key of the state prune. Chinese readings of one (from,to) frame
    // share that frame's budget exactly as LibIME prunes one frame's word list;
    // two English surfaces of one span do not, because the product contract
    // (D071) makes the canonical respelling and the literal typed surface two
    // distinct readings that must both survive to compose.
    const auto frameOf = [&edges](const M2PState &state) {
        if (state.arc < 0) {
            return std::tuple<size_t, size_t, std::string>{0, 0, std::string{}};
        }
        const auto &arc = edges[static_cast<size_t>(state.arc)].arc;
        return std::tuple<size_t, size_t, std::string>{
            arc.rawBegin, arc.rawEnd,
            arc.source == SegmentSource::English ? arc.resolvedOutput
                                                 : std::string{}};
    };

    // M2+b section 2 item 1: the sort key is the LM-aware total, so the
    // cross-word LM cost and the English length price compete with the
    // structural cost at every retention window instead of only breaking its
    // ties.
    const double lambda = config_.lmLambda;
    const double kappa = config_.englishKappa;
    const auto totalOf = [lambda, kappa](const M2PState &state) {
        return state.cost + lambda * (static_cast<double>(-state.lmScore) +
                                      kappa * state.englishBytes);
    };

    const auto stateLess = [&edges, &totalOf](const M2PState &a,
                                              const M2PState &b) {
        const double ta = totalOf(a);
        const double tb = totalOf(b);
        if (ta != tb) {
            return ta < tb;
        }
        if (a.lmScore != b.lmScore) {
            return a.lmScore > b.lmScore;
        }
        // Total order (section 2 item 9): arc bounds, then text bytes, then
        // source, then the deterministic position order of the edge table.
        if (a.arc < 0 || b.arc < 0) {
            return a.arc > b.arc; // only the seed state has no incoming edge
        }
        const auto &ea = edges[static_cast<size_t>(a.arc)];
        const auto &eb = edges[static_cast<size_t>(b.arc)];
        if (ea.arc.rawBegin != eb.arc.rawBegin) {
            return ea.arc.rawBegin < eb.arc.rawBegin;
        }
        if (ea.arc.rawEnd != eb.arc.rawEnd) {
            return ea.arc.rawEnd < eb.arc.rawEnd;
        }
        if (ea.arc.resolvedOutput != eb.arc.resolvedOutput) {
            return ea.arc.resolvedOutput < eb.arc.resolvedOutput;
        }
        if (ea.arc.source != eb.arc.source) {
            return ea.arc.source < eb.arc.source;
        }
        return a.arc < b.arc;
    };

    // M2+r4 method B, section 4.2: the shape guarantee. Runs after the r3
    // window on the same class and only ever adds states, so the retained set
    // is a superset of r3's. `cls` is already in stateLess order, so the first
    // occurrence of a shape is that shape's best member: the shapes come out
    // ordered by their best member for free. The first K = B shapes are topped
    // up to min(S, members) states, S = kTerminalSiblingsPerShape, in the order
    // the terminal uses inside a shape — a member whose English surface is not
    // represented yet before a member that repeats one, each group in stateLess
    // order — under the same per-frame bound counted inside the shape.
    // `shapeOf`/`surfaceOf` are the signatures captured before the window moved
    // the states it kept, so the guarantee never reads a moved-from state.
    const size_t shapeQuota = kTerminalSiblingsPerShape;
    const auto r4Guarantee = [&](std::vector<M2PState> &cls,
                                 const std::vector<char> &windowKept,
                                 const std::vector<M2PState> &kept,
                                 const std::vector<uint64_t> &shapeOf,
                                 const std::vector<uint64_t> &surfaceOf,
                                 size_t maxStatesPerFrame) {
        std::vector<uint64_t> order;
        std::map<uint64_t, std::vector<size_t>> members;
        for (size_t ci = 0; ci < cls.size(); ++ci) {
            if (members.find(shapeOf[ci]) == members.end()) {
                order.push_back(shapeOf[ci]);
            }
            members[shapeOf[ci]].push_back(ci);
        }
        std::map<uint64_t, size_t> held;
        std::map<uint64_t, std::set<uint64_t>> heldSurfaces;
        for (const auto &state : kept) {
            ++held[state.shapeHash];
            heldSurfaces[state.shapeHash].insert(state.surfaceHash);
        }
        const size_t shapeCap = std::max<size_t>(1, beam);
        std::vector<M2PState> added;
        for (size_t s = 0; s < order.size() && s < shapeCap; ++s) {
            const uint64_t shape = order[s];
            if (shape == M2P_EMPTY_SHAPE) {
                // The empty shape is class 0's only shape and class 0 is
                // untouched by construction: a pure-Chinese window keeps B
                // states of it whenever B exist.
                continue;
            }
            const auto &indices = members[shape];
            const size_t target = std::min(shapeQuota, indices.size());
            const auto heldIt = held.find(shape);
            size_t have = heldIt == held.end() ? 0 : heldIt->second;
            if (have >= target) {
                continue;
            }
            std::map<M2PFrameKey, size_t> perShapeFrame;
            for (const auto &state : kept) {
                if (state.shapeHash == shape) {
                    ++perShapeFrame[frameOf(state)];
                }
            }
            std::set<uint64_t> surfaces = heldSurfaces[shape];
            std::vector<size_t> fresh;
            std::vector<size_t> repeated;
            for (const size_t ci : indices) {
                if (windowKept[ci]) {
                    continue;
                }
                if (surfaces.insert(surfaceOf[ci]).second) {
                    fresh.push_back(ci);
                } else {
                    repeated.push_back(ci);
                }
            }
            for (size_t round = 0; round < 2; ++round) {
                const std::vector<size_t> &list = round == 0 ? fresh : repeated;
                for (const size_t ci : list) {
                    if (have >= target) {
                        break;
                    }
                    auto &state = cls[ci];
                    if (++perShapeFrame[frameOf(state)] > maxStatesPerFrame) {
                        continue;
                    }
                    ++have;
                    added.push_back(std::move(state));
                }
            }
        }
        return added;
    };

    for (size_t i = 0; i < n; ++i) {
        if (states[i].empty()) {
            continue;
        }
        // Section 2 item 4: per-class retention of B — never a cross-class
        // prune, and no Top-1 freezing of a Chinese span. Inside a class the
        // LibIME-style per-(from,to) frame prune bounds how many readings of
        // one frame can occupy the window.
        const size_t maxStatesPerFrame = m2pMaxStatesPerFrame();
        std::vector<std::vector<M2PState>> classes(numClasses);
        for (auto &state : states[i]) {
            classes[classIndex(state.englishRuns, state.lastEnglish, cap)]
                .push_back(std::move(state));
        }
        states[i].clear();
        for (auto &cls : classes) {
            if (cls.empty()) {
                continue;
            }
            std::stable_sort(cls.begin(), cls.end(), stateLess);
            // r4 reads the two signatures after the window has moved the states
            // it kept, so they are captured from live states once, here.
            std::vector<uint64_t> shapeOf;
            std::vector<uint64_t> surfaceOf;
            std::vector<char> windowKept;
            if (r4) {
                shapeOf.resize(cls.size());
                surfaceOf.resize(cls.size());
                windowKept.assign(cls.size(), 0);
                for (size_t ci = 0; ci < cls.size(); ++ci) {
                    shapeOf[ci] = cls[ci].shapeHash;
                    surfaceOf[ci] = cls[ci].surfaceHash;
                }
            }
            std::map<M2PFrameKey, size_t> perFrame;
            std::vector<M2PState> kept;
            kept.reserve(std::min(cls.size(), beam));
            for (size_t ci = 0; ci < cls.size(); ++ci) {
                auto &state = cls[ci];
                if (++perFrame[frameOf(state)] > maxStatesPerFrame) {
                    continue;
                }
                if (r4) {
                    windowKept[ci] = 1;
                }
                kept.push_back(std::move(state));
                if (kept.size() == beam) {
                    break;
                }
            }
            std::vector<M2PState> added;
            if (r4) {
                added = r4Guarantee(cls, windowKept, kept, shapeOf, surfaceOf,
                                    maxStatesPerFrame);
            }
            states[i].insert(states[i].end(),
                             std::make_move_iterator(kept.begin()),
                             std::make_move_iterator(kept.end()));
            states[i].insert(states[i].end(),
                             std::make_move_iterator(added.begin()),
                             std::make_move_iterator(added.end()));
        }

        for (const int edgeIndex : byFrom[i]) {
            const auto &edge = edges[static_cast<size_t>(edgeIndex)];
            const double englishCost = MixedSegmentationSearch::arcCostOf(
                edge.arc, config_.weakBoundaryWeight);
            for (size_t p = 0; p < states[i].size(); ++p) {
                const auto &parent = states[i][p];
                M2PState arrival;
                arrival.from = i;
                arrival.arc = edgeIndex;
                arrival.previous = static_cast<int>(p);
                if (edge.chinese) {
                    libime::State next;
                    arrival.lmScore =
                        parent.lmScore +
                        model_->score(parent.lm, *edge.node, next) +
                        edge.adjust;
                    double cost = parent.cost;
                    bool runCorrection = parent.runCorrection;
                    if (!parent.inChineseRun) {
                        cost += runBase;
                        runCorrection = edge.correction;
                        if (edge.correction) {
                            cost += correctionDelta;
                        }
                    } else if (edge.correction && !parent.runCorrection) {
                        cost += correctionDelta;
                        runCorrection = true;
                    }
                    arrival.cost = cost;
                    arrival.lm = std::move(next);
                    arrival.lastEnglish = false;
                    arrival.englishRuns = parent.englishRuns;
                    arrival.inChineseRun = true;
                    arrival.runCorrection = runCorrection;
                    arrival.correction = parent.correction || edge.correction;
                    // A Chinese arc is not part of the shape: the prefix keeps
                    // its parent's signature.
                    arrival.shapeHash = parent.shapeHash;
                    arrival.surfaceHash = parent.surfaceHash;
                } else {
                    arrival.cost = parent.cost + englishCost;
                    arrival.lmScore = parent.lmScore;
                    arrival.englishBytes =
                        parent.englishBytes +
                        (edge.arc.rawEnd - edge.arc.rawBegin);
                    arrival.lm = transitionAcrossEnglish(parent.lm);
                    arrival.lastEnglish = true;
                    arrival.englishRuns =
                        parent.englishRuns + (parent.lastEnglish ? 0 : 1);
                    arrival.inChineseRun = false;
                    arrival.runCorrection = false;
                    arrival.correction =
                        parent.correction ||
                        edge.arc.provenance == CandidateProvenance::Correction;
                    arrival.shapeHash = m2pExtendShape(
                        parent.shapeHash, edge.arc.rawBegin, edge.arc.rawEnd);
                    arrival.surfaceHash = m2pExtendSurfaces(
                        parent.surfaceHash, edge.arc.resolvedOutput);
                }
                states[edge.arc.rawEnd].push_back(std::move(arrival));
            }
        }
    }

    if (states[n].empty()) {
        return result;
    }

    // Requirement 6: EOS once, at the end of the raw. The state it produces is
    // discarded (nothing continues after the raw) and the EOS WordNode lives in
    // the table, so the pointer UserLanguageModel::score stores stays valid for
    // as long as the discarded state does.
    for (auto &state : states[n]) {
        libime::State ignored;
        state.lmScore += model_->score(state.lm, chinese_->eos(), ignored);
    }
    std::stable_sort(states[n].begin(), states[n].end(), stateLess);

    // Reconstruct: merge each maximal Chinese word run into one Chinese arc, so
    // a surviving path has exactly the arc shape A-prime handed downstream.
    std::vector<SegmentationPath> arrivals;
    arrivals.reserve(states[n].size());
    for (auto &state : states[n]) {
        std::vector<int> sequence;
        size_t cursor = n;
        int index = static_cast<int>(&state - states[n].data());
        while (cursor != 0 && index >= 0) {
            const auto &entry = states[cursor][static_cast<size_t>(index)];
            if (entry.arc < 0) {
                break;
            }
            sequence.push_back(entry.arc);
            cursor = entry.from;
            index = entry.previous;
        }
        std::reverse(sequence.begin(), sequence.end());
        SegmentationPath path;
        // The terminal selection and the composer hand-off both consume the
        // LM-aware total (the D075-validated baseline), so it is frozen onto
        // the path here and never re-derived.
        path.cost = totalOf(state);
        for (size_t k = 0; k < sequence.size(); ++k) {
            const auto &edge = edges[static_cast<size_t>(sequence[k])];
            if (!edge.chinese) {
                path.arcs.push_back(edge.arc);
                continue;
            }
            std::string text = edge.arc.resolvedOutput;
            bool runCorrection = edge.correction;
            size_t end = edge.arc.rawEnd;
            size_t j = k + 1;
            while (j < sequence.size() &&
                   edges[static_cast<size_t>(sequence[j])].chinese) {
                const auto &next = edges[static_cast<size_t>(sequence[j])];
                text += next.arc.resolvedOutput;
                runCorrection = runCorrection || next.correction;
                end = next.arc.rawEnd;
                ++j;
            }
            SegmentationArc run = chineseRunProbe(runCorrection);
            run.rawBegin = edge.arc.rawBegin;
            run.rawEnd = end;
            run.source = SegmentSource::Chinese;
            run.provenance = runCorrection ? CandidateProvenance::Correction
                                           : CandidateProvenance::Exact;
            run.resolvedOutput = std::move(text);
            path.arcs.push_back(std::move(run));
            k = j - 1;
        }
        arrivals.push_back(std::move(path));
    }
    result = MixedSegmentationSearch::selectTerminal(
        std::move(arrivals), cap, beam, std::max<size_t>(1, topK),
        /*reserveEnglishShapes=*/true);
    return result;
}

} // namespace pinyin
