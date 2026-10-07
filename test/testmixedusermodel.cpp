/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
//
// Phase 3A-2 §17 — persisted-user-model seeded fixture.
//
// The beam-eviction / false-negative shape that temporary B1.1 compensated
// for was observed on device, where the user dictionary has learned real
// multi-syllable words — not on the pristine system dictionary the other
// oracle tests run against. This test builds a real `libime::PinyinIME`
// exactly as `PinyinEngine` does (pinyin.cpp:1163-1181), learns a
// multi-syllable user word the way `PinyinContext::learnWord` does —
// `PinyinDictionary::addWord` at the default cost plus usage bigrams in the
// user history — persists both files with `save(UserDict, Binary)` and
// `model()->save` (the same on-disk shapes `PinyinEngine::save` writes,
// pinyin.cpp:2925-2953), then constructs a SECOND `libime::PinyinIME` that
// loads the system dictionary and those user files from disk — a simulated
// app restart — and plumbs it into the real `LibIMEChineseArcOracle` via
// `setWordDecoder`.
//
// Assertions, in contract order:
//   P0  addWord(UserDict) is visible through lookupWord (seed sanity).
//   P0b/P0c  user.dict and user.history binaries are written to disk.
//   P1  public persistence round trip survives save → restart → load
//       (`lookupWord` on the restarted IME).
//   P1b/P1c  the restarted IME surfaces the persisted word on the classical
//       decode candidate list; the pristine IME does not.
//   O1  the restarted (disk-backed) IME makes the oracle emit an LM word
//       arc over the persisted multi-syllable span. This whole-Chinese-span
//       arc is exactly the crowding source whose eviction shape the removed
//       B1.1-A gate used to suppress; classed retention must now absorb it.
//   O2  negative control: the same oracle wired to an IME WITHOUT the user
//       dictionary load emits no such arc — the arc is persistence-driven,
//       not system-dictionary noise.
//   S1  search-level: with the persisted word arc present, `MixedSegmentation
//       Search` still retains an English-bearing path for a word+English raw.
//   E1  engine-level recall: the full pipeline over "qinghaiiphone" returns
//       a non-empty pool with canonical iPhone recall.
//   E2  engine-level fusion: the pool contains the persisted-word + English
//       fused reading (青亥 adjacent to iPhone), proving the disk-backed
//       user word participates in composed candidates, not just arcs.
//   E3  leading-English raw: the placement-skip contract holds — the mixed
//       pool never surfaces a non-English member. (Full leading-English
//       fusion is bounded by a classical parse resumption property,
//       documented at the assertion, not asserted as contract.)
//   F1  §9 fast path unchanged: a pure-Chinese raw with the persisted word
//       keeps the mixed pool empty (classical decoder owns it).

#include "chinesearcoracle.h"
#include "hanwordresolver.h"
#include "mixedengine.h"
#include "segmentcomposer.h"

#include "english/englisharcoracle.h"
#include "english/englishcorrectionoracle.h"
#include "english/englishlexicon.h"
#include "english/englishuserlexicon.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <libime/core/historybigram.h>
#include <libime/core/languagemodel.h>
#include <libime/core/lattice.h>
#include <libime/core/userlanguagemodel.h>
#include <libime/pinyin/pinyincontext.h>
#include <libime/pinyin/pinyindictionary.h>
#include <libime/pinyin/pinyinencoder.h>
#include <libime/pinyin/pinyinime.h>

using namespace pinyin;

namespace {

int failures = 0;
void check(bool cond, const char *what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// The seeded user word. Deliberately NOT a real dictionary word ("青亥" is
// a novel two-syllable Han string) so its arcs can only originate from the
// persisted user dictionary, which keeps O2 a clean negative control.
// `PinyinDictionary::addWord` takes the quote-separated human full-pinyin
// string directly (the canonical libime full-pinyin surface form, e.g.
// "ni'hao"); run39 proved the `encodeFullPinyin` byte form is rejected.
constexpr const char *kSeedPinyin = "qing'hai";
constexpr const char *kSeedHanzi = "青亥";
constexpr std::string_view kSeedRaw = "qinghai";               // 7 bytes
constexpr std::string_view kWordThenEnglish = "qinghaiiphone"; // 13 bytes
constexpr std::string_view kEnglishThenWord = "iphoneqinghai";

// Build a PinyinIME exactly like PinyinEngine does: fresh composite
// dictionary + the zh_CN system language model file, system dictionary
// loaded from the LibIME package data directory, and — on the restart
// instance — the persisted user dictionary and user history loaded from the
// same file shapes `PinyinEngine::save` writes (pinyin.cpp:2925-2953) and
// the constructor reads back (pinyin.cpp:1185-1214: `dict()->load(UserDict,
// Binary)` + `model()->load(in)`).
std::unique_ptr<libime::PinyinIME> makeIME(const std::string &userDictPath,
                                           const std::string &historyPath) {
    auto ime = std::make_unique<libime::PinyinIME>(
        std::make_unique<libime::PinyinDictionary>(),
        std::make_unique<libime::UserLanguageModel>(
            libime::DefaultLanguageModelResolver::instance()
                .languageModelFileForLanguage("zh_CN")));
#ifdef FUSION_LIBIME_PKGDATADIR
    ime->dict()->load(libime::PinyinDictionary::SystemDict,
                      FUSION_LIBIME_PKGDATADIR "/sc.dict",
                      libime::PinyinDictFormat::Binary);
#else
    check(false,
          "fixture requires FUSION_LIBIME_PKGDATADIR (LibIME package data "
          "directory with sc.dict)");
    return ime;
#endif
    if (!userDictPath.empty()) {
        try {
            ime->dict()->load(libime::PinyinDictionary::UserDict,
                              userDictPath.c_str(),
                              libime::PinyinDictFormat::Binary);
        } catch (const std::exception &e) {
            std::fprintf(stderr, "FAIL: user.dict reload threw: %s\n",
                         e.what());
            ++failures;
        }
    }
    if (!historyPath.empty()) {
        try {
            std::ifstream in(historyPath);
            ime->model()->load(in);
        } catch (const std::exception &e) {
            std::fprintf(stderr, "FAIL: user.history reload threw: %s\n",
                         e.what());
            ++failures;
        }
    }
    return ime;
}

bool emitsSeedWordArc(const LibIMEChineseArcOracle &oracle,
                      std::string_view raw) {
    const auto arcs = oracle.arcsAt(raw, 0, raw.size());
    for (const auto &arc : arcs) {
        if (arc.source == SegmentSource::Chinese &&
            arc.resolvedOutput == kSeedHanzi) {
            return true;
        }
    }
    return false;
}

bool poolHasCandidateContaining(const std::vector<UnifiedCandidate> &pool,
                                const char *needle) {
    for (const auto &cand : pool) {
        if (cand.composedText.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

int main() {
    // ---- Seed + persist (simulates the user learning a word by repeated
    // selection and the application saving on exit / periodically). -------
    // Device mechanism, exactly as `PinyinContext::learnWord` implements it
    // (libime pinyincontext.cpp:396-399): `addWord(UserDict, …)` at the
    // default learn cost, plus usage bigrams in the user language model
    // history, which is what makes a learned word surface in n-best decodes
    // (the cloudpinyin selection path does the same pair at
    // pinyin.cpp:3070-3075). Both sides persist (user.dict + user.history).
    auto imeA = makeIME("", "");
    try {
        imeA->dict()->addWord(libime::PinyinDictionary::UserDict, kSeedPinyin,
                              kSeedHanzi);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "FAIL: addWord threw: %s\n", e.what());
        ++failures;
    }
    check(imeA->dict()
              ->lookupWord(libime::PinyinDictionary::UserDict, kSeedPinyin,
                           kSeedHanzi)
              .has_value(),
          "P0: addWord(UserDict) is visible through lookupWord");

    // Usage history: ten selections of the learned word (the device shape:
    // the user picks it repeatedly, the bigram count grows, the word becomes
    // n-best competitive). Deterministic, public API, same WordWithCode
    // encoding the production selection path uses.
    const auto encodedSeed = libime::PinyinEncoder::encodeFullPinyinWithFlags(
        kSeedPinyin, libime::PinyinFuzzyFlag::VE_UE);
    std::vector<libime::HistoryBigram::WordWithCode> seedSentence = {
        {kSeedHanzi, std::string(encodedSeed.data(), encodedSeed.size())}};
    for (int i = 0; i < 10; ++i) {
        imeA->model()->history().addWithCode(seedSentence);
    }

    std::error_code ec;
    const auto dir = std::filesystem::temp_directory_path(ec);
    if (ec) {
        std::fprintf(stderr, "FAIL: temp_directory_path: %s\n",
                     ec.message().c_str());
        return 1;
    }
    const std::string userDictPath =
        (dir / "fcitx5-mixed-usermodel-user.dict").string();
    try {
        imeA->dict()->save(libime::PinyinDictionary::UserDict,
                           userDictPath.c_str(),
                           libime::PinyinDictFormat::Binary);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "FAIL: user.dict save threw: %s\n", e.what());
        ++failures;
    }
    const auto fileSize = std::filesystem::file_size(userDictPath, ec);
    check(!ec && fileSize > 0, "P0b: user.dict binary written to disk");
    const std::string historyPath =
        (dir / "fcitx5-mixed-usermodel-user.history").string();
    try {
        std::ofstream out(historyPath);
        imeA->model()->save(out);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "FAIL: user.history save threw: %s\n", e.what());
        ++failures;
    }
    const auto historySize = std::filesystem::file_size(historyPath, ec);
    check(!ec && historySize > 0, "P0c: user.history written to disk");
    std::fprintf(stderr, "seed persisted: %s (%llu bytes) + %s (%llu bytes)\n",
                 userDictPath.c_str(),
                 static_cast<unsigned long long>(ec ? 0 : fileSize),
                 historyPath.c_str(),
                 static_cast<unsigned long long>(ec ? 0 : historySize));

    // ---- Simulated restart: a brand-new IME loads system + persisted user
    // dictionary from disk. ----------------------------------------------
    auto imeB = makeIME(userDictPath, historyPath);
    check(imeB->dict()
              ->lookupWord(libime::PinyinDictionary::UserDict, kSeedPinyin,
                           kSeedHanzi)
              .has_value(),
          "P1: persisted user word survives save -> restart -> load");

    LibIMEChineseArcOracle oracleB(ChineseInputMode::Pinyin);
    oracleB.setWordDecoder(imeB.get());

    // O2 negative control IME: identical setup but never loads the user
    // dictionary + history files.
    auto imeC = makeIME("", "");

    // P1b: the persisted model is visible on the classical decode surface —
    // the same candidates the classical decoder produces for the user after
    // a real restart (PinyinContext over the restarted IME). Proven in
    // run44: 青亥 ranks first with the restored dictionary + history and is
    // absent from the pristine IME.
    const auto classicalHasSeed = [](libime::PinyinIME *ime) {
        libime::PinyinContext probe(ime);
        for (char ch : std::string(kSeedRaw)) {
            probe.type(ch);
        }
        for (const auto &r : probe.candidates()) {
            if (r.toString() == kSeedHanzi) {
                return true;
            }
        }
        return false;
    };
    check(classicalHasSeed(imeB.get()),
          "P1b: restarted IME surfaces the persisted word on the classical "
          "decode candidate list");
    check(!classicalHasSeed(imeC.get()),
          "P1c: without the persisted files the classical candidate list "
          "does not contain the seed word");

    oracleB.setRaw(kSeedRaw);
    check(emitsSeedWordArc(oracleB, kSeedRaw),
          "O1: disk-backed user word is emitted as an LM word arc by the "
          "real oracle (persisted multi-syllable arc, no in-memory seeding)");

    // ---- O2 negative control: identical oracle setup on an IME that never
    // loaded the user dictionary file. ------------------------------------
    LibIMEChineseArcOracle oracleC(ChineseInputMode::Pinyin);
    oracleC.setWordDecoder(imeC.get());
    oracleC.setRaw(kSeedRaw);
    check(!emitsSeedWordArc(oracleC, kSeedRaw),
          "O2: without the persisted user dictionary the seed arc is absent "
          "(arc is persistence-driven)");

    // ---- English core (same shape as the Shuangpin pipeline test). ------
    EnglishLexicon lex;
    lex.loadFromEntries({{"iphone", "iPhone", 9, true, false, false}});
    EnglishArcOracle sysEn(&lex);
    EnglishUserLexicon userLex;
    EnglishUserArcOracle userEn(&userLex);
    EnglishCorrectionOracle corrEn(&lex);
    CompositeEnglishArcOracle composite;
    composite.addSource(&sysEn);
    composite.addSource(&userEn);
    composite.addSource(&corrEn);

    // ---- S1: search-level retention with the persisted crowding arc. ----
    {
        oracleB.setRaw(kWordThenEnglish);
        MixedSegmentationSearch search;
        const auto paths = search.search(kWordThenEnglish, oracleB, composite);
        bool englishSurvived = false;
        for (const auto &p : paths) {
            for (const auto &a : p.arcs) {
                if (a.source == SegmentSource::English) {
                    englishSurvived = true;
                }
            }
        }
        check(englishSurvived,
              "S1: an English-bearing path survives the search with the "
              "persisted whole-Chinese-span word arc present (the B1.1-A "
              "crowding shape is absorbed by classed retention)");
    }

    // ---- E1/E2/E3: engine-level recall + fusion on the persisted model. -
    // The resolver object must outlive the ArcResolver lambda (the lambda
    // captures `this`), exactly like the `mixedHanResolver_` member at the
    // production seam.
    MixedEngine engine;
    HanWordResolver hanResolver(imeB->dict());
    {
        oracleB.setRaw(kWordThenEnglish);
        const auto arcResolver = hanResolver.asArcResolver(kWordThenEnglish);
        const auto pool =
            engine.compute(kWordThenEnglish, oracleB, composite, arcResolver);
        check(!pool.empty(),
              "E1: mixed pool non-empty for persisted word + English raw");
        check(poolHasCandidateContaining(pool, "iPhone"),
              "E1: pool recalls the canonical iPhone form on the persisted "
              "user model");
        bool fused = false;
        for (const auto &cand : pool) {
            if (cand.composedText.find(kSeedHanzi) != std::string::npos &&
                cand.composedText.find("iPhone") != std::string::npos) {
                fused = true;
            }
        }
        check(fused,
              "E2: pool contains the fused persisted-word + iPhone reading");
    }
    {
        oracleB.setRaw(kEnglishThenWord);
        const auto arcResolver = hanResolver.asArcResolver(kEnglishThenWord);
        const auto pool =
            engine.compute(kEnglishThenWord, oracleB, composite, arcResolver);
        // Committed placement-skip contract: whatever the classical parse
        // yields, the mixed pool must never surface a non-English member.
        // Full leading-English fusion (青亥+iPhone from "iphoneqinghai") is
        // bounded by a LibIME parse property — the classical parse of the
        // whole raw creates no Chinese graph nodes at [6,13) after the
        // unparseable leading substring (run45), matching the product's
        // recorded non-English dead-slot skip at the placement seam
        // (pinyin.cpp:979-985). That parse property is documented here, not
        // asserted as contract.
        bool englishBearingOnly = true;
        for (const auto &cand : pool) {
            if (!std::any_of(cand.composedText.begin(), cand.composedText.end(),
                             [](unsigned char ch) {
                                 return std::isalpha(ch) && ch < 0x80;
                             })) {
                englishBearingOnly = false;
            }
        }
        check(englishBearingOnly,
              "E3: leading-English raw never surfaces a non-English pool "
              "member (placement-skip contract holds on the persisted model)");
    }

    // ---- F1: §9 fast path is unchanged by the persisted model. ----------
    {
        oracleB.setRaw(kSeedRaw);
        const auto arcResolver = hanResolver.asArcResolver(kSeedRaw);
        const auto pool =
            engine.compute(kSeedRaw, oracleB, composite, arcResolver);
        check(pool.empty(),
              "F1: pure-Chinese raw with persisted word keeps the §9 "
              "fast-path empty pool");
    }

    std::filesystem::remove(userDictPath, ec);
    std::filesystem::remove(historyPath, ec);

    if (failures > 0) {
        std::fprintf(stderr, "testmixedusermodel: %d failure(s)\n", failures);
        return 1;
    }
    std::fprintf(stderr,
                 "testmixedusermodel: OK (persisted user model drives oracle "
                 "word arcs; classed retention + engine recall hold on the "
                 "disk-backed user dictionary)\n");
    return 0;
}
