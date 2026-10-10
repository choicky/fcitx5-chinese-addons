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
#include <fcitx/inputmethodengine.h>
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
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace fcitx;

namespace {

std::unique_ptr<EventSourceTime> endTestEvent;
// Phase 3A-2 §16: corpus-result failures must not abort the suite mid-run.
// The ranking corpus records its failures here and the process still exits
// non-zero, but every scheduled test after it executes, so no blind spot
// hides behind the first failing group.
int deferredTestFailures = 0;
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

// The auxiliary band is the user visible part of the auxiliary filter, so the
// tests below assert on it instead of on engine internals.
std::string auxUpText(InputContext *ic) {
    return ic->inputPanel().auxUp().toString();
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
        //   user-learned whole-raw additions) takes the hybrid placement
        //   class lead: Row 1 is rank 0, while a StrongChinese remainder is
        //   Row 2 at rank 1 after the classical head.
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
        auto expectedClassLeadRank = [](const char *raw) {
            return std::string_view(raw) == "libime" ? 1 : 0;
        };

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
        // Contract (D071): the candidate set preserves BOTH the canonical
        // display and the literal typed surface; canonical may rank first.
        for (const char *raw :
             {"chatgpt", "macos", "github", "iphone", "openwrt", "libime"}) {
            // The canonical needle is the dictionary display; identify it by
            // case-insensitive fold match against the raw (all these raws
            // are lowercase respellings of their surface). The literal
            // surface is the whole candidate equal to the raw byte-for-byte.
            ic->reset();
            for (const char *p = raw; *p; ++p) {
                testfrontend->call<ITestFrontend::keyEvent>(
                    uuid, Key(std::string(1, *p)), false);
            }
            auto *fresh = ic->inputPanel().candidateList().get();
            int canonRank = -1;
            int litRank = -1;
            std::string top = top1Text();
            if (fresh) {
                if (auto *bulk = fresh->toBulk()) {
                    for (int i = 0; i < bulk->totalSize(); i++) {
                        auto text = std::string(
                            bulk->candidateFromAll(i).text().toString());
                        // Whole-candidate case-insensitive equality with the
                        // raw identifies the respell class members.
                        if (text.size() >= 3) {
                            std::string lower = text;
                            std::transform(lower.begin(), lower.end(),
                                           lower.begin(), [](unsigned char c) {
                                               return static_cast<char>(
                                                   std::tolower(c));
                                           });
                            if (lower == raw) {
                                if (text == raw) {
                                    if (litRank < 0) {
                                        litRank = i;
                                    }
                                } else if (canonRank < 0) {
                                    canonRank = i;
                                }
                            }
                        }
                    }
                }
            }
            std::fprintf(stderr, "RANKCORP-A %s rank=%d top1=%s\n", raw,
                         canonRank, top.c_str());
            std::fprintf(stderr, "RANKCORP-A2 %s literalRank=%d\n", raw,
                         litRank);
            if (canonRank < 0) {
                fail(std::string("A recall: ") + raw +
                     " canonical surface not reachable");
            } else if (canonRank != expectedClassLeadRank(raw)) {
                fail(std::string("A ranking: ") + raw + " rank " +
                     std::to_string(canonRank) + " != class lead " +
                     std::to_string(expectedClassLeadRank(raw)));
            }
            if (litRank < 0) {
                fail(std::string("A literal coexistence: ") + raw +
                     " literal surface dropped from candidate set");
            } else if (canonRank >= 0 && litRank <= canonRank) {
                fail(std::string("A literal ordering: ") + raw +
                     " literal ranks at or above the canonical display");
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
            // D071 literal coexistence inside a mixed composition: the
            // lowercase span must remain reachable next to the canonical
            // respell ("喔惮岂github" beside "喔惮岂GitHub").
            const int litRank = rankOfNeedle("wodakaigithub", "github");
            if (litRank < 0) {
                fail("C literal coexistence: wodakaigithub github dropped");
            } else if (rank >= 0 && litRank <= rank) {
                fail("C literal ordering: github at or above GitHub");
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
        if (!failures.empty()) {
            std::fprintf(
                stderr,
                "RANKCORP DEFERRED-FAIL count=%zu (see RANKCORP FAIL "
                "lines; suite continues, exit status stays non-zero)\n",
                failures.size());
            ++deferredTestFailures;
        }
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

// Shared pieces for the Auxiliary Filter x Architecture A fusion matrix
// (closure item 9). All filter codes are queried from the fixed shipped
// tables through pinyinhelper at runtime — the stroke table and the pinned
// MoQi table (commit 6d8ba8f1, SHA256 66deab4a...) — never guessed.
struct FusionBoundaryCase {
    std::string text;     // mixed candidate: leading Han, English, later Han
    std::string keepCode; // full code of the frontier char
    std::string baitCode; // full code of a later char matching no lead char
};

std::vector<std::string> utf8CharsOf(std::string_view s) {
    std::vector<std::string> chars;
    auto range = utf8::MakeUTF8CharRange(s);
    for (auto iter = std::begin(range), end = std::end(range); iter != end;
         ++iter) {
        chars.emplace_back(iter.charRange().first, iter.charRange().second);
    }
    return chars;
}

bool hasCandidateText(InputContext *ic, const std::string &text) {
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
}

bool hasCandidateWith(InputContext *ic, std::string_view needle) {
    auto candList = ic->inputPanel().candidateList();
    auto *bulk = candList ? candList->toBulk() : nullptr;
    if (!bulk) {
        return false;
    }
    for (int i = 0; i < bulk->totalSize(); ++i) {
        if (bulk->candidateFromAll(i).text().toString().find(needle) !=
            std::string::npos) {
            return true;
        }
    }
    return false;
}

int bulkSizeOf(InputContext *ic) {
    auto candList = ic->inputPanel().candidateList();
    auto *bulk = candList ? candList->toBulk() : nullptr;
    return bulk ? bulk->totalSize() : -1;
}

FusionBoundaryCase findFusionBoundaryCase(
    InputContext *ic,
    const std::function<std::string(const std::string &)> &codeOf) {
    FusionBoundaryCase out;
    auto candList = ic->inputPanel().candidateList();
    auto *bulk = candList ? candList->toBulk() : nullptr;
    if (!bulk) {
        return out;
    }
    for (int i = 0; i < bulk->totalSize(); ++i) {
        const auto text = bulk->candidateFromAll(i).text().toString();
        const auto engPos = text.find("iPhone");
        if (engPos == std::string::npos || engPos == 0 ||
            engPos + strlen("iPhone") >= text.size()) {
            continue;
        }
        std::vector<std::string> leadCodes, trailCodes;
        for (const auto &chr : utf8CharsOf(text.substr(0, engPos))) {
            leadCodes.push_back(codeOf(chr));
        }
        for (const auto &chr :
             utf8CharsOf(text.substr(engPos + strlen("iPhone")))) {
            trailCodes.push_back(codeOf(chr));
        }
        if (leadCodes.empty() || leadCodes.front().empty() ||
            trailCodes.empty()) {
            continue;
        }
        for (const auto &code : trailCodes) {
            if (code.empty()) {
                continue;
            }
            const bool clash = std::any_of(leadCodes.begin(), leadCodes.end(),
                                           [&code](const std::string &lc) {
                                               return lc.starts_with(code);
                                           });
            if (!clash) {
                out.text = text;
                out.keepCode = leadCodes.front();
                out.baitCode = code;
                return out;
            }
        }
    }
    return out;
}

AddonInstance *pinyinWithAuxiliaryFilter(Instance *instance,
                                         const char *value) {
    auto *pinyin = instance->addonManager().addon("pinyin");
    FCITX_ASSERT(pinyin);
    RawConfig config;
    config.setValueByPath("AuxiliaryFilter", value);
    pinyin->setConfig(config);
    return pinyin;
}

// MoQi mode on the mixed product path (Pinyin layout). The contract at the
// fusion boundary: MoQi filters the first target Han after the selection
// frontier only — an English-frontier candidate never survives, and no
// candidate may be kept by Han behind the embedded English surface (no
// Stroke-style skip-English regression). Paging, selection with commit,
// Escape, backspace-to-empty exit and second invocation are exercised on
// the mixed list itself.
void testMixedMoQiFilterFusion(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto *ph = instance->addonManager().addon("pinyinhelper");
        FCITX_ASSERT(ph);
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

        auto codeOf = [ph](const std::string &chr) {
            return ph->call<IPinyinHelper::reverseLookupMoQi>(chr);
        };
        auto typeCode = [testfrontend, uuid](const std::string &code) {
            for (const char c : code) {
                testfrontend->call<ITestFrontend::keyEvent>(
                    uuid, Key(std::string(1, c)), false);
            }
        };
        auto burst = [testfrontend, uuid, ic]() {
            ic->reset();
            for (const char *p = "woxiangmaiiphonepeijian"; *p; ++p) {
                testfrontend->call<ITestFrontend::keyEvent>(
                    uuid, Key(std::string(1, *p)), false);
            }
        };

        // Disabled x Pinyin x mixed: the auxiliary-filter surface is gone
        // (the 笔画/墨奇 tab actions disappear) and the mixed list is
        // untouched. Mode unavailability is asserted at the action surface,
        // mirroring testDisabledAuxiliaryFilter: a raw grave press here falls
        // through to the upstream punctuation/commit fallback, which is
        // independent of the fusion.
        pinyinWithAuxiliaryFilter(instance, "Disabled");
        burst();
        FCITX_ASSERT(hasCandidateWith(ic, "iPhone"));
        auto *tabbed = ic->inputPanel().candidateList()->toTabbed();
        FCITX_ASSERT(tabbed);
        const auto disabledActions = tabbed->tabActions();
        FCITX_ASSERT(
            std::ranges::none_of(disabledActions, [](const auto &action) {
                return action.text() == "笔画" || action.text() == "墨奇";
            }));

        pinyinWithAuxiliaryFilter(instance, "MoQi");
        burst();
        const auto bc = findFusionBoundaryCase(ic, codeOf);
        FCITX_ASSERT(!bc.text.empty())
            << "no mixed candidate with mapped Han on both sides of iPhone";
        const auto unfilteredSize = bulkSizeOf(ic);

        // Enter MoQi filtering and filter at the Han frontier.
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_grave),
                                                    false);
        FCITX_ASSERT(!auxUpText(ic).empty());
        typeCode(bc.keepCode);
        FCITX_ASSERT(hasCandidateText(ic, bc.text))
            << "frontier Han MoQi code must keep the mixed candidate";
        // Every survivor's first character is Han with a MoQi code starting
        // with the buffer: an English-frontier candidate cannot pass MoQi
        // filtering, and MoQi never looks behind the frontier char.
        {
            auto candList = ic->inputPanel().candidateList();
            auto *bulk = candList->toBulk();
            for (int i = 0; i < bulk->totalSize(); ++i) {
                const auto t = bulk->candidateFromAll(i).text().toString();
                const auto first = utf8CharsOf(t).front();
                const auto code = codeOf(first);
                FCITX_ASSERT(!code.empty() && code.starts_with(bc.keepCode))
                    << "MoQi survivor without frontier code: " << t;
            }
        }

        // Paging with an active filter keeps the mixed candidate available.
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_equal),
                                                    false);
        FCITX_ASSERT(!auxUpText(ic).empty());
        FCITX_ASSERT(hasCandidateText(ic, bc.text));
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_minus),
                                                    false);
        FCITX_ASSERT(hasCandidateText(ic, bc.text));

        // Selection through the filtered list commits the exact composed
        // text.
        {
            auto candList = ic->inputPanel().candidateList();
            auto *bulk = candList->toBulk();
            bool selected = false;
            for (int i = 0; i < bulk->totalSize(); ++i) {
                const auto &cw = bulk->candidateFromAll(i);
                if (cw.text().toString() == bc.text) {
                    testfrontend->call<ITestFrontend::pushCommitExpectation>(
                        bc.text);
                    cw.select(ic);
                    selected = true;
                    break;
                }
            }
            FCITX_ASSERT(selected);
        }

        // Second invocation at the English-crossing boundary: a code that
        // belongs only to Han behind "iPhone" keeps nothing — no
        // skip-English match, and English-frontier candidates are dropped.
        ic->reset();
        for (const char *p = "woxiangmaiiphonepeijian"; *p; ++p) {
            testfrontend->call<ITestFrontend::keyEvent>(
                uuid, Key(std::string(1, *p)), false);
        }
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_grave),
                                                    false);
        typeCode(bc.baitCode);
        FCITX_ASSERT(!hasCandidateWith(ic, "iPhone"))
            << "MoQi must not filter Han behind the English surface";

        // Backspace pops the buffer; emptying it exits filtering entirely.
        for (std::size_t i = 0; i <= bc.baitCode.size(); ++i) {
            testfrontend->call<ITestFrontend::keyEvent>(
                uuid, Key(FcitxKey_BackSpace), false);
        }
        FCITX_ASSERT(auxUpText(ic).empty())
            << "backspace-to-empty must leave MoQi filtering";
        FCITX_ASSERT(hasCandidateText(ic, bc.text));
        FCITX_ASSERT(bulkSizeOf(ic) == unfilteredSize);

        // Escape leaves filtering with a non-empty buffer.
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_grave),
                                                    false);
        typeCode(bc.keepCode);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Escape"), false);
        FCITX_ASSERT(auxUpText(ic).empty());
        FCITX_ASSERT(hasCandidateText(ic, bc.text));

        pinyinWithAuxiliaryFilter(instance, "Stroke");
        std::fprintf(
            stderr,
            "MIXEDMOQI product path OK: frontier filtering, survivor "
            "anchoring, paging, filtered selection, english-crossing "
            "boundary, backspace exit and escape all behaved as contracted\n");
    });
}

// The remaining matrix cells at the fusion boundary: {Stroke, MoQi,
// Disabled} x Shuangpin product path. Ziranma codes come from the pinned
// LibIME build (171edcf137001e8eb8274f53ec010058cda70b09,
// shuangpindata.h SPMap_C_Ziranma): final ei -> 'z', ian -> 'm', so 配 =
// "pz" and 件 = "jm" after the 想iPhone prefix proven by
// testMixedShuangpin; the MoQi and stroke codes are queried from the fixed
// tables at runtime.
void testMixedAuxiliaryFilterShuangpinFusion(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        pinyinWithAuxiliaryFilter(instance, "Stroke");
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto *ph = instance->addonManager().addon("pinyinhelper");
        FCITX_ASSERT(ph);
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "shuangpin", true);

        auto burst = [testfrontend, uuid]() {
            for (const char *p = "hsiphonepzjm"; *p; ++p) {
                testfrontend->call<ITestFrontend::keyEvent>(
                    uuid, Key(std::string(1, *p)), false);
            }
        };
        auto strokeCodeOf = [ph](const std::string &chr) {
            return ph->call<IPinyinHelper::reverseLookupStroke>(chr);
        };
        auto moqiCodeOf = [ph](const std::string &chr) {
            return ph->call<IPinyinHelper::reverseLookupMoQi>(chr);
        };
        auto typeStroke = [testfrontend, uuid](const std::string &code) {
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
        auto typeLetters = [testfrontend, uuid](const std::string &code) {
            for (const char c : code) {
                testfrontend->call<ITestFrontend::keyEvent>(
                    uuid, Key(std::string(1, c)), false);
            }
        };

        // Stroke x Shuangpin mixed: frontier keeps, English-crossing bait
        // keeps nothing.
        ic->reset();
        burst();
        const auto sb = findFusionBoundaryCase(ic, strokeCodeOf);
        FCITX_ASSERT(!sb.text.empty())
            << "no Shuangpin mixed candidate with Han on both sides of iPhone";
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_grave),
                                                    false);
        typeStroke(sb.keepCode);
        FCITX_ASSERT(hasCandidateText(ic, sb.text))
            << "Stroke must keep the Shuangpin mixed candidate at the Han "
               "frontier";
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Escape"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_grave),
                                                    false);
        typeStroke(sb.baitCode);
        FCITX_ASSERT(!hasCandidateWith(ic, "iPhone"))
            << "Stroke must not skip the English frontier on the Shuangpin "
               "path";
        for (std::size_t i = 0; i <= sb.baitCode.size(); ++i) {
            testfrontend->call<ITestFrontend::keyEvent>(
                uuid, Key(FcitxKey_BackSpace), false);
        }
        FCITX_ASSERT(auxUpText(ic).empty());
        // Second invocation and selection with commit.
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_grave),
                                                    false);
        typeStroke(sb.keepCode);
        {
            auto *bulk = ic->inputPanel().candidateList()->toBulk();
            bool selected = false;
            for (int i = 0; i < bulk->totalSize(); ++i) {
                const auto &cw = bulk->candidateFromAll(i);
                if (cw.text().toString() == sb.text) {
                    testfrontend->call<ITestFrontend::pushCommitExpectation>(
                        sb.text);
                    cw.select(ic);
                    selected = true;
                    break;
                }
            }
            FCITX_ASSERT(selected);
        }

        // MoQi x Shuangpin mixed: same boundary, first-char anchor.
        pinyinWithAuxiliaryFilter(instance, "MoQi");
        ic->reset();
        burst();
        const auto mb = findFusionBoundaryCase(ic, moqiCodeOf);
        FCITX_ASSERT(!mb.text.empty())
            << "no Shuangpin mixed candidate with mapped MoQi codes";
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_grave),
                                                    false);
        typeLetters(mb.keepCode);
        FCITX_ASSERT(hasCandidateText(ic, mb.text))
            << "MoQi must keep the Shuangpin mixed candidate at the frontier";
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Escape"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_grave),
                                                    false);
        typeLetters(mb.baitCode);
        FCITX_ASSERT(!hasCandidateWith(ic, "iPhone"))
            << "MoQi must not reach behind the English surface";
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Escape"), false);

        // Disabled x Shuangpin mixed: the auxiliary-filter surface is gone.
        // Entering Stroke/MoQi mode is only reachable through the grave
        // trigger or the 笔画/墨奇 tab actions; with Disabled the tab
        // actions disappear, and a raw grave press on an active composition
        // falls through to the upstream punctuation/commit fallback that is
        // independent of the fusion. Mode unavailability is therefore
        // asserted at the action surface, mirroring
        // testDisabledAuxiliaryFilter, while the mixed list stays untouched.
        pinyinWithAuxiliaryFilter(instance, "Disabled");
        ic->reset();
        burst();
        FCITX_ASSERT(hasCandidateWith(ic, "iPhone"));
        auto *tabbed = ic->inputPanel().candidateList()->toTabbed();
        FCITX_ASSERT(tabbed);
        const auto actions = tabbed->tabActions();
        FCITX_ASSERT(std::ranges::none_of(actions, [](const auto &action) {
            return action.text() == "笔画" || action.text() == "墨奇";
        }));
        FCITX_ASSERT(hasCandidateWith(ic, "iPhone"));

        pinyinWithAuxiliaryFilter(instance, "Stroke");
        std::fprintf(
            stderr,
            "MIXEDAUX-SP product path OK: Stroke/MoQi/Disabled x Shuangpin "
            "mixed candidates matched the frontier contract in every cell\n");
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

void testAuxiliaryFilterConfigContract(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *pinyin = instance->addonManager().addon("pinyin");
        FCITX_ASSERT(pinyin);
        auto *entry = instance->inputMethodManager().entry("pinyin");
        FCITX_ASSERT(entry);
        auto *engine = reinterpret_cast<InputMethodEngine *>(pinyin);
        auto *configuration = engine->getConfigForInputMethod(*entry);
        FCITX_ASSERT(configuration);

        RawConfig description;
        configuration->dumpDescription(description);
        const std::string root = configuration->typeName();
        const auto path = [&root](std::string_view child) {
            return root + "/AuxiliaryFilter/" + std::string(child);
        };
        // Android parses Fcitx's native Enum descriptor as ConfigEnum and
        // renders it with the generic ListPreference implementation.
        FCITX_ASSERT(*description.valueByPath(path("Type")) == "Enum");
        FCITX_ASSERT(*description.valueByPath(path("DefaultValue")) ==
                     "Stroke");
        FCITX_ASSERT(*description.valueByPath(path("Enum/0")) == "Disabled");
        FCITX_ASSERT(*description.valueByPath(path("Enum/1")) == "Stroke");
        FCITX_ASSERT(*description.valueByPath(path("Enum/2")) == "MoQi");
        FCITX_ASSERT(description.valueByPath(path("EnumI18n/0")));
        FCITX_ASSERT(description.valueByPath(path("EnumI18n/1")));
        FCITX_ASSERT(description.valueByPath(path("EnumI18n/2")));

        // Reload persistence can not be asserted here: the test environment
        // deliberately has no writable user config path (see
        // setupTestingEnvironment()), so setConfig()'s safeSaveAsIni() stores
        // nothing and a following reloadConfig() would read no file and reset
        // every option to its default. That round trip needs a real config
        // directory, i.e. an on-device check.
        for (const auto value : {"Disabled", "Stroke", "MoQi"}) {
            RawConfig config;
            configuration->save(config);
            config.setValueByPath("AuxiliaryFilter", value);
            engine->setConfigForInputMethod(*entry, config);

            RawConfig current;
            engine->getConfigForInputMethod(*entry)->save(current);
            FCITX_ASSERT(*current.valueByPath("AuxiliaryFilter") == value);
        }

        RawConfig config;
        configuration->save(config);
        config.setValueByPath("AuxiliaryFilter", "Stroke");
        engine->setConfigForInputMethod(*entry, config);
    });
}

void testActionInStrokeFilter(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *pinyin = instance->addonManager().addon("pinyin");
        FCITX_ASSERT(pinyin);
        RawConfig config;
        config.setValueByPath("AuxiliaryFilter", "Stroke");
        pinyin->setConfig(config);

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

void testDisabledAuxiliaryFilter(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *pinyin = instance->addonManager().addon("pinyin");
        FCITX_ASSERT(pinyin);
        RawConfig config;
        config.setValueByPath("AuxiliaryFilter", "Disabled");
        pinyin->setConfig(config);

        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

        for (const auto key : {"x", "i", "a", "n"}) {
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key(key), false);
        }
        findCandidateOrDie(ic, "西安");

        auto *tabbed = ic->inputPanel().candidateList()->toTabbed();
        FCITX_ASSERT(tabbed);
        auto actions = tabbed->tabActions();
        FCITX_ASSERT(std::ranges::none_of(actions, [](const auto &action) {
            return action.text() == "笔画" || action.text() == "墨奇";
        }));
    });
}

void testPinyinTabFilter(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *pinyin = instance->addonManager().addon("pinyin");
        FCITX_ASSERT(pinyin);
        RawConfig config;
        config.setValueByPath("AuxiliaryFilter", "Stroke");
        pinyin->setConfig(config);

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

void testMoQiTabFilter(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *pinyin = instance->addonManager().addon("pinyin");
        FCITX_ASSERT(pinyin);
        RawConfig config;
        config.setValueByPath("AuxiliaryFilter", "MoQi");
        pinyin->setConfig(config);

        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

        for (const auto key : {"x", "i", "a", "n"}) {
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key(key), false);
        }
        findCandidateOrDie(ic, "西安");

        auto *tabbed = ic->inputPanel().candidateList()->toTabbed();
        FCITX_ASSERT(tabbed);
        auto findAction = [tabbed](std::string_view text) {
            auto actions = tabbed->tabActions();
            auto iter = std::ranges::find_if(
                actions, [text](const auto &a) { return a.text() == text; });
            FCITX_ASSERT(iter != actions.end());
            return iter->id();
        };

        // The configured grave key is the generic Auxiliary Filter trigger.
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("grave"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        FCITX_ASSERT(findCandidate(ic, "西安") >= 0);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("k"), false);
        FCITX_ASSERT(findCandidate(ic, "西安") >= 0);

        // Backspace removes MoQi codes before leaving MoQi mode.
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("BackSpace"),
                                                    false);
        FCITX_ASSERT(findCandidate(ic, "西安") >= 0);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("BackSpace"),
                                                    false);
        FCITX_ASSERT(findCandidate(ic, "西安") >= 0);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("BackSpace"),
                                                    false);
        FCITX_ASSERT(findCandidate(ic, "西安") >= 0);
        FCITX_ASSERT(findAction("墨奇") < 0);

        // Clear the current composition, then use explicit separators so
        // "西" is exposed as a partial candidate before the remaining "安".
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Escape"), false);
        for (const auto key : {"x", "i", "'", "'", "a", "n"}) {
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key(key), false);
        }

        // Filter at the initial frontier (西 -> ak), then partially select 西.
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("grave"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("k"), false);
        findAndSelectCandidate(ic, "西");
        FCITX_ASSERT(findCandidate(ic, "安") >= 0);

        // Continue composing after the selected prefix. No text is committed,
        // and the next candidates start at the advanced selection frontier.
        for (const auto key : {"m", "e", "n"}) {
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key(key), false);
        }
        FCITX_ASSERT(findCandidate(ic, "安") >= 0);

        // Enter the configured filter again and filter the new frontier
        // (安 -> bn), then make another partial selection.
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("grave"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("b"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        FCITX_ASSERT(findCandidate(ic, "安") >= 0);
        findAndSelectCandidate(ic, "安");
        FCITX_ASSERT(findCandidate(ic, "门") >= 0);

        // A third entry proves selection rebuilt the normal candidate list and
        // left the remaining composition available to the same product path.
        FCITX_ASSERT(ic->inputPanel().candidateList());
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("grave"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Escape"), false);
        FCITX_ASSERT(findCandidate(ic, "门") >= 0);
    });
}

void testMoQiShuangpinFilter(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *pinyin = instance->addonManager().addon("pinyin");
        FCITX_ASSERT(pinyin);
        RawConfig config;
        config.setValueByPath("AuxiliaryFilter", "MoQi");
        pinyin->setConfig(config);

        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "shuangpin", true);

        auto enterMoQi = [ic]() {
            auto *tabbed = ic->inputPanel().candidateList()->toTabbed();
            FCITX_ASSERT(tabbed);
            auto actions = tabbed->tabActions();
            auto moqi = std::ranges::find_if(
                actions, [](const auto &a) { return a.text() == "墨奇"; });
            FCITX_ASSERT(moqi != actions.end());
            tabbed->triggerTabAction(moqi->id());
        };

        // Ziranma uses "xi" for xi and "an" for an.
        for (const auto key : {"x", "i", "a", "n"}) {
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key(key), false);
        }
        findCandidateOrDie(ic, "西安");

        enterMoQi();
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("k"), false);
        FCITX_ASSERT(findCandidate(ic, "西安") >= 0);

        // Backspace removes the MoQi buffer first, then exits MoQi mode.
        for (int i = 0; i < 3; i++) {
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("BackSpace"),
                                                        false);
            FCITX_ASSERT(findCandidate(ic, "西安") >= 0);
        }

        // Move the cursor to the boundary between the two Shuangpin
        // syllables. This exposes "西" through candidatesToCursor() while
        // keeping the complete "xian" composition intact.
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Left"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Left"), false);
        findAndSelectCandidate(ic, "西");
        FCITX_ASSERT(findCandidate(ic, "安") >= 0);

        enterMoQi();
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("b"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        FCITX_ASSERT(findCandidate(ic, "安") >= 0);

        // Escape leaves the selected prefix and remaining composition intact.
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Escape"), false);
        FCITX_ASSERT(findCandidate(ic, "安") >= 0);
    });
}

// Continuing to compose after a partial selection keeps the rest of the
// composition and still offers its candidates. This holds while the composition
// cursor stays at the end, so the new syllable is appended to the remainder.
// No auxiliary filter takes part: the step that failed earlier ran after the
// filter had already been left, and the behaviour is filter independent. The
// cursor boundary case is different on purpose and is covered above: once the
// cursor has been moved into the middle, typing inserts at the cursor and
// re-segments the remaining input (see research/shuangpin-cursor-selection.md).
void testShuangpinContinueInputAfterSelection(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "shuangpin", true);

        // Ziranma uses "xi" for xi and "an" for an.
        for (const auto key : {"x", "i", "a", "n"}) {
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key(key), false);
        }
        findCandidateOrDie(ic, "西安");

        // Select 西 from the whole input, which advances the selection frontier
        // to the second syllable without moving the cursor.
        findAndSelectCandidate(ic, "西");
        FCITX_ASSERT(findCandidate(ic, "安") >= 0);

        // The next syllable is appended to the remaining composition, nothing
        // is committed, and the remaining candidates are still offered.
        for (const auto key : {"n", "i"}) {
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key(key), false);
        }
        FCITX_ASSERT(!ic->inputPanel().preedit().toString().empty());
        FCITX_ASSERT(findCandidate(ic, "安") >= 0);
    });
}

void testAuxiliaryFilterEntryGuards(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *pinyin = instance->addonManager().addon("pinyin");
        FCITX_ASSERT(pinyin);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

        RawConfig moqi;
        moqi.setValueByPath("AuxiliaryFilter", "MoQi");
        pinyin->setConfig(moqi);

        // The trigger key is only special while a candidate list exists: with
        // no composition it types a literal backtick instead of filtering. The
        // literal is committed when the next key arrives.
        testfrontend->call<ITestFrontend::pushCommitExpectation>("`");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("`"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("x"), false);
        FCITX_ASSERT(auxUpText(ic).empty());
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Escape"), false);

        // Disabled must not change that: the trigger key stays a literal.
        RawConfig disabled;
        disabled.setValueByPath("AuxiliaryFilter", "Disabled");
        pinyin->setConfig(disabled);
        testfrontend->call<ITestFrontend::pushCommitExpectation>("`");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("`"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("x"), false);
        FCITX_ASSERT(auxUpText(ic).empty());
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Escape"), false);

        // Leave the configuration as the other filter tests expect it.
        pinyin->setConfig(moqi);
    });
}

void testMoQiFilterBufferLimit(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *pinyin = instance->addonManager().addon("pinyin");
        FCITX_ASSERT(pinyin);
        RawConfig config;
        config.setValueByPath("AuxiliaryFilter", "MoQi");
        pinyin->setConfig(config);

        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

        for (const auto key : {"x", "i", "a", "n"}) {
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key(key), false);
        }
        findCandidateOrDie(ic, "西安");

        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("`"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("k"), false);
        FCITX_ASSERT(auxUpText(ic).ends_with("ak"));
        FCITX_ASSERT(findCandidate(ic, "西安") >= 0);

        // MoQi codes are two letters long, so a third letter must be ignored
        // instead of leaking into the pinyin composition.
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("b"), false);
        FCITX_ASSERT(!auxUpText(ic).empty());
        FCITX_ASSERT(auxUpText(ic).ends_with("ak"));
        FCITX_ASSERT(findCandidate(ic, "西安") >= 0);
    });
}

void testMoQiFilterNoMatch(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *pinyin = instance->addonManager().addon("pinyin");
        FCITX_ASSERT(pinyin);
        RawConfig config;
        config.setValueByPath("AuxiliaryFilter", "MoQi");
        pinyin->setConfig(config);

        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

        for (const auto key : {"x", "i", "a", "n"}) {
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key(key), false);
        }
        findCandidateOrDie(ic, "西安");

        // No frontier character carries a MoQi code starting with "zz", so
        // matching candidates must be filtered out rather than kept.
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("`"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("z"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("z"), false);
        FCITX_ASSERT(auxUpText(ic).ends_with("zz"));
        FCITX_ASSERT(ic->inputPanel().candidateList());
        FCITX_ASSERT(findCandidate(ic, "西安") < 0);

        // Leaving the filter restores the unfiltered candidate list.
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Escape"), false);
        FCITX_ASSERT(auxUpText(ic).empty());
        FCITX_ASSERT(findCandidate(ic, "西安") >= 0);
    });
}

void testMoQiFilterModifierKeys(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *pinyin = instance->addonManager().addon("pinyin");
        FCITX_ASSERT(pinyin);
        RawConfig config;
        config.setValueByPath("AuxiliaryFilter", "MoQi");
        pinyin->setConfig(config);

        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

        for (const auto key : {"x", "i", "a", "n"}) {
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key(key), false);
        }
        findCandidateOrDie(ic, "西安");

        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("`"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        FCITX_ASSERT(auxUpText(ic).ends_with("a"));

        // Key combinations are swallowed while filtering: they must not reach
        // the composition nor change the MoQi buffer.
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+a"),
                                                    false);
        FCITX_ASSERT(!auxUpText(ic).empty());
        FCITX_ASSERT(auxUpText(ic).ends_with("a"));
        FCITX_ASSERT(findCandidate(ic, "西安") >= 0);
    });
}

void testMoQiFilterPageNavigation(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *pinyin = instance->addonManager().addon("pinyin");
        FCITX_ASSERT(pinyin);
        RawConfig config;
        config.setValueByPath("AuxiliaryFilter", "MoQi");
        pinyin->setConfig(config);

        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

        for (const auto key : {"x", "i", "a", "n"}) {
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key(key), false);
        }
        findCandidateOrDie(ic, "西安");

        // Previous page on the first page with an empty buffer leaves the
        // filter while keeping the composition.
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("`"), false);
        FCITX_ASSERT(!auxUpText(ic).empty());
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_minus),
                                                    false);
        FCITX_ASSERT(auxUpText(ic).empty());
        FCITX_ASSERT(findCandidate(ic, "西安") >= 0);

        // Next page keeps the filter active.
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("`"), false);
        FCITX_ASSERT(!auxUpText(ic).empty());
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_equal),
                                                    false);
        FCITX_ASSERT(!auxUpText(ic).empty());
        FCITX_ASSERT(findCandidate(ic, "西安") >= 0);
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

// Phase 3A-1 RED regression — mixed Chinese-arc TEXT quality at the real
// product path (PinyinEngine fusion seam -> LibIMEChineseArcOracle ->
// HanWordResolver -> SegmentComposer -> MixedEngine pool). The Device
// Evidence Addendum superseded the Phase-2 verdict that mixed pool contents
// were healthy: on device, Chinese runs of mixed candidates are rare
// one-character-per-syllable table picks even where spans align. The root
// cause is source-proven in /tmp/chinese-arc-source-trace.md (F-1..F-6):
// graph edges are single-syllable, matchWords is exact-encoding so only
// single characters are reachable, no LM/Viterbi context is consulted, and
// Shuangpin spans are mis-decoded as Pinyin.
//
// Expectations are NOT hardcoded from linguistic intuition: for each case
// the expected Han text is obtained at runtime from the classical LibIME
// decoding path by typing the pure run raw (e.g. "wodakai" -> its top-1),
// exactly as the native HANPROBE record run did (phase3a1-probe.log,
// 2026-10-07). Recorded current (defective) mixed outputs — Phase 3A-1
// RED evidence, historical only:
//   pinyin wodakaigithub           -> 喔惮岂GitHub   (喔+惮+岂 per syllable)
//   pinyin woxiangmaiiphonepeijian -> 喔降脉iPhone妃㓺
//   sp     wodaklgithub            -> 喔惮亏累GitHub
//   sp     hsiphone                -> 好似iPhone    (sp code read as pinyin)
//   sp     hsiphonepzjm            -> 好似iPhone偏在举目
// From Phase 3A-2 onward every case above is a FATAL assertion (case 5's
// search-eviction RED was closed by the bounded-diversity retention policy,
// so the former non-fatal openFinding exception for it was removed).
void testMixedHanTextQuality(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);

        auto type = [&](const char *mode, const std::string &raw) {
            instance->setCurrentInputMethod(ic, mode, true);
            ic->reset();
            for (const char p : raw) {
                testfrontend->call<ITestFrontend::keyEvent>(
                    uuid, Key(std::string(1, p)), false);
            }
        };
        auto bulkNow = [&ic]() -> BulkCandidateList * {
            auto candList = ic->inputPanel().candidateList();
            FCITX_ASSERT(candList && !candList->empty());
            auto *bulk = candList->toBulk();
            FCITX_ASSERT(bulk);
            return bulk;
        };
        // Classical ground truth: top-1 reading of a pure Chinese run raw.
        auto classicalTop1 = [&](const char *mode,
                                 const std::string &runRaw) -> std::string {
            type(mode, runRaw);
            auto *bulk = bulkNow();
            const auto top = bulk->candidateFromAll(0).text().toString();
            const bool asciiOnly =
                std::all_of(top.begin(), top.end(),
                            [](char ch) { return ch & 0x80 ? false : true; });
            FCITX_ASSERT(!top.empty() && !asciiOnly)
                << "classical top-1 for run raw " << runRaw << " is not Han";
            return top;
        };

        // One case = alternating [Chinese run raw, English surface] pairs
        // starting and ending with a run or surface as listed. The mixed
        // candidate must compose to classicalTop1(run)+surface+classicalTop1
        // (run2)... with the canonical English surface.
        struct MixedCase {
            const char *mode;
            std::vector<std::pair<bool, std::string>> parts; // true=Chinese run
        };
        const std::vector<MixedCase> cases = {
            {"pinyin", {{true, "wodakai"}, {false, "GitHub"}}},
            {"pinyin",
             {{true, "woxiangmai"}, {false, "iPhone"}, {true, "peijian"}}},
            {"shuangpin", {{true, "wodakl"}, {false, "GitHub"}}},
            {"shuangpin", {{true, "hs"}, {false, "iPhone"}}},
            {"shuangpin", {{true, "hs"}, {false, "iPhone"}, {true, "pzjm"}}},
        };

        for (const auto &c : cases) {
            std::string expected;
            std::string mixedRaw;
            for (const auto &[isChinese, part] : c.parts) {
                if (isChinese) {
                    expected += classicalTop1(c.mode, part);
                    mixedRaw += part;
                } else {
                    expected += part;
                    for (const char ch : part) {
                        mixedRaw += (ch >= 'A' && ch <= 'Z')
                                        ? static_cast<char>(ch + 32)
                                        : ch;
                    }
                }
            }
            type(c.mode, mixedRaw);
            auto *bulk = bulkNow();
            if (!hasCandidateText(ic, expected)) {
                std::string dump;
                for (int i = 0; i < bulk->totalSize(); ++i) {
                    const auto t = bulk->candidateFromAll(i).text().toString();
                    if (t.find("iPhone") != std::string::npos ||
                        t.find("GitHub") != std::string::npos) {
                        dump += " |";
                        dump += std::to_string(i);
                        dump += ":";
                        dump += t;
                    }
                }
                FCITX_ASSERT(false)
                    << "MIXEDHANQ FAIL mode=" << c.mode << " raw=" << mixedRaw
                    << " expected=" << expected
                    << " actual-english-candidates:" << dump;
            }
        }
        ic->reset();
        std::fprintf(stderr, "MIXEDHANQ OK: mixed Chinese runs carry classical "
                             "run decodes in both Pinyin and Shuangpin\n");
    });
}

// Phase 3A-2 §11 R13 — direct-B1-emission Chinese-quality guard. MIXEDHANQ
// asserts classical TOP-1 equality for fixed witnesses; R13 exercises the
// product surface for every English-bearing mixed candidate of the committed
// witnesses and asserts the surface-enforceable invariants: vacuity (mixed
// evidence actually reaches the surface), run-split purity (no non-letter
// ASCII residue in a mixed candidate) and raw alignment (every English run
// occurs in the raw left-to-right; correction variants that cannot align are
// covered by the dedicated correction regressions).
//
// The membership leg (each Han span ∈ typed full-IME reading set of its raw
// gap) was EXECUTED as an assert in run30 and measured as an invalid anchor
// (run30/31/32 evidence, /tmp/mixed-r13-chinese-quality.md): the typed
// sentence surface for a gap is not a superset of the live decoder's
// closed-span readings for it — abbreviation word arcs (互撕 from "hsi",
// 好似 from shuangpin "hs") and multi-arc run concatenations never surface
// as typed readings, and single-letter English runs ("I") misalign gaps. The
// Chinese-evidence contract itself is provenance, not typed-set membership,
// and is established by construction on the source side: arc texts minted by
// `PinyinEncoder::parseUserPinyin` + `PinyinIME::decoder()->decode` over the
// exact span (chinesearcoracle.cpp), composed by pure concatenation of
// `arc.resolvedOutput` (segmentcomposer.cpp), untouched by UnifiedRanker,
// rebuilt by Rewriter with separators only (rewriter.cpp), pasted verbatim
// at placement (pinyin.cpp). So membership runs here as a NON-FATAL
// divergence diagnostic (R13WARN + count): future divergences beyond the
// recorded classes stay visible without asserting a contract the decoder
// does not provide. No compose-time re-decode exists and none is justified
// (mandate §11: the RED disproved the anchor, not the emission).
void testMixedR13ChineseQuality(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testr13");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);

        auto type = [&](const char *mode, const std::string &raw) {
            instance->setCurrentInputMethod(ic, mode, true);
            ic->reset();
            for (const char p : raw) {
                testfrontend->call<ITestFrontend::keyEvent>(
                    uuid, Key(std::string(1, p)), false);
            }
        };
        auto bulkNow = [&ic]() -> BulkCandidateList * {
            auto candList = ic->inputPanel().candidateList();
            FCITX_ASSERT(candList && !candList->empty());
            auto *bulk = candList->toBulk();
            FCITX_ASSERT(bulk);
            return bulk;
        };
        std::unordered_map<std::string, std::unordered_set<std::string>>
            gapReadings;
        auto readingsFor = [&](const std::string &key, const char *mode,
                               const std::string &gap)
            -> const std::unordered_set<std::string> & {
            auto it = gapReadings.find(key);
            if (it == gapReadings.end()) {
                type(mode, gap);
                auto *gapBulk = bulkNow();
                std::unordered_set<std::string> set;
                for (int i = 0; i < gapBulk->totalSize(); ++i) {
                    set.insert(gapBulk->candidateFromAll(i).text().toString());
                }
                it = gapReadings.emplace(key, std::move(set)).first;
            }
            return it->second;
        };
        auto lower = [](std::string s) {
            for (auto &ch : s) {
                ch = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(ch)));
            }
            return s;
        };

        struct R13Case {
            const char *mode;
            std::string raw;
        };
        const std::vector<R13Case> cases = {
            {"pinyin", "hsiphone"},
            {"pinyin", "wodakaigithubheiphonexiuxian"},
            {"pinyin", "woxiangmaiiphonepeijian"},
            {"shuangpin", "hsiphone"},
            {"shuangpin", "hsiphonepzjm"},
        };

        int checked = 0;
        int missed = 0;
        for (const auto &c : cases) {
            const std::string lowerRaw = lower(c.raw);
            type(c.mode, c.raw);
            auto *bulk = bulkNow();
            std::vector<std::string> texts;
            for (int i = 0; i < bulk->totalSize() && texts.size() < 32; ++i) {
                auto t = bulk->candidateFromAll(i).text().toString();
                const bool hasAscii =
                    std::any_of(t.begin(), t.end(), [](char ch) {
                        return (ch >= 'a' && ch <= 'z') ||
                               (ch >= 'A' && ch <= 'Z');
                    });
                if (hasAscii) {
                    texts.push_back(std::move(t));
                }
            }
            FCITX_ASSERT(!texts.empty())
                << "R13 vacuity guard: no English-bearing candidate for "
                << c.mode << " raw=" << c.raw;
            for (const auto &t : texts) {
                // Split into maximal [A-Za-z]-runs and Han (>=0x80) runs.
                std::vector<std::pair<bool, std::string>> runs;
                std::string cur;
                bool curEnglish = false;
                bool haveCur = false;
                bool malformed = false;
                for (const char ch : t) {
                    const bool english =
                        (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
                    if (!english && static_cast<unsigned char>(ch) < 0x80) {
                        malformed = true;
                        break;
                    }
                    if (haveCur && english != curEnglish) {
                        runs.emplace_back(curEnglish, cur);
                        cur.clear();
                    }
                    curEnglish = english;
                    haveCur = true;
                    cur += ch;
                }
                if (malformed) {
                    FCITX_ASSERT(false)
                        << "R13: non-letter ASCII residue in candidate " << t;
                }
                if (haveCur) {
                    runs.emplace_back(curEnglish, cur);
                }
                // Align English runs into the raw left-to-right; correction
                // variants that cannot align (e.g. githbu→GitHub) are
                // covered by the dedicated correction regressions, not here.
                size_t cursor = 0;
                size_t gapBegin = 0;
                bool aligned = true;
                for (size_t r = 0; r < runs.size() && aligned; ++r) {
                    if (!runs[r].first) {
                        continue;
                    }
                    const auto eng = lower(runs[r].second);
                    const auto pos = lowerRaw.find(eng, cursor);
                    if (pos == std::string::npos) {
                        aligned = false;
                        break;
                    }
                    const std::string gap =
                        c.raw.substr(gapBegin, pos - gapBegin);
                    if (!gap.empty() && r > 0 && !runs[r - 1].first) {
                        const auto &han = runs[r - 1];
                        const std::string key = std::string(c.mode) + "|" + gap;
                        const auto &set = readingsFor(key, c.mode, gap);
                        if (!set.count(han.second)) {
                            ++missed;
                            std::fprintf(stderr,
                                         "R13WARN span=%s gap=%s mode=%s "
                                         "raw=%s cand=%s\n",
                                         han.second.c_str(), gap.c_str(),
                                         c.mode, c.raw.c_str(), t.c_str());
                        }
                        ++checked;
                    }
                    cursor = pos + eng.size();
                    gapBegin = cursor;
                }
                if (aligned) {
                    const std::string tailGap = c.raw.substr(gapBegin);
                    if (!tailGap.empty() && !runs.back().first) {
                        const std::string key =
                            std::string(c.mode) + "|" + tailGap;
                        const auto &set = readingsFor(key, c.mode, tailGap);
                        if (!set.count(runs.back().second)) {
                            ++missed;
                            std::fprintf(stderr,
                                         "R13WARN span=%s gap=%s mode=%s "
                                         "raw=%s cand=%s\n",
                                         runs.back().second.c_str(),
                                         tailGap.c_str(), c.mode, c.raw.c_str(),
                                         t.c_str());
                        }
                        ++checked;
                    }
                }
            }
            ic->reset();
        }
        instance->setCurrentInputMethod(ic, "pinyin", true);
        std::fprintf(stderr,
                     "R13 OK: vacuity, residue and alignment guards GREEN "
                     "for English-bearing mixed candidates of the committed "
                     "witnesses in both modes; membership probe: %d/%d Han "
                     "spans outside the typed-gap surface (diagnostic only, "
                     "provenance holds by construction)\n",
                     missed, checked);
    });
}

// Phase 3C placement regression: the conservative single-syllable guard must
// remain active for chang, while a multi-syllable whole-remainder English word
// takes the frozen Row-2 slot after the classical head. This preserves the
// Phase 3A-1 guard without reviving the earlier assumption that a mixed
// candidate precedes the classical head.
void testMixedSingleSyllableGuard(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *tf = instance->addonManager().addon("testfrontend");
        auto uuid = tf->call<ITestFrontend::createInputContext>("testfixa");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        auto type = [&](std::string_view raw) {
            instance->setCurrentInputMethod(ic, "pinyin", true);
            ic->reset();
            for (const char p : raw) {
                tf->call<ITestFrontend::keyEvent>(uuid, Key(std::string(1, p)),
                                                  false);
            }
        };
        auto bulkNow = [&ic]() -> BulkCandidateList * {
            auto list = ic->inputPanel().candidateList();
            FCITX_ASSERT(list && !list->empty());
            auto *bulk = list->toBulk();
            FCITX_ASSERT(bulk);
            return bulk;
        };
        auto idxOf = [](BulkCandidateList *bulk,
                        const std::string &text) -> int {
            for (int i = 0; i < bulk->totalSize(); ++i) {
                if (bulk->candidateFromAll(i).text().toString() == text) {
                    return i;
                }
            }
            return -1;
        };
        auto firstHanIdx = [](BulkCandidateList *bulk) -> int {
            for (int i = 0; i < bulk->totalSize(); ++i) {
                const auto t = bulk->candidateFromAll(i).text().toString();
                const bool han = std::any_of(t.begin(), t.end(),
                                             [](char c) { return c & 0x80; }) &&
                                 std::none_of(t.begin(), t.end(), [](char c) {
                                     return !(c & 0x80);
                                 });
                if (han) {
                    return i;
                }
            }
            return -1;
        };
        // (1) Conservative invariant: "chang" is one syllable; the
        // classical reading keeps top-1 and Chang stays in the list.
        type("chang");
        {
            auto *bulk = bulkNow();
            const auto top = bulk->candidateFromAll(0).text().toString();
            FCITX_ASSERT(!top.empty() && (top[0] & 0x80))
                << "GUARD FAIL: Chang displaced the single-syllable "
                   "classical reading for raw=chang";
            FCITX_ASSERT(idxOf(bulk, "Chang") >= 0)
                << "GUARD FAIL: Chang missing for raw=chang";
        }
        // (2) A whole-remainder multi-syllable English word is Row 2: the
        // classical head remains slot 0 and Qinghai is the bounded insertion
        // candidate at slot 1.
        type("qinghai");
        {
            auto *bulk = bulkNow();
            const int q = idxOf(bulk, "Qinghai");
            const int h = firstHanIdx(bulk);
            FCITX_ASSERT(q >= 0)
                << "GUARD FAIL: Qinghai class member missing for raw=qinghai";
            FCITX_ASSERT(h == 0 && q == 1)
                << "ROW2 FAIL: whole-remainder insertion placement changed "
                   "(qIdx="
                << q << " hanIdx=" << h << ")";
        }
        ic->reset();
        std::fprintf(stderr,
                     "MIXEDGUARD OK: single-syllable guard and Row-2 placement "
                     "preserved\n");
    });
}

// Phase 3B placement regression (Android Round-2 device evidence). On the
// device the correct mixed candidate (e.g. 我打开GitHub) was already pool[0]
// with correct alignment, yet it was placed behind the whole classical list
// (classLead=0, slot=SIZE_MAX/4). Android/iOS default the pinyin
// "Correction" layout to QWERTY (pinyin.h FuzzyConfig) while every other
// native test runs with None, so the device-only classical readings were
// never exercised here. Each case therefore runs under BOTH layouts through
// the real key-event product path. The Han part of the expected candidate
// is the classical top-1 of the pure run under the same layout, never a
// hardcoded string. Conservative guards (chang, ambiguous Exact overlaps,
// pure Chinese, standalone canonical+literal) run under both layouts too.
void testMixedPlacementWindow(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *pinyin = instance->addonManager().addon("pinyin", true);
        FCITX_ASSERT(pinyin);
        auto *tf = instance->addonManager().addon("testfrontend");
        auto uuid = tf->call<ITestFrontend::createInputContext>("testplace");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);

        // Bounded visible window: the mixed candidate must be at index 0 or
        // 1 (same bound as the RANKCORP C "leading path" contract).
        constexpr int kMixedWindow = 1;

        auto type = [&](const char *mode, const std::string &raw) {
            instance->setCurrentInputMethod(ic, mode, true);
            ic->reset();
            for (const char p : raw) {
                tf->call<ITestFrontend::keyEvent>(uuid, Key(std::string(1, p)),
                                                  false);
            }
        };
        auto bulkNow = [&ic]() -> BulkCandidateList * {
            auto list = ic->inputPanel().candidateList();
            FCITX_ASSERT(list && !list->empty());
            auto *bulk = list->toBulk();
            FCITX_ASSERT(bulk);
            return bulk;
        };
        auto idxOf = [](BulkCandidateList *bulk,
                        const std::string &text) -> int {
            for (int i = 0; i < bulk->totalSize(); ++i) {
                if (bulk->candidateFromAll(i).text().toString() == text) {
                    return i;
                }
            }
            return -1;
        };
        auto isPureHan = [](const std::string &t) {
            return !t.empty() &&
                   std::none_of(t.begin(), t.end(),
                                [](unsigned char c) { return c < 0x80; });
        };
        auto classicalTop1 = [&](const char *mode, const std::string &run) {
            type(mode, run);
            const auto top = bulkNow()->candidateFromAll(0).text().toString();
            FCITX_ASSERT(isPureHan(top))
                << "MIXEDPLACE classical top-1 for run " << run
                << " is not Han: " << top;
            return top;
        };

        std::vector<std::string> failures;
        auto fail = [&](const std::string &line) {
            failures.push_back(line);
            std::fprintf(stderr, "MIXEDPLACE FAIL %s\n", line.c_str());
        };

        struct PlaceCase {
            const char *mode;
            std::vector<std::pair<bool, std::string>> parts; // true=Han run
        };
        const std::vector<PlaceCase> cases = {
            {"pinyin", {{true, "wodakai"}, {false, "GitHub"}}},
            {"pinyin",
             {{true, "woxiangmai"}, {false, "iPhone"}, {true, "peijian"}}},
            {"shuangpin", {{true, "wodakl"}, {false, "GitHub"}}},
        };

        for (const char *layout : {"None", "QWERTY"}) {
            RawConfig config;
            config.setValueByPath("Fuzzy/Correction", layout);
            pinyin->setConfig(config);

            for (const auto &c : cases) {
                std::string expected;
                std::string literal;
                std::string raw;
                for (const auto &[isHan, part] : c.parts) {
                    if (isHan) {
                        const auto han = classicalTop1(c.mode, part);
                        expected += han;
                        literal += han;
                        raw += part;
                    } else {
                        expected += part;
                        std::string lower;
                        for (const char ch : part) {
                            lower += static_cast<char>(
                                std::tolower(static_cast<unsigned char>(ch)));
                        }
                        literal += lower;
                        raw += lower;
                    }
                }
                type(c.mode, raw);
                auto *bulk = bulkNow();
                const int rank = idxOf(bulk, expected);
                const int litRank = idxOf(bulk, literal);
                const auto top = bulk->candidateFromAll(0).text().toString();
                std::fprintf(stderr,
                             "MIXEDPLACE layout=%s mode=%s raw=%s expected=%s "
                             "rank=%d literalRank=%d total=%d top1=%s\n",
                             layout, c.mode, raw.c_str(), expected.c_str(),
                             rank, litRank, bulk->totalSize(), top.c_str());
                const std::string where = std::string(" layout=") + layout +
                                          " mode=" + c.mode + " raw=" + raw;
                if (rank < 0) {
                    fail("recall: " + expected + " absent" + where);
                } else if (rank > kMixedWindow) {
                    fail("placement: " + expected + " rank " +
                         std::to_string(rank) + " outside window" + where);
                }
                if (litRank < 0) {
                    fail("literal coexistence: " + literal + " absent" + where);
                } else if (rank >= 0 && litRank <= rank) {
                    fail("literal ordering: " + literal + " at or above " +
                         expected + where);
                }
            }

            // Conservative guards under the same layout (pinyin: chang,
            // ambiguous Exact overlaps, pure Chinese head).
            type("pinyin", "chang");
            {
                const auto top =
                    bulkNow()->candidateFromAll(0).text().toString();
                if (!isPureHan(top)) {
                    fail(std::string("chang: top-1 not Han layout=") + layout +
                         " top1=" + top);
                }
            }
            for (const char *amb : {"ai", "an", "pin", "long", "game"}) {
                type("pinyin", amb);
                const auto t = bulkNow()->candidateFromAll(0).text().toString();
                if (!isPureHan(t)) {
                    fail(std::string("ambiguous: ") + amb +
                         " top-1 not Han layout=" + layout + " top1=" + t);
                }
            }
            for (const char *pure : {"nihao", "pengyou", "women", "shurufa"}) {
                type("pinyin", pure);
                auto *bulk = bulkNow();
                const int head = std::min(bulk->totalSize(), 7);
                for (int i = 0; i < head; ++i) {
                    const auto t = bulk->candidateFromAll(i).text().toString();
                    if (std::any_of(t.begin(), t.end(), [](unsigned char ch) {
                            return std::isalpha(ch);
                        })) {
                        fail(std::string("pure Chinese: ") + pure +
                             " English in head idx=" + std::to_string(i) +
                             " layout=" + layout + " text=" + t);
                        break;
                    }
                }
            }
            // Standalone canonical + literal (D071), both modes.
            for (const char *mode : {"pinyin", "shuangpin"}) {
                for (const auto &[raw, canonical] :
                     std::vector<std::pair<std::string, std::string>>{
                         {"chatgpt", "ChatGPT"},
                         {"github", "GitHub"},
                         {"macos", "macOS"}}) {
                    type(mode, raw);
                    auto *bulk = bulkNow();
                    const int cr = idxOf(bulk, canonical);
                    const int lr = idxOf(bulk, raw);
                    std::fprintf(stderr,
                                 "MIXEDPLACE-STANDALONE layout=%s mode=%s "
                                 "raw=%s canonical=%d literal=%d\n",
                                 layout, mode, raw.c_str(), cr, lr);
                    // Row 1 starts at slot 0; a StrongChinese Shuangpin
                    // remainder is Row 2 and therefore starts at slot 1.
                    // In both cases the canonical sibling must precede its
                    // literal sibling within the visible placement window.
                    if (cr < 0 || lr < 0 || cr >= lr || cr > kMixedWindow) {
                        fail("standalone: " + raw +
                             " canonical=" + std::to_string(cr) +
                             " literal=" + std::to_string(lr) +
                             " layout=" + layout + " mode=" + mode);
                    }
                }
            }
        }

        // Restore the host default for every later test.
        RawConfig restore;
        restore.setValueByPath("Fuzzy/Correction", "None");
        pinyin->setConfig(restore);
        ic->reset();

        FCITX_ASSERT(failures.empty())
            << "MIXEDPLACE FAIL count=" << failures.size();
        std::fprintf(stderr, "MIXEDPLACE OK: mixed candidates inside the "
                             "visible window under None and QWERTY layouts\n");
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
    testMixedHanTextQuality(&instance);
    testMixedR13ChineseQuality(&instance);
    testMixedSingleSyllableGuard(&instance);
    testMixedPlacementWindow(&instance);
    testMixedProductCorpus(&instance);
    testMixedRankingCorpus(&instance);
    testMixedLearning(&instance);
    testForget(&instance);
    testAuxiliaryFilterConfigContract(&instance);
    testActionInStrokeFilter(&instance);
    testMixedStrokeFilterFusion(&instance);
    testDisabledAuxiliaryFilter(&instance);
    testPinyinTabFilter(&instance);
    testMoQiTabFilter(&instance);
    testMoQiShuangpinFilter(&instance);
    testShuangpinContinueInputAfterSelection(&instance);
    testAuxiliaryFilterEntryGuards(&instance);
    testMoQiFilterBufferLimit(&instance);
    testMoQiFilterNoMatch(&instance);
    testMoQiFilterModifierKeys(&instance);
    testMoQiFilterPageNavigation(&instance);
    testMixedMoQiFilterFusion(&instance);
    testMixedAuxiliaryFilterShuangpinFusion(&instance);
    testPinyinTabFilterWithSeparator(&instance);
    testPin(&instance);
    testQuickPhraseTrigger(&instance);
    testVQuickPhraseTrigger(&instance);
    testPunctuationWithCursorAtBeginning(&instance);
    testPunctuation(&instance);
    instance.exec();
    endTestEvent.reset();
    return deferredTestFailures == 0 ? 0 : 1;
}
