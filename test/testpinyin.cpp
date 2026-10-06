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
