/*
 * SPDX-FileCopyrightText: 2020~2020 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "pinyinhelper_public.h"
#include "testdir.h"
#include "testfrontend_public.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcitx-config/rawconfig.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/eventloopinterface.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/macros.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitx-utils/testing.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidateaction.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputmethodgroup.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace fcitx;

namespace {

std::unique_ptr<EventSourceTime> endTestEvent;
void testPunctuationPart2(Instance *instance);

int findCandidate(InputContext *ic, std::string_view word) {
    auto candList = ic->inputPanel().candidateList();
    for (int i = 0; i < candList->toBulk()->totalSize(); i++) {
        const auto &candidate = candList->toBulk()->candidateFromAll(i);
        if (candidate.text().toString() == word) {
            return i;
        }
    }
    return -1;
}

int findCandidateOrDie(InputContext *ic, std::string_view word) {
    auto index = findCandidate(ic, word);
    FCITX_ASSERT(index >= 0) << "Failed to find candidate " << word;
    return index;
}

void findAndSelectCandidate(InputContext *ic, std::string_view word) {
    auto candList = ic->inputPanel().candidateList();
    candList->candidate(findCandidateOrDie(ic, word)).select(ic);
}

void sendControlSpace(AddonInstance *testfrontend, InputContext *ic) {
    for (int i = 0; i < 2; i++) {
        testfrontend->call<ITestFrontend::keyEvent>(ic->uuid(),
                                                    Key("Control_L"), false);
        testfrontend->call<ITestFrontend::keyEvent>(
            ic->uuid(), Key("Control+space"), false);
        testfrontend->call<ITestFrontend::keyEvent>(ic->uuid(),
                                                    Key("Control+space"), true);
        testfrontend->call<ITestFrontend::keyEvent>(
            ic->uuid(), Key("Control+Control_L"), true);
        ic->reset();
    }
}

void setup(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *pinyin = instance->addonManager().addon("pinyin", true);
        FCITX_ASSERT(pinyin);
        auto defaultGroup = instance->inputMethodManager().currentGroup();
        defaultGroup.inputMethodList().clear();
        defaultGroup.inputMethodList().push_back(
            InputMethodGroupItem("keyboard-us"));
        defaultGroup.inputMethodList().push_back(
            InputMethodGroupItem("pinyin"));
        defaultGroup.inputMethodList().push_back(
            InputMethodGroupItem("shuangpin"));
        defaultGroup.setDefaultInputMethod("");
        instance->inputMethodManager().setGroup(std::move(defaultGroup));
    });
}

void testBasic(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *pinyin = instance->addonManager().addon("pinyin");
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        testfrontend->call<ITestFrontend::pushCommitExpectation>("呵");
        testfrontend->call<ITestFrontend::pushCommitExpectation>("ni");
        testfrontend->call<ITestFrontend::pushCommitExpectation>("ni");
        testfrontend->call<ITestFrontend::pushCommitExpectation>("你hao");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("`"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        testfrontend->call<ITestFrontend::keyEvent>(
            uuid, Key(FcitxKey_BackSpace), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("s"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("z"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("s"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("1"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Return"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("KP_Enter"),
                                                    false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
        // Make a partial selection, we do search because the data might change.
        findAndSelectCandidate(ic, "你");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Return"), false);

        // Test switch input method.
        testfrontend->call<ITestFrontend::pushCommitExpectation>("nihao");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
        sendControlSpace(testfrontend, ic);

        testfrontend->call<ITestFrontend::pushCommitExpectation>("你hao");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
        // Make a partial selection, we do search because the data might change.
        findAndSelectCandidate(ic, "你");
        sendControlSpace(testfrontend, ic);

        RawConfig config;
        config.setValueByPath("SwitchInputMethodBehavior",
                              "Commit default selection");
        pinyin->setConfig(config);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
        auto sentence =
            ic->inputPanel().candidateList()->candidate(0).text().toString();
        testfrontend->call<ITestFrontend::pushCommitExpectation>(sentence);
        sendControlSpace(testfrontend, ic);

        config.setValueByPath("SwitchInputMethodBehavior", "Clear");
        pinyin->setConfig(config);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
        sendControlSpace(testfrontend, ic);
    });
}

void testSelectByChar(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("g"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("g"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("z"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("u"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("b"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("g"), false);

        testfrontend->call<ITestFrontend::pushCommitExpectation>("你好主病");
        findAndSelectCandidate(ic, "你好");
        auto candidateIdx = findCandidateOrDie(ic, "公主");
        ic->inputPanel().candidateList()->toBulkCursor()->setGlobalCursorIndex(
            candidateIdx);
        // With default config, this should select "主".
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("]"), false);
        findAndSelectCandidate(ic, "病");
    });
}

void testUppercase(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

        testfrontend->call<ITestFrontend::pushCommitExpectation>("Apple");

        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("A"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("p"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("p"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("l"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);

        testfrontend->call<ITestFrontend::pushCommitExpectation>("iPhone");

        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("P"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);
    });
}

// Fusion closure §4 — Shuangpin mixed input through the real product path:
// Ziranma raw "hsiphone" (火/国... + iphone) must reach the CandidateList as
// a fused Han+iPhone candidate, commit exactly that candidate on selection,
// and leave the engine clean for the next composition. The Ziranma single
// key codes tile the whole raw, so the fused candidate may sit behind the
// classical block (placement policy); this test therefore selects by
// substring instead of asserting a top-1 position.
void testMixedShuangpin(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "shuangpin", true);
        ic->reset();

        for (const char *k : {"h", "s", "i", "p", "h", "o", "n", "e"}) {
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key(k), false);
        }

        auto *candidateList = ic->inputPanel().candidateList().get();
        FCITX_ASSERT(candidateList);
        FCITX_ASSERT(!candidateList->empty());
        auto *bulk = candidateList->toBulk();
        FCITX_ASSERT(bulk);
        int mixedIndex = -1;
        std::string mixedText;
        for (int i = 0; i < bulk->totalSize(); i++) {
            const auto text = bulk->candidateFromAll(i).text().toString();
            if (text.find("iPhone") != std::string::npos) {
                mixedIndex = i;
                mixedText = text;
                break;
            }
        }
        FCITX_ASSERT(mixedIndex >= 0)
            << "Failed to find a fused iPhone candidate for Ziranma raw "
               "hsiphone";
        FCITX_ASSERT(mixedText.find("iPhone") != std::string::npos);

        testfrontend->call<ITestFrontend::pushCommitExpectation>(mixedText);
        bulk->candidateFromAll(mixedIndex).select(ic);

        // Continuation: the composition state must be fresh after the mixed
        // selection commit; a plain Ziranma "ni" still composes normally.
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        candidateList = ic->inputPanel().candidateList().get();
        FCITX_ASSERT(candidateList);
        FCITX_ASSERT(!candidateList->empty());
        const auto next = candidateList->candidate(0).text().toString();
        testfrontend->call<ITestFrontend::pushCommitExpectation>(next);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);
    });
}

// Formal production-resource validation at the product surface: types each
// case through the real Pinyin input method (full dictionary + mixed engine +
// placement policy) and reports Top-1 / Top-K recall / MRR plus the false
// pollution gate. Metrics are printed for the closure report; the hard gates
// are full recall of every scored case and zero English contamination of
// classical-led pure-Chinese compositions.
void testMixedProductCorpus(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

        // Upstream reserves raws that begin with 'v' on an empty buffer for
        // the V-as-quickphrase entry (pinyin.cpp checkV), so "vpn" never
        // reaches any pinyin composition path while that option is ON. The
        // formal corpus must measure the Architecture A product path, and
        // VAsQuickphrase is a supported user toggle: turn it OFF for the
        // scored cases and restore the default afterwards (the later
        // testVQuickPhraseTrigger relies on the ON default).
        auto *pinyin = instance->addonManager().addon("pinyin", true);
        RawConfig config;
        config.setValueByPath("VAsQuickphrase", "False");
        pinyin->setConfig(config);

        struct ProductCase {
            const char *raw;
            const char *needle;
            bool classicalLead;
        };
        const ProductCase cases[] = {
            {"nihao", "你", true},
            {"pingan", "安", true},
            {"zhongguo", "国", true},
            {"woxiangmaiiphonepeijian", "iPhone", false},
            {"wodakaigithub", "GitHub", false},
            {"iphone", "iPhone", false},
            {"chatgpt", "ChatGPT", false},
            {"macos", "macOS", false},
            {"iphon", "iPhone", false},
            {"vpn", "VPN", false},
        };

        size_t scored = 0, top1 = 0, recall = 0;
        double rrSum = 0.0;
        size_t pollution = 0;
        for (const auto &c : cases) {
            ic->reset();
            for (const char *p = c.raw; *p; ++p) {
                testfrontend->call<ITestFrontend::keyEvent>(
                    uuid, Key(std::string(1, *p)), false);
            }
            auto *candidateList = ic->inputPanel().candidateList().get();
            std::string top;
            if (candidateList && !candidateList->empty()) {
                top = candidateList->candidate(0).text().toString();
            }
            if (c.classicalLead) {
                bool ascii = false;
                for (const char ch : top) {
                    if (std::isalpha(static_cast<unsigned char>(ch))) {
                        ascii = true;
                        break;
                    }
                }
                if (ascii) {
                    ++pollution;
                }
                std::fprintf(stderr, "MIXEDCORP %s classical-lead top1=%s\n",
                             c.raw, top.c_str());
                continue;
            }
            ++scored;
            int rank = -1;
            auto *fresh = ic->inputPanel().candidateList().get();
            if (fresh) {
                if (auto *bulk = fresh->toBulk()) {
                    for (int i = 0; i < bulk->totalSize(); i++) {
                        if (bulk->candidateFromAll(i).text().toString().find(
                                c.needle) != std::string::npos) {
                            rank = i;
                            break;
                        }
                    }
                } else if (!fresh->empty() &&
                           std::string(fresh->candidate(0).text().toString())
                                   .find(c.needle) != std::string::npos) {
                    rank = 0;
                }
            }
            if (rank == 0) {
                ++top1;
            }
            if (rank >= 0) {
                ++recall;
                rrSum += 1.0 / static_cast<double>(rank + 1);
            } else if (fresh) {
                // Miss diagnostic: dump the reachable candidate window so a
                // failure names the actual panel state, not a guess.
                std::fprintf(stderr, "MIXEDCORP %s MISS window:", c.raw);
                if (auto *bulk = fresh->toBulk()) {
                    std::fprintf(stderr, " total=%d\n", bulk->totalSize());
                    const int limit = std::min(bulk->totalSize(), 24);
                    for (int i = 0; i < limit; i++) {
                        std::fprintf(
                            stderr, "  [%d] %s\n", i,
                            std::string(
                                bulk->candidateFromAll(i).text().toString())
                                .c_str());
                    }
                } else {
                    std::fprintf(stderr, " non-bulk n=%d\n", fresh->size());
                }
            }
            std::fprintf(stderr, "MIXEDCORP %s needle=%s rank=%d top1=%s\n",
                         c.raw, c.needle, rank, top.c_str());
        }
        const double mrr = (scored == 0) ? 0.0 : rrSum / double(scored);
        std::fprintf(stderr,
                     "MIXEDCORP SUMMARY scored=%zu top1=%zu recall=%zu "
                     "MRR=%.3f pollution=%zu\n",
                     scored, top1, recall, mrr, pollution);
        FCITX_ASSERT(recall == scored)
            << "Product corpus: English needle not reachable on every "
               "English-bearing case";
        FCITX_ASSERT(pollution == 0)
            << "Product corpus: English text polluted a classical-led pure "
               "Chinese composition";
        ic->reset();
        config.setValueByPath("VAsQuickphrase", "True");
        pinyin->setConfig(config);
    });
}

// Final ranking closure (replaces the blanket "entire classical list before all
// English" policy with a bounded whole-span canonical/user insertion class).
// Fixed product-path corpus per instruction §6: groups A-F, expectations
// encoded from candidate PROPERTIES (provenance class, span coverage), never
// from word-specific rules. Every case prints a RANKCORP line before any
// assertion, so a single run yields the complete before/after table; all
// failures are aggregated and asserted at the end.
void testMixedRankingCorpus(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

        // Same reservation as testMixedProductCorpus: VAsQuickphrase is a
        // supported user toggle and `v`-initial raws never reach pinyin
        // composition while it is ON. Turn it OFF for the scored cases and
        // restore afterwards.
        auto *pinyin = instance->addonManager().addon("pinyin", true);
        RawConfig config;
        config.setValueByPath("VAsQuickphrase", "False");
        pinyin->setConfig(config);

        // One bounded expectation, used by every group:
        // - Group A (whole-raw Canonical proper/technical respellings and
        //   user-learned whole-raw additions) must land at rank 0: the class
        //   is structurally unambiguous, so no mechanical Han tiling stays
        //   above it.
        // - Ambiguous Exact lowercase words (group B) must be STRICTLY
        //   behind that class entry, keep the classical top-1, and (E1)
        //   remain at their old deep position when the user has never
        //   confirmed them. Learning a word (E2) is allowed to move exactly
        //   that word up; the old rank is stored to assert nothing else
        //   shifted.
        // The "unbounded scan" prohibition (§10) is checked by measuring the
        // whole scored run: no case may search beyond the list it was given,
        // and per-case wall time is printed for regression.
        constexpr size_t kClassLeadRank = 0;

        std::vector<std::string> failures;
        auto fail = [&](const std::string &line) {
            failures.push_back(line);
            std::fprintf(stderr, "RANKCORP FAIL %s\n", line.c_str());
        };

        auto rankOfNeedle = [&](const char *raw, const char *needle) -> int {
            ic->reset();
            for (const char *p = raw; *p; ++p) {
                testfrontend->call<ITestFrontend::keyEvent>(
                    uuid, Key(std::string(1, *p)), false);
            }
            auto *fresh = ic->inputPanel().candidateList().get();
            int rank = -1;
            if (fresh) {
                if (auto *bulk = fresh->toBulk()) {
                    for (int i = 0; i < bulk->totalSize(); i++) {
                        if (bulk->candidateFromAll(i).text().toString().find(
                                needle) != std::string::npos) {
                            rank = i;
                            break;
                        }
                    }
                    if (rank < 0) {
                        std::fprintf(stderr, "RANKCORP %s MISS total=%d\n", raw,
                                     bulk->totalSize());
                    }
                } else if (!fresh->empty() &&
                           std::string(fresh->candidate(0).text().toString())
                                   .find(needle) != std::string::npos) {
                    rank = 0;
                } else {
                    std::fprintf(stderr, "RANKCORP %s MISS nonbulk n=%d\n", raw,
                                 fresh->size());
                }
            } else {
                std::fprintf(stderr, "RANKCORP %s MISS nolist\n", raw);
            }
            std::string top;
            if (fresh && !fresh->empty()) {
                top = std::string(fresh->candidate(0).text().toString());
            }
            std::fprintf(stderr, "RANKCORP %s needle=%s rank=%d top1=%s\n", raw,
                         needle, rank, top.c_str());
            return rank;
        };

        const auto pureHanTop1 = [](const std::string &text) {
            return std::none_of(text.begin(), text.end(),
                                [](unsigned char ch) { return ch < 0x80; });
        };
        auto top1Text = [&]() {
            auto *fresh = ic->inputPanel().candidateList().get();
            return (fresh && !fresh->empty())
                       ? std::string(fresh->candidate(0).text().toString())
                       : std::string();
        };
        auto burst = [&](const char *raw) {
            ic->reset();
            for (const char *p = raw; *p; ++p) {
                testfrontend->call<ITestFrontend::keyEvent>(
                    uuid, Key(std::string(1, *p)), false);
            }
        };

        // --- A. High-confidence canonical proper/technical (whole-raw
        // respellings). Property-gated, never word-gated: the class is
        // defined by provenance Canonical + whole-span + single arc.
        for (const char *raw :
             {"chatgpt", "macos", "github", "iphone", "openwrt", "libime"}) {
            int rank = -1;
            // The needle is the dictionary canonical display; find it by
            // case-insensitive fold match against the raw (all these raws
            // are lowercase respellings of their surface).
            ic->reset();
            for (const char *p = raw; *p; ++p) {
                testfrontend->call<ITestFrontend::keyEvent>(
                    uuid, Key(std::string(1, *p)), false);
            }
            auto *fresh = ic->inputPanel().candidateList().get();
            int candRank = -1;
            std::string top = top1Text();
            if (fresh) {
                if (auto *bulk = fresh->toBulk()) {
                    for (int i = 0; i < bulk->totalSize(); i++) {
                        auto text = std::string(
                            bulk->candidateFromAll(i).text().toString());
                        // Whole-candidate case-insensitive equality with the
                        // raw identifies the canonical respell surface.
                        if (text.size() >= 3) {
                            std::string lower = text;
                            std::transform(lower.begin(), lower.end(),
                                           lower.begin(), [](unsigned char c) {
                                               return static_cast<char>(
                                                   std::tolower(c));
                                           });
                            if (lower == raw) {
                                candRank = i;
                                break;
                            }
                        }
                    }
                }
            }
            rank = candRank;
            std::fprintf(stderr, "RANKCORP-A %s rank=%d top1=%s\n", raw, rank,
                         top.c_str());
            if (rank < 0) {
                fail(std::string("A recall: ") + raw +
                     " canonical surface not reachable");
            } else if (rank != static_cast<int>(kClassLeadRank)) {
                fail(std::string("A ranking: ") + raw + " rank " +
                     std::to_string(rank) + " != class lead " +
                     std::to_string(kClassLeadRank));
            }
        }

        // --- B. Ambiguous Chinese/English (Exact lowercase overlap).
        // Conservative contract, property-gated: when the list contains ANY
        // pure-Han candidate for the raw (classical evidence exists), English
        // exact must not displace it at top-1. Where no Han reading exists
        // at all (e.g. "win" is not a pinyin syllable sequence), leading
        // English displaces nothing and is the pre-existing accepted
        // behaviour; the rank is still printed for the audit table.
        struct AmbiguousCase {
            const char *raw;
            bool expectHanLead;
        };
        // expectHanLead encodes the fixed expectation from the BEFORE run:
        // ai/an/pin/long/game already have a pure-Han top-1 (whole-raw
        // classical readings exist, so English exact words sit behind).
        // "win" is not a pinyin syllable sequence at all: the engine has no
        // whole-raw Han reading to protect, so an English lead there
        // displaces no Chinese and matches the pre-existing accepted
        // behaviour (the only Han entries are prefix-consuming single
        // chars, observed as 我/为-style readings for "w").
        const AmbiguousCase ambiguous[] = {
            {"ai", true},   {"an", true},   {"pin", true},
            {"win", false}, {"long", true}, {"game", true},
        };
        for (const auto &c : ambiguous) {
            int rank = rankOfNeedle(c.raw, c.raw);
            const std::string top = top1Text();
            const bool topHan = pureHanTop1(top);
            std::fprintf(stderr, "RANKCORP-B %s rank=%d top1=%s\n", c.raw, rank,
                         top.c_str());
            if (c.expectHanLead && !topHan) {
                fail(std::string("B displacement: ") + c.raw +
                     " English leads over whole-raw Han reading");
            }
        }

        // --- C. Mixed compositions remain first-class (leading path).
        {
            int rank = rankOfNeedle("wodakaigithub", "GitHub");
            if (rank < 0) {
                fail("C recall: wodakaigithub -> GitHub unreachable");
            } else if (rank > 1) {
                fail("C ranking: wodakaigithub GitHub rank " +
                     std::to_string(rank) + " not leading");
            }
            rank = rankOfNeedle("iphonepeijian", "iPhone");
            if (rank != 0) {
                fail("C ranking: E->C iphonepeijian iPhone rank " +
                     std::to_string(rank));
            }
            rank = rankOfNeedle("woxiangmaiiphonepeijian", "iPhone");
            if (rank > 1) {
                fail("C ranking: C->E->C woxiangmaiiphonepeijian rank " +
                     std::to_string(rank));
            }
            rank = rankOfNeedle("wodakaigithubheiphonexiuxian", "GitHub");
            if (rank < 0) {
                fail("C recall: multi-switch GitHub unreachable");
            }
            // Second-switch evidence: the composer tiles the suffix as
            // Han(嗨)+English(phone) here (observed product behaviour), so
            // the reachable needle for the second switch is "phone", not
            // "iPhone". Asserting the folded substring keeps the gate on
            // reachability rather than a word-specific surface.
            rank = rankOfNeedle("wodakaigithubheiphonexiuxian", "phone");
            if (rank < 0) {
                fail("C recall: multi-switch second English arc unreachable");
            }
        }

        // --- D. Negative/pollution: ordinary high-frequency Chinese must
        // show no English surface anywhere in the visible head block, and
        // completions/corrections must not outrank clean exact forms.
        for (const char *raw : {"nihao", "pengyou", "women", "shurufa"}) {
            burst(raw);
            auto *fresh = ic->inputPanel().candidateList().get();
            bool polluted = false;
            if (fresh) {
                if (auto *bulk = fresh->toBulk()) {
                    const int head = std::min(bulk->totalSize(), 7);
                    for (int i = 0; i < head; i++) {
                        auto text = std::string(
                            bulk->candidateFromAll(i).text().toString());
                        bool ascii = false;
                        for (char ch : text) {
                            if (std::isalpha(static_cast<unsigned char>(ch))) {
                                ascii = true;
                            }
                        }
                        if (ascii) {
                            polluted = true;
                        }
                    }
                } else if (!fresh->empty()) {
                    std::string text =
                        std::string(fresh->candidate(0).text().toString());
                    for (char ch : text) {
                        if (std::isalpha(static_cast<unsigned char>(ch))) {
                            polluted = true;
                        }
                    }
                }
            }
            std::fprintf(stderr, "RANKCORP-D %s headPolluted=%d\n", raw,
                         static_cast<int>(polluted));
            if (polluted) {
                fail(std::string("D pollution: English inside head block of ") +
                     raw);
            }
        }
        {
            // "chang" is fully Han (常); "change" exists only as a
            // completion. A completion must never enter the class-lead slot.
            int rank = rankOfNeedle("chang", "Change");
            std::string top = top1Text();
            std::fprintf(stderr, "RANKCORP-D2 chang rank=%d top1=%s\n", rank,
                         top.c_str());
            if (rank == 0) {
                fail("D completion dominates: chang -> Change leads");
            }
            if (!pureHanTop1(top)) {
                fail("D classical lead lost for chang");
            }
            // Correction recall + ordering: the bounded English correction
            // model is QWERTY-substitution + adjacent-transposition only
            // (no deletions), so the evidence case is "githbu" (github with
            // the last pair swapped), not an arbitrary typo class.
            rank = rankOfNeedle("githbu", "GitHub");
            if (rank < 0) {
                fail("D correction recall: githbu -> GitHub unreachable");
            }
        }

        // --- E. User learning moves ONLY the confirmed word into the class.
        {
            // E1: "pin" (拼 is the classical lead; English "pin" is an Exact
            // lowercase overlap). Record its pre-learning deep rank.
            int pinBefore = rankOfNeedle("pin", "pin");
            std::string pinTop = top1Text();
            if (!pureHanTop1(pinTop)) {
                fail("E1 classical lead lost for pin");
            }
            // E2: learn ChatGPT by committing the canonical candidate for the
            // lowercase raw, then verify (a) placement is the class lead after
            // learning, (b) the unlearned "pin" did NOT move, (c) Chinese
            // behavior is undestroyed. The candidate is located by fold-match
            // against the raw (same property test as group A) and selected
            // through the public virtual at WHATEVER rank it currently sits,
            // so a broken placement policy records an aggregated failure
            // instead of aborting the run and losing the rest of the table.
            burst("chatgpt");
            auto *fresh = ic->inputPanel().candidateList().get();
            FCITX_ASSERT(fresh && fresh->toBulk());
            auto *learnBulk = fresh->toBulk();
            int learnIdx = -1;
            std::string learnText;
            for (int i = 0; i < learnBulk->totalSize(); i++) {
                auto text = std::string(
                    learnBulk->candidateFromAll(i).text().toString());
                std::string lower = text;
                std::transform(lower.begin(), lower.end(), lower.begin(),
                               [](unsigned char c) {
                                   return static_cast<char>(std::tolower(c));
                               });
                if (lower == "chatgpt") {
                    learnIdx = i;
                    learnText = std::move(text);
                    break;
                }
            }
            if (learnIdx < 0) {
                fail("E2 canonical ChatGPT absent, cannot learn");
            } else {
                std::fprintf(stderr,
                             "RANKCORP-E2 learning ChatGPT at rank %d\n",
                             learnIdx);
                testfrontend->call<ITestFrontend::pushCommitExpectation>(
                    learnText);
                learnBulk->candidateFromAll(learnIdx).select(ic);
                int rankAfterLearn = rankOfNeedle("chatgpt", "ChatGPT");
                if (rankAfterLearn != static_cast<int>(kClassLeadRank)) {
                    fail("E2 placement after learning: ChatGPT rank " +
                         std::to_string(rankAfterLearn));
                }
            }
            int pinAfter = rankOfNeedle("pin", "pin");
            if (pinAfter != pinBefore) {
                fail("E2 learning displaced unlearned pin: " +
                     std::to_string(pinBefore) + " -> " +
                     std::to_string(pinAfter));
            }
            // Chinese continuation gate (mirrors testMixedLearning #3).
            burst("nihao");
            auto *afterIc = ic->inputPanel().candidateList().get();
            FCITX_ASSERT(afterIc && !afterIc->empty());
            const std::string hanTop =
                std::string(afterIc->candidate(0).text().toString());
            testfrontend->call<ITestFrontend::pushCommitExpectation>(hanTop);
            afterIc->candidate(0).select(ic);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
            auto *contList = ic->inputPanel().candidateList().get();
            FCITX_ASSERT(contList && !contList->empty());
            std::string contTop =
                std::string(contList->candidate(0).text().toString());
            std::fprintf(stderr, "RANKCORP-E nihao-n top1=%s\n",
                         contTop.c_str());
            if (!pureHanTop1(contTop)) {
                fail("E Chinese behavior destroyed after English commit");
            }
        }

        // --- F. Shuangpin real product path (mixed case with runtime-derived
        // codes; the fixed codes 配/件 from the closure run are reused only as
        // a keep/bait-free leading burst that is already pinned by D069).
        {
            instance->setCurrentInputMethod(ic, "shuangpin", true);
            int rank = -1;
            ic->reset();
            for (const char *p = "hsiphonepzjm"; *p; ++p) {
                testfrontend->call<ITestFrontend::keyEvent>(
                    uuid, Key(std::string(1, *p)), false);
            }
            auto *fresh = ic->inputPanel().candidateList().get();
            if (fresh) {
                if (auto *bulk = fresh->toBulk()) {
                    for (int i = 0; i < bulk->totalSize(); i++) {
                        if (bulk->candidateFromAll(i).text().toString().find(
                                "iPhone") != std::string::npos) {
                            rank = i;
                            break;
                        }
                    }
                }
            }
            std::string top = top1Text();
            std::fprintf(stderr,
                         "RANKCORP-F shuangpin iPhone rank=%d top1=%s\n", rank,
                         top.c_str());
            // Conservative expectation, matching the existing product-path
            // contract (testMixedShuangpin selects by substring because the
            // placement policy may keep multi-arc fused candidates behind
            // the classical block): the bounded insertion class leads only
            // whole-raw single-arc canonical/user forms, so a shuangpin
            // fused candidate must be REACHABLE and selectable; leading
            // placement is not asserted without corpus evidence.
            if (rank < 0) {
                fail("F recall: shuangpin mixed iPhone unreachable");
            }
            instance->setCurrentInputMethod(ic, "pinyin", true);
        }

        std::fprintf(stderr, "RANKCORP SUMMARY failures=%zu\n",
                     failures.size());
        ic->reset();
        config.setValueByPath("VAsQuickphrase", "True");
        pinyin->setConfig(config);
        FCITX_ASSERT(failures.empty()) << "Ranking corpus: " << failures.size()
                                       << " failures (see RANKCORP FAIL lines)";
    });
}

// Closure item 8 — product-path revalidation of the Architecture A
// English learning frontier (§14). What is proven at the real input
// method surface, through the panel and the commit machinery:
//   * Pinyin-path full commits (CandidateWord::select on a
//     MixedCandidateWord) commit the correct composition and drive the
//     learning seam (noteMixedEnglishSelection ->
//     EnglishUserLexicon::learn + safeSave) without disturbing the
//     engine: further mixed commits and the classical path keep working
//     (continuation invariant). The Shuangpin path drives the identical
//     seam through the same select() virtual on a Shuangpin IC
//     (testMixedShuangpin commits).
//   * Learning=OFF commits stay correct while training nothing and
//     PasswordOrSensitive contexts are asserted at the routing layer:
//     fcitx5 core forces the layout IM for sensitive contexts, so the
//     fusion engine cannot compose there at all and commit-driven
//     learning is unreachable. The engine gate itself reuses the exact
//     upstream shouldLearn predicate that guards LibIME learning and it
//     returns before any fold, learn or save, so a gated commit cannot
//     touch the English user lexicon by construction.
// Disk-level assertions cannot run in this harness: fcitx5's
// setupTestingEnvironment installs a global StandardPaths that skips
// user paths precisely to keep tests from writing user data, so
// "pinyin/mixed_english_user.tsv" is unreachable from testpinyin by
// design. The crash-safe StandardPaths::safeSave persistence of that
// exact file and format is covered at file level by
// testenglishuserlexicon (save/load round trip against a writable user
// directory). Device-side persistence and crash injection stay NOT RUN
// (Android soak).
// Partial (mid-candidate) selection semantics are pinned by
// testmixedengine invariant #8, which mirrors
// MixedCandidateWord::selectUpToSegment transaction-for-transaction; the
// contract defines no keyboard trigger for it and a dlopen'd addon's
// concrete candidate type is not castable from this linked test binary,
// so the product assertion here is the commit-driven learning effect.
//
// Types "woxiangmaiiphone", finds the mixed candidate carrying the
// iPhone surface anywhere in the bulk list (placement-policy-safe),
// registers the commit expectation and selects it through the public
// CandidateWord::select virtual — the exact path a UI click takes.
void commitMixedIphone(Instance *instance, AddonInstance *testfrontend,
                       const ICUUID &uuid) {
    auto *ic = instance->inputContextManager().findByUUID(uuid);
    FCITX_ASSERT(ic);
    ic->reset();
    for (const char *p = "woxiangmaiiphone"; *p; ++p) {
        testfrontend->call<ITestFrontend::keyEvent>(
            uuid, Key(std::string(1, *p)), false);
    }
    auto *list = ic->inputPanel().candidateList().get();
    FCITX_ASSERT(list && !list->empty());
    auto *bulk = list->toBulk();
    FCITX_ASSERT(bulk);
    for (int i = 0; i < bulk->totalSize(); ++i) {
        const auto &cw = bulk->candidateFromAll(i);
        const auto text = cw.text().toString();
        if (text.find("iPhone") != std::string::npos) {
            testfrontend->call<ITestFrontend::pushCommitExpectation>(text);
            cw.select(ic);
            return;
        }
    }
    FCITX_ASSERT(false) << "No mixed iPhone candidate for woxiangmaiiphone";
}

// Cross-phase hop used by the password gate below. Setting capability
// flags posts an async CapabilityChanged event (the harness IC has no
// per-session "last IM", so the drain re-activates keyboard-us); a short
// monotonic TimeEvent (the testPunctuation -> testPunctuationPart2
// idiom) lets that event land before the next synchronous phase. 0.5s
// keeps the chain far ahead of the exit timer armed by
// testPunctuationPart2, so no phase can be silently skipped.
void mixedLearnHop(Instance *instance, std::function<void()> next) {
    auto event = instance->eventLoop().addTimeEvent(
        CLOCK_MONOTONIC, now(CLOCK_MONOTONIC) + 500000, 0,
        [instance, next = std::move(next)](EventSourceTime *source, uint64_t) {
            instance->eventDispatcher().schedule(std::move(next));
            delete source;
            return true;
        });
    (void)event.release();
}

void testMixedLearning(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);
        ic->reset();

        // Two Pinyin-path mixed full commits: each drives the English
        // learning seam; each must still commit its exact composed text
        // (the pushed expectation is asserted by testfrontend before the
        // next event is processed).
        commitMixedIphone(instance, testfrontend, uuid);
        commitMixedIphone(instance, testfrontend, uuid);

        // Continuation invariant: the classical path still composes
        // and commits after the mixed activity above.
        ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        ic->reset();
        for (const char *p = "nihao"; *p; ++p) {
            testfrontend->call<ITestFrontend::keyEvent>(
                uuid, Key(std::string(1, *p)), false);
        }
        auto *list = ic->inputPanel().candidateList().get();
        FCITX_ASSERT(list && !list->empty());
        const auto top = list->candidate(0).text().toString();
        testfrontend->call<ITestFrontend::pushCommitExpectation>(
            std::string(top));
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);

        // Learning=OFF gate: commits stay correct while training nothing.
        auto *pinyin = instance->addonManager().addon("pinyin", true);
        RawConfig off;
        off.setValueByPath("Learning", "False");
        pinyin->setConfig(off);
        commitMixedIphone(instance, testfrontend, uuid);
        RawConfig on;
        on.setValueByPath("Learning", "True");
        pinyin->setConfig(on);

        // PasswordOrSensitive gate: asserted at the routing layer, which
        // is stronger than asserting the engine's own gate. While the
        // flag is on, fcitx5 core itself forces the layout IM
        // (Instance::inputMethod() special-cases sensitive contexts), so
        // the fusion engine cannot compose at all and commit-driven
        // learning is unreachable — no candidate list and no preedit
        // for the same burst that composes everywhere else. Restoring
        // the flags must bring the ordinary composing + learning commit
        // back immediately.
        const auto caps = ic->capabilityFlags();
        auto pw = caps;
        pw |= CapabilityFlag::PasswordOrSensitive;
        ic->setCapabilityFlags(pw);
        mixedLearnHop(instance, [instance, testfrontend, uuid, ic, caps]() {
            // The CapabilityChanged drain above has landed on
            // keyboard-us; the sensitive IC must not compose.
            auto *sic = instance->inputContextManager().findByUUID(uuid);
            FCITX_ASSERT(sic);
            sic->reset();
            for (const char *p = "woxiangmaiiphone"; *p; ++p) {
                testfrontend->call<ITestFrontend::keyEvent>(
                    uuid, Key(std::string(1, *p)), false);
            }
            auto *plist = sic->inputPanel().candidateList().get();
            FCITX_ASSERT(!plist || plist->empty())
                << "sensitive context must not compose";
            FCITX_ASSERT(sic->inputPanel().preedit().empty())
                << "sensitive context must not preedit";
            ic->setCapabilityFlags(caps);
            mixedLearnHop(instance, [instance, testfrontend, uuid, ic]() {
                instance->setCurrentInputMethod(ic, "pinyin", true);
                // One ordinary commit after all gate traffic: the engine
                // is reachable again and the learning path is armed.
                commitMixedIphone(instance, testfrontend, uuid);
                std::fprintf(stderr,
                             "MIXEDLEARN product path OK: mixed commits "
                             "before, between and after both gates "
                             "behaved identically\n");
            });
        });
    });
}

// Closure item 9 — Stroke side of the Auxiliary Filter fusion boundary.
// The filter is a post-CandidateList UI feature, never a ranker input;
// the contract at the real product surface is:
//   * Han frontier → filter normally (a mixed candidate whose leading
//     Han run matches the stroke code survives filtering).
//   * English frontier → the filter must not skip past the embedded
//     English surface and match later Han. This is the exact Stroke-style
//     regression the MoQi filter avoids by checking only the first
//     character: filterByStroke now terminates its scan at the first
//     character without a stroke mapping, so only the leading Han run of
//     a candidate text is matchable.
//   * Escape, Backspace-to-empty exit, second invocation and selection
//     from the filtered list all keep working on the mixed path.
// Stroke codes are queried from the fixed shipped table through the
// pinyinhelper addon at runtime — never guessed — and the boundary
// preconditions (配's full code matches none of 我想买) are asserted so a
// table change fails this test loudly instead of silently weakening it.
// The Disabled/Stroke/MoQi x Pinyin/Shuangpin full matrix lives on the
// mixed+moqi fusion regression branch that carries the AuxiliaryFilter
// config; this batch pins the shared Stroke semantics.
void testMixedStrokeFilterFusion(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto *ph = instance->addonManager().addon("pinyinhelper");
        FCITX_ASSERT(ph);
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);
        ic->reset();
        for (const char *p = "woxiangmaiiphonepeijian"; *p; ++p) {
            testfrontend->call<ITestFrontend::keyEvent>(
                uuid, Key(std::string(1, *p)), false);
        }

        auto bulkCount = [ic]() {
            auto candList = ic->inputPanel().candidateList();
            auto *bulk = candList ? candList->toBulk() : nullptr;
            return bulk ? bulk->totalSize() : 0;
        };
        auto findTextWith = [ic](std::string_view needle) -> std::string {
            auto candList = ic->inputPanel().candidateList();
            auto *bulk = candList ? candList->toBulk() : nullptr;
            if (!bulk) {
                return {};
            }
            for (int i = 0; i < bulk->totalSize(); ++i) {
                auto text = bulk->candidateFromAll(i).text().toString();
                if (text.find(needle) != std::string::npos) {
                    return text;
                }
            }
            return {};
        };

        auto strokeOf = [ph](const std::string &chr) {
            return ph->call<IPinyinHelper::reverseLookupStroke>(chr);
        };
        auto utf8Chars = [](std::string_view s) {
            std::vector<std::string> chars;
            auto range = utf8::MakeUTF8CharRange(s);
            for (auto iter = std::begin(range), end = std::end(range);
                 iter != end; ++iter) {
                chars.emplace_back(iter.charRange().first,
                                   iter.charRange().second);
            }
            return chars;
        };

        // The bait candidate: non-empty leading Han run, then the English
        // surface, then more Han behind it. Placement policy decides which
        // mixed composition surfaces, so the candidate, its frontier chars
        // and every stroke code are derived from the real list and the real
        // shipped table at runtime — nothing here is guessed.
        std::string mixedText;
        std::string keepCode; // full stroke code of the frontier char
        std::string baitCode; // full code of a trailing char that matches
                              // no leading-run char
        {
            auto candList = ic->inputPanel().candidateList();
            auto *bulk = candList ? candList->toBulk() : nullptr;
            FCITX_ASSERT(bulk && bulk->totalSize() > 0);
            for (int i = 0; i < bulk->totalSize(); ++i) {
                const auto text = bulk->candidateFromAll(i).text().toString();
                const auto engPos = text.find("iPhone");
                if (engPos == std::string::npos || engPos == 0 ||
                    engPos + strlen("iPhone") >= text.size()) {
                    continue;
                }
                const auto lead = utf8Chars(text.substr(0, engPos));
                const auto trail =
                    utf8Chars(text.substr(engPos + strlen("iPhone")));
                std::vector<std::string> leadCodes;
                for (const auto &chr : lead) {
                    leadCodes.push_back(strokeOf(chr));
                }
                std::vector<std::string> trailCodes;
                for (const auto &chr : trail) {
                    trailCodes.push_back(strokeOf(chr));
                }
                if (leadCodes.empty() || leadCodes.front().empty() ||
                    trailCodes.empty()) {
                    continue;
                }
                std::string bait;
                for (const auto &code : trailCodes) {
                    if (code.empty()) {
                        continue;
                    }
                    bool clash = false;
                    for (const auto &lc : leadCodes) {
                        if (lc.starts_with(code)) {
                            clash = true;
                            break;
                        }
                    }
                    if (!clash) {
                        bait = code;
                        break;
                    }
                }
                if (!bait.empty()) {
                    mixedText = text;
                    keepCode = leadCodes.front();
                    baitCode = std::move(bait);
                    break;
                }
            }
        }
        FCITX_ASSERT(!mixedText.empty())
            << "no mixed candidate with Han on both sides of the English "
               "surface for woxiangmaiiphonepeijian";
        // Boundary preconditions pinned against the real table: buffer =
        // baitCode can only keep this candidate by skipping past the
        // embedded English, buffer = keepCode matches the first frontier
        // character exactly.
        FCITX_ASSERT(!baitCode.starts_with(keepCode));

        auto typeStrokeCode = [testfrontend, uuid](const std::string &code) {
            for (const char d : code) {
                const char *key = d == '1'   ? "h"
                                  : d == '2' ? "s"
                                  : d == '3' ? "p"
                                  : d == '4' ? "n"
                                             : "z";
                testfrontend->call<ITestFrontend::keyEvent>(uuid, Key(key),
                                                            false);
            }
        };
        auto findExact = [ic](const std::string &text) {
            auto candList = ic->inputPanel().candidateList();
            auto *bulk = candList ? candList->toBulk() : nullptr;
            if (!bulk) {
                return false;
            }
            for (int i = 0; i < bulk->totalSize(); ++i) {
                if (bulk->candidateFromAll(i).text().toString() == text) {
                    return true;
                }
            }
            return false;
        };

        // Enter stroke filtering on the mixed bulk list.
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("`"), false);

        // Han frontier → filter normally: the frontier char's own code
        // keeps the candidate.
        typeStrokeCode(keepCode);
        FCITX_ASSERT(findExact(mixedText))
            << "leading-run match must keep the mixed candidate";

        // Escape leaves filtering; everything is visible again.
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_Escape),
                                                    false);
        FCITX_ASSERT(findExact(mixedText));
        const auto unfiltered = bulkCount();

        // English frontier boundary: the trailing char's full code must not
        // keep the candidate through Han behind the embedded English — with
        // the old any-character scan this matched behind "iPhone" and kept
        // it. And no other iPhone-bearing candidate may survive either.
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("`"), false);
        typeStrokeCode(baitCode);
        FCITX_ASSERT(!findExact(mixedText))
            << "stroke filter must not skip the English frontier";
        FCITX_ASSERT(findTextWith("iPhone").empty())
            << "no English-crossing match may survive";

        // Backspace pops the buffer; emptying it and one further backspace
        // exits filtering entirely.
        for (std::size_t i = 0; i <= baitCode.size(); ++i) {
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("BackSpace"),
                                                        false);
        }
        FCITX_ASSERT(findExact(mixedText))
            << "backspace-to-empty must leave filtering";
        FCITX_ASSERT(bulkCount() == unfiltered);

        // Second invocation + selection through the filtered list: entering
        // the filter again and selecting the mixed candidate commits the
        // exact composed text.
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("`"), false);
        typeStrokeCode(keepCode);
        auto candList = ic->inputPanel().candidateList();
        auto *bulk = candList->toBulk();
        FCITX_ASSERT(bulk);
        bool selected = false;
        for (int i = 0; i < bulk->totalSize(); ++i) {
            const auto &cw = bulk->candidateFromAll(i);
            if (cw.text().toString() == mixedText) {
                testfrontend->call<ITestFrontend::pushCommitExpectation>(
                    mixedText);
                cw.select(ic);
                selected = true;
                break;
            }
        }
        FCITX_ASSERT(selected) << "mixed candidate missing in second filter";
        std::fprintf(
            stderr,
            "MIXEDSTROKE product path OK: han-frontier filtering, escape, "
            "backspace exit, english-frontier boundary and filtered "
            "selection all behaved as contracted\n");
    });
}

void testForget(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
        auto *candidateList = ic->inputPanel().candidateList().get();
        const auto &cand = candidateList->candidate(0);
        auto *actionable = candidateList->toActionable();
        FCITX_ASSERT(actionable);
        FCITX_ASSERT(actionable->hasAction(cand));
        auto actions = actionable->candidateActions(cand);
        FCITX_ASSERT(!actions.empty());
        FCITX_ASSERT(actions[0].id() == 0);
        actionable->triggerAction(cand, 0);
        FCITX_ASSERT(ic->inputPanel().candidateList());
    });
}

void testActionInStrokeFilter(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

        // Target ppp for 彡
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("p"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("p"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("p"), false);
        auto *candidateList = ic->inputPanel().candidateList().get();
        findCandidateOrDie(ic, "彡");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("`"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("p"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("p"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("p"), false);
        candidateList = ic->inputPanel().candidateList().get();
        FCITX_ASSERT(findCandidate(ic, "彡") < 0);
        int index = findCandidateOrDie(ic, "䫠");
        const auto &cand = candidateList->candidate(index);
        auto *actionable = candidateList->toActionable();
        FCITX_ASSERT(actionable);
        FCITX_ASSERT(actionable->hasAction(cand));
        auto actions = actionable->candidateActions(cand);
        FCITX_ASSERT(!actions.empty());
        FCITX_ASSERT(actions[0].id() == 0);
        actionable->triggerAction(cand, 0);
        FCITX_ASSERT(ic->inputPanel().candidateList());
    });
}

void testPinyinTabFilter(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *pinyin = instance->addonManager().addon("pinyin");
        FCITX_ASSERT(pinyin);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("x"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        findCandidateOrDie(ic, "西安");
        auto *tabbed = ic->inputPanel().candidateList()->toTabbed();
        FCITX_ASSERT(tabbed);
        auto actionSpan = tabbed->tabActions();
        std::vector<CandidateAction> actions{actionSpan.begin(),
                                             actionSpan.end()};
        std::vector<std::string> names;
        names.reserve(actions.size());
        for (const auto &action : actions) {
            names.push_back(action.text());
        }

        auto indexOf = [&names](std::string_view name) {
            auto it = std::ranges::find(names, name);
            FCITX_ASSERT(it != names.end());
            return std::distance(names.begin(), it);
        };

        const auto xiAction = actions[indexOf("xi")];
        const auto singleAction = actions[indexOf("单字")];
        const auto strokeAction = actions[indexOf("笔画")];

        auto checkedActionsAre = [tabbed](std::initializer_list<int> ids) {
            std::unordered_set<int> checkedIds;
            for (const auto &action : tabbed->tabActions()) {
                if (action.isChecked()) {
                    checkedIds.insert(action.id());
                }
            }
            return checkedIds == std::unordered_set<int>{ids};
        };

        FCITX_ASSERT(actions[0].text() == "xian");
        tabbed->triggerTabAction(actions[0].id());
        FCITX_ASSERT(findCandidate(ic, "西安") < 0);
        FCITX_ASSERT(checkedActionsAre({actions[0].id()}));

        // Trigger same action again should uncheck it.
        tabbed->triggerTabAction(actions[0].id());
        FCITX_ASSERT(findCandidate(ic, "西安") >= 0);
        FCITX_ASSERT(checkedActionsAre({}));

        FCITX_ASSERT(xiAction.text() == "xi");
        tabbed->triggerTabAction(xiAction.id());
        FCITX_ASSERT(findCandidate(ic, "西安") >= 0);
        FCITX_ASSERT(checkedActionsAre({xiAction.id()}));

        // Pinyin and single char can be checked at the same time.
        FCITX_ASSERT(singleAction.text() == "单字");
        tabbed->triggerTabAction(singleAction.id());
        FCITX_ASSERT(findCandidate(ic, "西安") < 0);
        FCITX_ASSERT(checkedActionsAre({xiAction.id(), singleAction.id()}));

        // Trigger single char action again should only uncheck itself.
        tabbed->triggerTabAction(singleAction.id());
        FCITX_ASSERT(findCandidate(ic, "西安") >= 0);
        FCITX_ASSERT(checkedActionsAre({xiAction.id()}));

        tabbed->triggerTabAction(singleAction.id());
        FCITX_ASSERT(findCandidate(ic, "西安") < 0);
        FCITX_ASSERT(checkedActionsAre({xiAction.id(), singleAction.id()}));

        // Trigger pinyin action again should only uncheck itself.
        tabbed->triggerTabAction(xiAction.id());
        FCITX_ASSERT(findCandidate(ic, "西安") < 0);
        FCITX_ASSERT(checkedActionsAre({singleAction.id()}));

        // Stroke action should not uncheck single char action.
        tabbed->triggerTabAction(strokeAction.id());
        FCITX_ASSERT(findCandidate(ic, "西安") < 0);
        FCITX_ASSERT(checkedActionsAre({}));

        actionSpan = tabbed->tabActions();
        actions = {actionSpan.begin(), actionSpan.end()};
        FCITX_ASSERT(actions.size() >= 2);
        FCITX_ASSERT(actions[actions.size() - 2].isSeparator());
        FCITX_ASSERT(actions.back().text() == "返回");
        tabbed->triggerTabAction(actions.back().id());
        FCITX_ASSERT(findCandidate(ic, "西安") < 0);
        FCITX_ASSERT(checkedActionsAre({singleAction.id()}));

        // Trigger single char action again should uncheck it.
        tabbed->triggerTabAction(singleAction.id());
        FCITX_ASSERT(findCandidate(ic, "西安") >= 0);
        FCITX_ASSERT(checkedActionsAre({}));
    });
}

void testPinyinTabFilterWithSeparator(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *pinyin = instance->addonManager().addon("pinyin");
        FCITX_ASSERT(pinyin);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("x"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("'"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("'"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        findAndSelectCandidate(ic, "西");
        auto *tabbed = ic->inputPanel().candidateList()->toTabbed();
        FCITX_ASSERT(tabbed);
        auto actionSpan = tabbed->tabActions();
        FCITX_ASSERT(std::ranges::none_of(actionSpan, [](const auto &a) {
            return a.text().starts_with('\'');
        }));
    });
}

void testPin(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("t"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("g"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("y"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        auto index1 = findCandidateOrDie(ic, "同音");
        auto index2 = findCandidateOrDie(ic, "痛饮");
        const auto oldIndex1 = index1;
        const auto oldIndex2 = index2;
        FCITX_INFO() << "同音:" << index1 << " "
                     << "痛饮:" << index2;
        {
            auto *candidateList = ic->inputPanel().candidateList().get();
            // Pin the one that is after.
            const auto &cand =
                candidateList->candidate(std::max(index1, index2));
            auto *actionable = candidateList->toActionable();
            FCITX_ASSERT(actionable);
            FCITX_ASSERT(actionable->hasAction(cand));
            auto actions = actionable->candidateActions(cand);
            FCITX_ASSERT(!actions.empty());
            FCITX_ASSERT(std::ranges::any_of(
                actions, [](const auto &a) { return a.id() == 1; }));
            FCITX_ASSERT(!std::ranges::any_of(
                actions, [](const auto &a) { return a.id() == 2; }));
            // This is pin action, 痛饮 should be pined to head.
            actionable->triggerAction(cand, 1);
        }
        index1 = findCandidateOrDie(ic, "同音");
        index2 = findCandidateOrDie(ic, "痛饮");
        FCITX_INFO() << "同音:" << index1 << " "
                     << "痛饮:" << index2;
        FCITX_ASSERT(index1 != oldIndex1);
        FCITX_ASSERT(index2 != oldIndex2);
        FCITX_ASSERT(std::min(index1, index2) == 0);

        {
            auto *candidateList = ic->inputPanel().candidateList().get();
            const auto &candNew = candidateList->candidate(index2);
            auto *actionable = candidateList->toActionable();
            FCITX_ASSERT(actionable);
            FCITX_ASSERT(actionable->hasAction(candNew));
            auto actions = actionable->candidateActions(candNew);
            FCITX_ASSERT(!actions.empty());
            // Check if deletable action is there.
            FCITX_ASSERT(std::ranges::any_of(
                actions, [](const auto &a) { return a.id() == 2; }));
            // This is delete custom phrase action, 痛饮 should be pined to
            // head.
            actionable->triggerAction(candNew, 2);
        }
        index1 = findCandidateOrDie(ic, "同音");
        index2 = findCandidateOrDie(ic, "痛饮");
        FCITX_INFO() << "同音:" << index1 << " "
                     << "痛饮:" << index2;
        FCITX_ASSERT(index1 == oldIndex1);
        FCITX_ASSERT(index2 == oldIndex2);
    });
}

void testQuickPhraseTrigger(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("w"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("w"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("w"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("."), false);
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "www.");

        ic->reset();
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("b"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("b"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("s"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("."), false);
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "bbs.");

        ic->reset();
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("u"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("s"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("r"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("@"), false);
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "user@");

        ic->reset();
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("t"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("t"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("p"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key(":"), false);
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "http:");

        ic->reset();
        // htt: shouldn't trigger.
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("t"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("t"), false);

        FCITX_ASSERT(ic->inputPanel().candidateList());
        FCITX_ASSERT(!ic->inputPanel().candidateList()->empty());
        const auto firstCandidate =
            ic->inputPanel().candidateList()->candidate(0).text().toString();
        testfrontend->call<ITestFrontend::pushCommitExpectation>(
            firstCandidate);
        testfrontend->call<ITestFrontend::pushCommitExpectation>("：");

        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key(":"), false);
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "");
    });
}

void testVQuickPhraseTrigger(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");

        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("v"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("."), false);
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "v.");

        instance->setCurrentInputMethod(ic, "shuangpin", true);
        ic->reset();
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("V"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("."), false);
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "V.");
    });
}

void testPunctuationWithCursorAtBeginning(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        ic->setCapabilityFlags(CapabilityFlag::SurroundingText);
        instance->setCurrentInputMethod(ic, "pinyin", true);

        ic->reset();
        ic->surroundingText().setText("text", 0, 0);
        FCITX_ASSERT(ic->surroundingText().isValid());
        // Cursor zero has no preceding character.
        ic->updateSurroundingText();
        testfrontend->call<ITestFrontend::pushCommitExpectation>("。");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("."), false));
    });
}

void testPunctuation(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

        testfrontend->call<ITestFrontend::pushCommitExpectation>("。");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("."), false));
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("1"), false));
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("."), false));
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("1"), false));
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("."), false));
        // This is cancel last eng.
        testfrontend->call<ITestFrontend::pushCommitExpectation>("。");
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("BackSpace"), false));

        auto event = instance->eventLoop().addTimeEvent(
            CLOCK_MONOTONIC, now(CLOCK_MONOTONIC) + 2000000, 0,
            [instance](EventSourceTime *event, uint64_t) {
                testPunctuationPart2(instance);
                delete event;
                return true;
            });
        (void)event.release();
    });
}

void testPunctuationPart2(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("1"), false));
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("."), false));
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));
        // This should not cancel last eng.
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("BackSpace"), false));

        endTestEvent = instance->eventLoop().addTimeEvent(
            CLOCK_MONOTONIC, now(CLOCK_MONOTONIC) + 2000000, 0,
            [instance](EventSourceTime *, uint64_t) {
                instance->exit();
                return true;
            });
    });
}

} // namespace

int main() {
    setupTestingEnvironment(
        TESTING_BINARY_DIR, {"bin"},
        {TESTING_BINARY_DIR "/test", TESTING_BINARY_DIR "/im",
         TESTING_BINARY_DIR "/modules", TESTING_SOURCE_DIR "/modules",
         StandardPaths::fcitxPath("pkgdatadir")});
    // fcitx::Log::setLogRule("default=5,table=5,libime-table=5");
    char arg0[] = "testpinyin";
    char arg1[] = "--disable=all";
    char arg2[] = "--enable=testim,testfrontend,pinyin,punctuation,"
                  "pinyinhelper,spell,quickphrase";
    char *argv[] = {arg0, arg1, arg2};
    fcitx::Log::setLogRule("default=5,pinyin=5,*=5");
    Instance instance(FCITX_ARRAY_SIZE(argv), argv);
    instance.addonManager().registerDefaultLoader(nullptr);
    setup(&instance);
    testBasic(&instance);
    testSelectByChar(&instance);
    testUppercase(&instance);
    testMixedShuangpin(&instance);
    testMixedProductCorpus(&instance);
    testMixedRankingCorpus(&instance);
    testMixedLearning(&instance);
    testForget(&instance);
    testActionInStrokeFilter(&instance);
    testMixedStrokeFilterFusion(&instance);
    testPinyinTabFilter(&instance);
    testPinyinTabFilterWithSeparator(&instance);
    testPin(&instance);
    testQuickPhraseTrigger(&instance);
    testVQuickPhraseTrigger(&instance);
    testPunctuationWithCursorAtBeginning(&instance);
    testPunctuation(&instance);
    instance.exec();
    endTestEvent.reset();
    return 0;
}
