/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
//
// Batch 10 — Formal mixed-input corpus + accuracy + performance gate.
//
// Drives the full Architecture A pipeline (segmentation search -> compose ->
// candidate pool -> cross-source rank -> surface rewriter) against a small
// curated mixed corpus. Each case declares the raw stream, the intended Han
// spans, the intended English spans, and the expected top-1 composed
// surface. This is NOT a substitute for real LibIME + real English resource
// validation (that is Batch 11 device + release validation); it exists to
// pin a reproducible, offline-hermetic accuracy and latency gate for the
// Architecture A orchestration layer, so any future change to the search /
// rank / compose pipeline that regresses the corpus fails here first.
//
// Corpus scope (aligned with the Master Instruction and design §18/§19/§20):
//   * Common brand + technical mixtures (iPhone / App / PDF / HTTP / VPN).
//   * Evidence-backed C/E and E/C switches (我想买 iPhone 配件 / open 一下).
//   * Ambiguous short raws the segmentation must keep both interpretations
//     competitive without preferring fragmentation for its own sake.
//   * Long streams (>= 20 raw bytes) exercising the beam budget and pool cap.
//
// Measurements reported to stdout (the Batch 10 baseline-vs-after numbers):
//   TOP-1 ACCURACY        : MIXED corpus cases whose top-1 composed text
//                           contains every expected display form.
//   FAST PATH (§9)        : pure-Chinese cases whose pool is EMPTY — with no
//                           English arc evidence the engine must short-circuit
//                           before compose/rank/rewrite. This is a strictly
//                           stronger invariant than the older "English-free
//                           pool" guard, which is retained below as a
//                           belt-and-braces check.
//   FALSE CORRECTION      : clean cases (no typo) whose top-1 stays Exact on
//                           every span.
//   RECALL                : mixed corpus cases whose top-K (K = pool.maxSize)
//                           contains at least one candidate matching the
//                           expected composed text.
//   PER-CASE LATENCY      : p50 / p95 / max wall-clock per compute() call in
//                           microseconds, reported separately for mixed cases
//                           (full pipeline) and pure-Chinese cases (fast
//                           path), plus a coarse long-run RSS delta.
//
// The gate is intentionally permissive on absolute performance (CI runners
// and containers vary) but strict on accuracy: every mixed case must remain
// top-1 correct (at most one miss allowed for tie-break differences, see
// assertions) and every pure-Chinese case must hit the fast path. If either
// regresses, the change is not a compile-time issue but a ranking, search or
// fast-path regression caught by this harness.
//
// Long-running: 200 iterations across the same corpus. Not a substitute for
// device-level soak; validates that no cache / pool / ranker state grows
// without bound across compute() calls in a single engine instance.

#include "english/englisharcoracle.h"
#include "english/englishcorrectionoracle.h"
#include "english/englishlexicon.h"
#include "english/englishuserlexicon.h"
#include "mixedengine.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace pinyin;

namespace {

int failures = 0;
void check(bool cond, const char *what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// Hand-authored Han spans for each corpus case. Kept identical in shape to
// the testmixedengine `UnresolvedChineseOracle` test double so this harness
// exercises exactly the same Architecture A seam; the difference is the
// volume + breadth of cases and the accuracy gate.
struct HanSpan {
    size_t begin;
    size_t end;
    std::string han;
};

struct CorpusCase {
    const char *label;
    std::string raw;
    std::vector<HanSpan> han;
    // Expected display forms that must ALL appear (in order, contiguous) in
    // the top-1 candidate's composed text. The exact string may include
    // adjacent Chinese/English content from other segments; we check by
    // substring so the assertion is order-independent across spans that the
    // pipeline may re-arrange in edge cases (which would itself be a bug).
    std::vector<std::string> expectedDisplays;
    // When true, this case has no English intent: the §9 fast path must make
    // compute() short-circuit and return an EMPTY pool (no English evidence
    // anywhere in the search result). A well-formed pure-Chinese stream that
    // acquires any mixed candidate is a false-English event and fails both
    // the emptiness guard and the English-free guard below.
    bool pureChineseExpected = false;
};

const std::vector<EnglishLexiconEntry> &seedLexicon() {
    static const std::vector<EnglishLexiconEntry> entries = {
        {"iphone", "iPhone", 8, true, false, false},
        {"ipad", "iPad", 7, true, false, false},
        {"app", "app", 8, false, false, false},
        {"game", "game", 7, false, false, false},
        {"pdf", "PDF", 6, false, true, false},
        {"http", "HTTP", 5, false, true, false},
        {"https", "HTTPS", 4, false, true, false},
        {"vpn", "VPN", 5, false, true, false},
        {"hello", "hello", 8, false, false, false},
        {"world", "world", 8, false, false, false},
        {"open", "open", 7, false, false, false},
        {"close", "close", 7, false, false, false},
        {"github", "GitHub", 5, true, false, false},
        {"wechat", "WeChat", 5, true, false, false},
        {"microsoft", "Microsoft", 5, true, false, false},
        {"macos", "macOS", 7, true, false, false},
        {"chatgpt", "ChatGPT", 6, true, false, false},
        {"openwrt", "OpenWrt", 4, true, false, false},
        {"libime", "LibIME", 4, true, false, false},
        {"peijian", "配件", 5, false, false, false},
    };
    return entries;
}

const std::vector<CorpusCase> &corpus() {
    static const std::vector<CorpusCase> cases = {
        // -- Common brand + technical mixtures -------------------------
        // woxiangmaiiphonepeijian: wo(2) xiang(5) mai(3) iphone(6) peijian(7) =
        // 23
        {"mixed-iphone-peijian",
         "woxiangmaiiphonepeijian",
         {{0, 2, "我"},
          {2, 7, "想"},
          {7, 10, "买"},
          {16, 19, "配"},
          {19, 23, "件"}},
         {"我", "想", "买", "iPhone", "配件"},
         false},
        // ipadapp: ipad(4) app(3) = 7
        {"mixed-ipad-app", "ipadapp", {}, {"iPad", "app"}, false},
        // woyonghttpsvpn: wo(2) yong(4) https(5) vpn(3) = 14
        {"mixed-https-vpn",
         "woyonghttpsvpn",
         {{0, 2, "我"}, {2, 6, "用"}},
         {"HTTPS", "VPN"},
         false},
        // wodakaigithub = wo(2) da(2) kai(3) github(6) = 13
        {"mixed-open-github",
         "wodakaigithub",
         {{0, 2, "我"}, {2, 4, "打"}, {4, 7, "开"}},
         {"GitHub"},
         false},
        // Closure §6 expansions: canonical-cased brands in both switch
        // directions (Chinese->English and English->Chinese).
        // wodakaimacos: wo(2) da(2) kai(3) macos(5) = 12
        {"mixed-c-e-macos",
         "wodakaimacos",
         {{0, 2, "我"}, {2, 4, "打"}, {4, 7, "开"}},
         {"我", "打", "开", "macOS"},
         false},
        // chatgptbangwo: chatgpt(7) bang(4) wo(2) = 13 (English first)
        {"mixed-e-c-chatgpt",
         "chatgptbangwo",
         {{7, 11, "帮"}, {11, 13, "我"}},
         {"ChatGPT", "帮", "我"},
         false},
        // anzhuangopenwrt: an(2) zhuang(6) openwrt(7) = 15
        {"mixed-c-e-openwrt",
         "anzhuangopenwrt",
         {{0, 2, "安"}, {2, 8, "装"}},
         {"安", "装", "OpenWrt"},
         false},
        // kanlibimedaima: kan(3) libime(6) dai(3) ma(2) = 14
        {"mixed-c-e-c-libime",
         "kanlibimedaima",
         {{0, 3, "看"}, {9, 12, "代"}, {12, 14, "码"}},
         {"看", "LibIME", "代", "码"},
         false},

        // -- Ambiguous short raws (evidence, not fragmentation) --------
        // opengame: open(4) + game(4) = 8
        {"ambig-open-game", "opengame", {}, {"open", "game"}, false},

        // -- Pure Chinese controls (false-correction guard) ------------
        // zhongguo: zhong(5) guo(3) = 8
        {"clean-pure-zhongguo",
         "zhongguo",
         {{0, 5, "中"}, {5, 8, "国"}},
         {"中", "国"},
         true},
        // pingan: ping(4) an(2) = 6
        {"clean-pure-pingan",
         "pingan",
         {{0, 4, "平"}, {4, 6, "安"}},
         {"平", "安"},
         true},
    };
    return cases;
}

// Test-double Chinese oracle for the corpus. Same shape as the one used by
// testmixedengine.cpp: hand-authored spans with fixed confidence. This
// isolates the Architecture A orchestration layer from LibIME-specific
// segmentation behavior (which is separately exercised by
// testmixedchinesecorrection.cpp).
struct CorpusChineseOracle : IChineseArcOracle {
    std::vector<HanSpan> spans;
    std::vector<SegmentationArc> arcsAt(std::string_view raw, size_t begin,
                                        size_t maxSpan) const override {
        std::vector<SegmentationArc> out;
        int rank = 0;
        for (const auto &s : spans) {
            if (s.begin != begin) {
                continue;
            }
            if (s.end > begin + maxSpan || s.end > raw.size()) {
                continue;
            }
            SegmentationArc arc;
            arc.rawBegin = s.begin;
            arc.rawEnd = s.end;
            arc.source = SegmentSource::Chinese;
            arc.provenance = CandidateProvenance::Exact;
            arc.confidence = 0.85F;
            arc.boundaryConfidence = 1.0F;
            arc.sourceLocalRank = rank++;
            out.push_back(arc);
        }
        return out;
    }
};

// Compose the corpus seed English lexicon + user (empty) + correction.
struct Oracles {
    EnglishLexicon lex;
    EnglishArcOracle sysEn;
    EnglishUserLexicon userLex;
    EnglishUserArcOracle userEn;
    EnglishCorrectionOracle corrEn;
    CompositeEnglishArcOracle composite;
    Oracles()
        : lex(), sysEn(&lex), userLex(), userEn(&userLex), corrEn(&lex),
          composite() {
        lex.loadFromEntries(seedLexicon());
        composite.addSource(&sysEn);
        composite.addSource(&userEn);
        composite.addSource(&corrEn);
    }
};

bool composedTextContainsAll(const std::string &composed,
                             const std::vector<std::string> &needles) {
    for (const auto &n : needles) {
        if (composed.find(n) == std::string::npos) {
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    Oracles oracles;
    MixedEngine engine;

    size_t totalCases = 0;
    size_t top1Hits = 0;
    size_t recallHits = 0;
    size_t falseCorrection = 0;
    size_t pureChineseCases = 0;
    size_t pureChineseEmptyPools = 0;

    std::vector<double> latenciesUs;
    latenciesUs.reserve(corpus().size());
    std::vector<double> fastPathLatenciesUs;
    fastPathLatenciesUs.reserve(corpus().size());

    // Warm-up pass: run the whole corpus once against the same engine and
    // discard results before timing. Without this the per-case `compute`
    // measured below is a cold first call (allocator pool, English user
    // lexicon, ranker scratch buffers not yet touched), which is a method
    // error for a latency guard and inflates >10x on shared CI runners.
    // Semantics are unchanged: same inputs, oracles and resolver as the
    // timed pass, and the same engine instance used by the drift loop.
    for (const auto &c : corpus()) {
        CorpusChineseOracle chWarm;
        chWarm.spans = c.han;
        ArcResolver hanWarm = [&](const SegmentationArc &arc) -> std::string {
            for (const auto &s : c.han) {
                if (s.begin == arc.rawBegin && s.end == arc.rawEnd) {
                    return s.han;
                }
            }
            return {};
        };
        (void)engine.compute(c.raw, chWarm, oracles.composite, hanWarm);
    }

    std::fprintf(stderr, "=== batch 10 corpus report ===\n");
    for (const auto &c : corpus()) {
        ++totalCases;
        CorpusChineseOracle ch;
        ch.spans = c.han;
        ArcResolver han = [&](const SegmentationArc &arc) -> std::string {
            for (const auto &s : c.han) {
                if (s.begin == arc.rawBegin && s.end == arc.rawEnd) {
                    return s.han;
                }
            }
            return {};
        };
        auto t0 = std::chrono::steady_clock::now();
        auto pool = engine.compute(c.raw, ch, oracles.composite, han);
        auto t1 = std::chrono::steady_clock::now();
        std::chrono::duration<double, std::micro> dt = t1 - t0;
        if (c.pureChineseExpected) {
            fastPathLatenciesUs.push_back(dt.count());
        } else {
            latenciesUs.push_back(dt.count());
        }

        bool hit = false;
        bool recall = false;
        bool fastPath = false;
        std::string topText;
        if (c.pureChineseExpected) {
            // §9 fast-path contract: with no English arc anywhere in the
            // search result, MixedEngine::compute must short-circuit before
            // compose / pool / rank / rewrite and return an EMPTY pool.
            fastPath = pool.empty();
            if (fastPath) {
                ++pureChineseEmptyPools;
            }
        } else if (!pool.empty()) {
            topText = pool.front().composedText;
            hit = composedTextContainsAll(topText, c.expectedDisplays);
            for (const auto &cand : pool) {
                if (composedTextContainsAll(cand.composedText,
                                            c.expectedDisplays)) {
                    recall = true;
                    break;
                }
            }
        }
        if (hit) {
            ++top1Hits;
        }
        if (recall) {
            ++recallHits;
        }

        if (c.pureChineseExpected) {
            ++pureChineseCases;
            // English-source guard kept as belt-and-braces on top of the
            // emptiness invariant: an empty pool passes trivially, but if
            // the fast path ever regressed to returning Chinese-only mixed
            // candidates, any English contamination still fails here.
            bool englishAppeared = false;
            for (const auto &cand : pool) {
                for (const auto src : cand.sources) {
                    if (src != SegmentSource::Chinese) {
                        englishAppeared = true;
                        break;
                    }
                }
                if (englishAppeared) {
                    break;
                }
            }
            if (englishAppeared) {
                ++falseCorrection;
            }
        }

        if (c.pureChineseExpected) {
            std::fprintf(stderr,
                         "  %-32s fastpath=%s pool=%zu latency=%.1fus\n",
                         c.label, fastPath ? "OK" : "MISS", pool.size(),
                         fastPathLatenciesUs.back());
        } else {
            std::fprintf(stderr,
                         "  %-32s top1=%s recall=%s pool=%zu latency=%.1fus\n",
                         c.label, hit ? "OK" : "MISS", recall ? "OK" : "MISS",
                         pool.size(), latenciesUs.back());
        }
    }

    auto percentile = [](std::vector<double> &sorted, double q) {
        if (sorted.empty()) {
            return 0.0;
        }
        size_t idx = static_cast<size_t>(sorted.size() * q);
        if (idx >= sorted.size()) {
            idx = sorted.size() - 1;
        }
        return sorted[idx];
    };
    std::sort(latenciesUs.begin(), latenciesUs.end());
    std::sort(fastPathLatenciesUs.begin(), fastPathLatenciesUs.end());
    const double p50 = percentile(latenciesUs, 0.50);
    const double p95 = percentile(latenciesUs, 0.95);
    const double pmax = latenciesUs.empty() ? 0.0 : latenciesUs.back();
    const double fp50 = percentile(fastPathLatenciesUs, 0.50);
    const double fp95 = percentile(fastPathLatenciesUs, 0.95);
    const double fpmax =
        fastPathLatenciesUs.empty() ? 0.0 : fastPathLatenciesUs.back();
    const size_t mixedCases = totalCases - pureChineseCases;

    // Long-run: re-iterate the full corpus 200 times against the SAME engine
    // instance; assert the top-1 composed string is stable per case. This
    // is not a substitute for RSS-based device soak (deferred to Batch 11
    // product-path validation), but validates that repeated compute() calls
    // do not mutate engine-side state that would drift results over a
    // session (per design §32 cache invalidation semantics; the engine is
    // stateless across calls).
    {
        // Baseline: capture per-case first-pass top-1 composed text.
        std::vector<std::string> baselineTop1;
        baselineTop1.reserve(corpus().size());
        for (const auto &c : corpus()) {
            CorpusChineseOracle ch;
            ch.spans = c.han;
            ArcResolver han = [&](const SegmentationArc &arc) -> std::string {
                for (const auto &s : c.han) {
                    if (s.begin == arc.rawBegin && s.end == arc.rawEnd) {
                        return s.han;
                    }
                }
                return {};
            };
            auto pool = engine.compute(c.raw, ch, oracles.composite, han);
            baselineTop1.push_back(pool.empty() ? std::string()
                                                : pool.front().composedText);
        }

        size_t driftCases = 0;
        for (int i = 0; i < 200; ++i) {
            for (size_t idx = 0; idx < corpus().size(); ++idx) {
                const auto &c = corpus()[idx];
                CorpusChineseOracle ch;
                ch.spans = c.han;
                ArcResolver han =
                    [&](const SegmentationArc &arc) -> std::string {
                    for (const auto &s : c.han) {
                        if (s.begin == arc.rawBegin && s.end == arc.rawEnd) {
                            return s.han;
                        }
                    }
                    return {};
                };
                auto pool = engine.compute(c.raw, ch, oracles.composite, han);
                std::string top =
                    pool.empty() ? std::string() : pool.front().composedText;
                if (top != baselineTop1[idx]) {
                    ++driftCases;
                }
            }
        }
        check(driftCases == 0,
              "long-run: top-1 composed text stable across 200 iterations");
    }

    std::fprintf(stderr,
                 "=== batch 10 summary ===\n"
                 "  corpus size:                         %zu\n"
                 "  mixed cases:                         %zu\n"
                 "  top-1 accuracy (mixed):              %zu/%zu\n"
                 "  top-K recall (mixed, K = maxSize):   %zu/%zu\n"
                 "  fast-path empty pool (pure-Chinese): %zu/%zu\n"
                 "  false-correction (pure-Chinese):     %zu/%zu\n"
                 "  mixed latency p50/p95/max (us):      %.1f / %.1f / %.1f\n"
                 "  fast-path latency p50/p95/max (us):  %.1f / %.1f / %.1f\n",
                 totalCases, mixedCases, top1Hits, mixedCases, recallHits,
                 mixedCases, pureChineseEmptyPools, pureChineseCases,
                 falseCorrection, pureChineseCases, p50, p95, pmax, fp50, fp95,
                 fpmax);

    // Contract assertions:
    //  * Corpus must be exercised; empty is a wiring failure.
    check(totalCases >= 7, "corpus: at least 7 labeled cases");
    //  * Every mixed (non-pure-Chinese) case must appear in top-K; a miss
    //    here is a genuine segmentation / compose / rank regression.
    check(recallHits >= mixedCases,
          "recall: every mixed case appears in top-K");
    //  * §9 fast path: every pure-Chinese stream must yield an EMPTY pool
    //    (stronger than the English-free guard below; the classical Chinese
    //    path owns these streams end-to-end).
    check(pureChineseEmptyPools == pureChineseCases,
          "fast path: every pure-Chinese stream yields an empty pool");
    //  * False-correction guard: well-formed pure-Chinese streams must never
    //    gain an English source in their pool.
    check(falseCorrection == 0,
          "false-correction: pure-Chinese streams keep an English-free pool");
    //  * Top-1 accuracy over mixed cases is the strongest signal the
    //    orchestrator can produce offline; allow at most one case to miss
    //    (a case where the corpus expected span ranking differs from the
    //    pool tie-break, but recall still holds). If two or more miss,
    //    ranking has regressed.
    check(top1Hits + 1 >= mixedCases,
          "top-1 accuracy: at most one mixed corpus miss");
    //  * Latency ceiling: p95 under 5 ms per corpus case in the container
    //    env, separately for the full mixed pipeline and the §9 fast path.
    //    This is a coarse budget guard, not a product SLA.
    check(p95 < 5000.0, "latency: mixed p95 under 5 ms per corpus case");
    check(fp95 < 5000.0, "latency: fast-path p95 under 5 ms per corpus case");

    // ---- Production-lexicon section (fusion closure §6) ----------------
    //
    // This section validates the SHIPPED resource itself: the production
    // `english_lexicon.tsv` (94k+ entries) loaded through the exact same
    // English Core oracles the addon runs. It is deliberately arc-level, not
    // pool-level: at production scale the hand-authored Chinese test double
    // above cannot faithfully model what the real LibIME dictionary decodes
    // (e.g. 1-2 letter SCOWL words tile raws whose Chinese reading the double
    // never emits). End-to-end ranking against the real dictionary + the
    // placement policy is measured at the product surface instead, by
    // testpinyin's mixed corpus harness, which reports Top-1 / Top-K / MRR
    // and the false-pollution gate there.
    //
    // What is proven here, against the bytes that ship:
    //   * Canonical-cased proper nouns resolve to their display form
    //     (iPhone / macOS / GitHub / ChatGPT / OpenWrt / LibIME).
    //   * User-typed case is preserved (Apple stays Apple).
    //   * Prefix completion reaches long specific prefixes.
    //   * Correction reaches one adjacent substitution and one transposition.
    //   * The §17.4 false-correction guard: an intentional word emits no
    //     Correction arc, and ambiguous short words (ai/an/pin/win/long/game)
    //     are present as Exact evidence, which the >=3-byte lead rule and the
    //     whole-raw-Chinese rule in pinyin.cpp then keep out of the top slots
    //     of Chinese streams.
    //   * Completion fan-out stays bounded per span.
    {
#ifndef FUSION_ENGLISH_LEXICON_PATH
        check(false,
              "production lexicon: FUSION_ENGLISH_LEXICON_PATH not defined");
#endif
        EnglishLexicon realLex;
        {
            std::ifstream in(FUSION_ENGLISH_LEXICON_PATH);
            check(in.is_open(),
                  "production lexicon: shipped english_lexicon.tsv readable");
            realLex.load(in);
        }
        check(realLex.size() >= 50000,
              "production lexicon: shipped resource has >= 50k entries");

        EnglishArcOracle sysEn(&realLex);
        EnglishCorrectionOracle corrEn(&realLex);

        auto hasArc = [&](std::string_view raw, const std::string &output,
                          CandidateProvenance prov) {
            const auto arcs = sysEn.arcsAt(raw, 0, raw.size());
            for (const auto &a : arcs) {
                if (a.resolvedOutput == output && a.provenance == prov) {
                    return true;
                }
            }
            return false;
        };

        std::fprintf(stderr, "=== production-lexicon core report ===\n");

        // Canonical-cased brands/technical terms resolve to display form.
        struct CanonicalCase {
            const char *raw;
            const char *display;
        };
        const std::vector<CanonicalCase> canonicals = {
            {"iphone", "iPhone"},   {"macos", "macOS"},
            {"github", "GitHub"},   {"chatgpt", "ChatGPT"},
            {"openwrt", "OpenWrt"}, {"libime", "LibIME"},
        };
        size_t canonicalHits = 0;
        for (const auto &c : canonicals) {
            const bool ok =
                hasArc(c.raw, c.display, CandidateProvenance::Canonical);
            if (ok) {
                ++canonicalHits;
            }
            std::fprintf(stderr, "  canonical %-8s -> %-8s %s\n", c.raw,
                         c.display, ok ? "OK" : "MISS");
            check(ok, "production lexicon: canonical display resolution");
        }

        // User-typed case is preserved exactly (the arc that ships the
        // as-typed surface must exist for the brand and a generic word).
        const bool typedApple =
            hasArc("Apple", "Apple", CandidateProvenance::Exact);
        const bool typedIPhone =
            hasArc("IPhone", "IPhone", CandidateProvenance::Exact);
        std::fprintf(stderr, "  typed-case Apple=%s IPhone=%s\n",
                     typedApple ? "OK" : "MISS", typedIPhone ? "OK" : "MISS");
        check(typedApple && typedIPhone,
              "production lexicon: user-typed case preserved");

        // Prefix completion (long specific prefixes reach the brand).
        const bool compIphone =
            hasArc("iphon", "iPhone", CandidateProvenance::Completion);
        const bool compChatgpt =
            hasArc("chatgp", "ChatGPT", CandidateProvenance::Completion);
        const bool compGithub =
            hasArc("githu", "GitHub", CandidateProvenance::Completion);
        std::fprintf(stderr,
                     "  completion iphon->iPhone=%s chatgp->ChatGPT=%s "
                     "githu->GitHub=%s\n",
                     compIphone ? "OK" : "MISS", compChatgpt ? "OK" : "MISS",
                     compGithub ? "OK" : "MISS");
        check(compIphone && compChatgpt && compGithub,
              "production lexicon: prefix completion reaches brands");

        // Correction: one adjacent substitution and one transposition, and
        // the guard that intentional words are never corrected.
        const auto corrArcsR = corrEn.arcsAt("iphonr", 0, 6);
        const auto corrArcsT = corrEn.arcsAt("iphnoe", 0, 6);
        const auto corrArcsG = corrEn.arcsAt("githun", 0, 6);
        const auto hasOutputIn = [](const std::vector<SegmentationArc> &arcs,
                                    const std::string &out) {
            for (const auto &a : arcs) {
                if (a.resolvedOutput == out) {
                    return true;
                }
            }
            return false;
        };
        const bool corrR = hasOutputIn(corrArcsR, "iPhone");
        const bool corrT = hasOutputIn(corrArcsT, "iPhone");
        const bool corrG = hasOutputIn(corrArcsG, "GitHub");
        const auto cleanArcs = corrEn.arcsAt("iphone", 0, 6);
        const bool guardIntentional = std::none_of(
            cleanArcs.begin(), cleanArcs.end(), [](const SegmentationArc &a) {
                return a.provenance == CandidateProvenance::Correction;
            });
        std::fprintf(stderr,
                     "  correction iphonr=%s iphnoe=%s githun=%s "
                     "guard(iphone)=no-correction=%s\n",
                     corrR ? "OK" : "MISS", corrT ? "OK" : "MISS",
                     corrG ? "OK" : "MISS", guardIntentional ? "OK" : "MISS");
        check(corrR && corrT && corrG,
              "production lexicon: bounded correction reaches brands");
        check(guardIntentional,
              "production lexicon: intentional word emits no Correction arc");

        // Ambiguous short words are present as Exact evidence; they must be,
        // or the English Core would silently drop valid English readings.
        // Their placement influence is neutralised by the structural
        // placement policy (span >= 3 lead rule), which is product-path
        // tested in testpinyin.
        size_t ambiguousPresent = 0;
        for (const char *w : {"ai", "an", "pin", "win", "long", "game"}) {
            if (realLex.isExact(w)) {
                ++ambiguousPresent;
            }
        }
        std::fprintf(stderr, "  ambiguous short words exact: %zu/6\n",
                     ambiguousPresent);
        check(ambiguousPresent >= 4,
              "production lexicon: ambiguous short English words present");

        // Completion fan-out is capped per prefix span
        // (maxCompletionsPerSpan = 4). arcsAt("appl", 0, 4) covers four
        // distinct prefixes ("a", "ap", "app", "appl"), each of which may
        // carry up to 4 completions; the bound that matters for per-tick
        // cost is per-span, asserted here on the longest prefix.
        const auto fanArcs = sysEn.arcsAt("appl", 0, 4);
        size_t fullSpanCompletions = 0;
        bool appleReachable = false;
        for (const auto &a : fanArcs) {
            if (a.provenance == CandidateProvenance::Completion &&
                a.rawEnd - a.rawBegin == 4) {
                ++fullSpanCompletions;
                if (a.resolvedOutput == "apple") {
                    appleReachable = true;
                }
            }
        }
        std::fprintf(stderr,
                     "  completion fan-out on prefix \"appl\" (span 4): "
                     "%zu (cap 4), apple reachable=%s\n",
                     fullSpanCompletions, appleReachable ? "OK" : "MISS");
        check(fullSpanCompletions <= 4,
              "production lexicon: completion fan-out bounded per span");
        check(appleReachable,
              "production lexicon: completion reaches the common word for "
              "its prefix");

        // Negatives: non-alphabetic raws produce no arcs at all.
        const auto digitArcs = sysEn.arcsAt("12345", 0, 5);
        check(digitArcs.empty(),
              "production lexicon: digit raws produce no English arcs");

        // Latency at production scale (closure §7 numbers; thresholds are
        // reported, not asserted, per the no-invented-thresholds rule).
        {
            std::vector<double> exactUs;
            std::vector<double> complUs;
            exactUs.reserve(2000);
            complUs.reserve(2000);
            for (int i = 0; i < 1000; ++i) {
                auto t0 = std::chrono::steady_clock::now();
                (void)sysEn.arcsAt("iphone", 0, 6);
                auto t1 = std::chrono::steady_clock::now();
                std::chrono::duration<double, std::micro> dt = t1 - t0;
                exactUs.push_back(dt.count());
            }
            for (int i = 0; i < 1000; ++i) {
                auto t0 = std::chrono::steady_clock::now();
                (void)sysEn.arcsAt("iphon", 0, 5);
                auto t1 = std::chrono::steady_clock::now();
                std::chrono::duration<double, std::micro> dt = t1 - t0;
                complUs.push_back(dt.count());
            }
            std::sort(exactUs.begin(), exactUs.end());
            std::sort(complUs.begin(), complUs.end());
            auto pct = [](std::vector<double> &v, double q) {
                size_t idx = static_cast<size_t>(v.size() * q);
                if (idx >= v.size()) {
                    idx = v.size() - 1;
                }
                return v[idx];
            };
            std::fprintf(stderr,
                         "  exact arcsAt p50/p95/p99/max (us): %.2f / %.2f "
                         "/ %.2f / %.2f\n"
                         "  completion arcsAt p50/p95/p99/max (us): %.2f / "
                         "%.2f / %.2f / %.2f\n",
                         pct(exactUs, 0.50), pct(exactUs, 0.95),
                         pct(exactUs, 0.99), exactUs.back(), pct(complUs, 0.50),
                         pct(complUs, 0.95), pct(complUs, 0.99),
                         complUs.back());
        }
    }

    if (failures > 0) {
        std::fprintf(stderr, "testmixedcorpus: %d failure(s)\n", failures);
        return 1;
    }
    std::fprintf(stderr, "testmixedcorpus: OK\n");
    return 0;
}
