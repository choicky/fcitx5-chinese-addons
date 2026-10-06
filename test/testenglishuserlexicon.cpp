/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "english/englishuserlexicon.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fcitx-utils/fdstreambuf.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitx-utils/unixfd.h>
#include <filesystem>
#include <istream>
#include <ostream>
#include <sstream>
#include <string>

using namespace pinyin;

static int failures = 0;
static void check(bool cond, const char *what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

int main() {
    // 1. Empty lexicon: size 0, no lookup hit, save round-trip preserves
    //    only the schema header.
    {
        EnglishUserLexicon lex;
        check(lex.size() == 0, "empty: size 0");
        check(lex.lookup("nope") == nullptr, "empty: lookup miss");
        std::ostringstream os;
        lex.save(os);
        check(os.str() == "#schema=1\n",
              "empty: save emits only the schema header");
    }

    // 2. learn: creates on first confirmation, increments on repeat, updates
    //    lastUsed but never firstSeen.
    {
        EnglishUserLexicon lex;
        lex.learn("hello", "Hello", 1000);
        check(lex.size() == 1, "learn: first creates one entry");
        const auto *e = lex.lookup("hello");
        check(e && e->count == 1 && e->firstSeenSec == 1000 &&
                  e->lastUsedSec == 1000 && e->display == "Hello",
              "learn: fields on first entry");
        lex.learn("hello", "hello", 2000);
        const auto *e2 = lex.lookup("hello");
        check(e2 && e2->count == 2 && e2->firstSeenSec == 1000 &&
                  e2->lastUsedSec == 2000 && e2->display == "hello",
              "learn: repeat increments, refreshes lastUsed, updates display");
    }

    // 3. learn folds case: HELLO, Hello, hello all map to key "hello".
    {
        EnglishUserLexicon lex;
        lex.learn("HELLO", "HELLO", 10);
        lex.learn("Hello", "Hello", 20);
        lex.learn("hello", "hello", 30);
        check(lex.size() == 1, "fold: casing produces one key");
        const auto *e = lex.lookup("hello");
        check(e && e->count == 3, "fold: count aggregated");
    }

    // 4. forget returns true when present, false when not.
    {
        EnglishUserLexicon lex;
        lex.learn("foo", "foo", 1);
        check(lex.forget("foo") == true, "forget: present returns true");
        check(lex.forget("foo") == false, "forget: absent returns false");
        check(lex.size() == 0, "forget: removes entry");
    }

    // 5. reset clears everything.
    {
        EnglishUserLexicon lex;
        lex.learn("a", "a", 1);
        lex.learn("b", "b", 2);
        lex.reset();
        check(lex.size() == 0, "reset: clears state");
    }

    // 6. save/load round trip: state is stable across one emit + parse.
    {
        EnglishUserLexicon lex;
        lex.learn("apple", "Apple", 100);
        lex.learn("apple", "apple", 200); // same key, count now 2
        lex.learn("zebra", "zebra", 300);
        std::ostringstream os;
        lex.save(os);
        std::istringstream is(os.str());
        EnglishUserLexicon lex2;
        check(lex2.load(is) == true, "roundtrip: load returns true");
        check(lex2.size() == 2, "roundtrip: entries preserved");
        const auto *a = lex2.lookup("apple");
        check(a && a->count == 2 && a->display == "apple",
              "roundtrip: apple count/display preserved");
        const auto *z = lex2.lookup("zebra");
        check(z && z->count == 1 && z->firstSeenSec == 300,
              "roundtrip: zebra fields preserved");
    }

    // 7. Unknown schema → safe fallback: load returns false, state remains
    //    empty, no throw. This is critical so Pinyin init never blocks on a
    //    future-schema file (§22).
    {
        std::istringstream is("#schema=999\napple\t5\t1\t2\tapple\n");
        EnglishUserLexicon lex;
        check(lex.load(is) == false, "unknown schema: load returns false");
        check(lex.size() == 0, "unknown schema: state empty (safe fallback)");
    }

    // 8. Missing schema header → refuse to load (data row without version).
    {
        std::istringstream is("apple\t5\t1\t2\tapple\n");
        EnglishUserLexicon lex;
        check(lex.load(is) == false, "missing schema: load returns false");
        check(lex.size() == 0, "missing schema: state empty");
    }

    // 9. Corrupt file: mixed valid + invalid rows still parses valid rows
    //    with the correct schema header; invalid rows skipped.
    {
        std::istringstream is("#schema=1\n"
                              "hello\t3\t100\t200\thello\n"
                              "badline_without_tabs\n"
                              "\t5\t1\t2\tblank-key\n"
                              "ok\t2\t1\t2\tOK\n");
        EnglishUserLexicon lex;
        check(lex.load(is) == true, "corrupt: load returns true (schema ok)");
        check(lex.size() == 2, "corrupt: two valid rows parsed");
        check(lex.lookup("hello") != nullptr, "corrupt: hello present");
        check(lex.lookup("ok") != nullptr, "corrupt: ok present");
    }

    // 10. Duplicate keys collapse to the entry with larger count.
    {
        std::istringstream is("#schema=1\n"
                              "dup\t2\t1\t5\tdup\n"
                              "dup\t7\t1\t9\tdup\n");
        EnglishUserLexicon lex;
        check(lex.load(is) == true, "dup: load ok");
        check(lex.size() == 1, "dup: single entry after collapse");
        const auto *d = lex.lookup("dup");
        check(d && d->count == 7, "dup: kept larger count");
    }

    // 11. Evidence: saturating monotone in count, capped below 1.0 (§19).
    {
        EnglishUserLexicon lex;
        EnglishUserEntry e1;
        e1.key = "x";
        e1.count = 1;
        EnglishUserEntry e10;
        e10.key = "x";
        e10.count = 10;
        EnglishUserEntry e1000;
        e1000.key = "x";
        e1000.count = 1000;
        EnglishUserEntry eBig;
        eBig.key = "x";
        eBig.count = 100000;
        const float v1 = lex.evidence(e1);
        const float v10 = lex.evidence(e10);
        const float v1000 = lex.evidence(e1000);
        const float vBig = lex.evidence(eBig);
        check(v1 < v10 && v10 < v1000, "evidence: monotone in count");
        check(v1000 >= 0.90F && v1000 <= 0.95F, "evidence: near-cap at 1000");
        check(vBig <= 0.95F, "evidence: saturates below 1.0");
    }

    // 12. User arc oracle: emits an arc on a folded hit with CustomPhrase
    //     provenance; stops on non-alphabetic byte.
    {
        EnglishUserLexicon lex;
        lex.learn("foobar", "FooBar", 1);
        EnglishUserArcOracle oracle(&lex);
        std::string raw = "foobar!"; // trailing non-alphabetic
        auto arcs = oracle.arcsAt(raw, 0, raw.size());
        const SegmentationArc *hit = nullptr;
        for (const auto &a : arcs) {
            if (a.rawBegin == 0 && a.rawEnd == 6) {
                hit = &a;
                break;
            }
        }
        check(hit != nullptr, "oracle: user arc on foobar");
        check(hit && hit->source == SegmentSource::English,
              "oracle: source is English");
        check(hit && hit->provenance == CandidateProvenance::CustomPhrase,
              "oracle: provenance is CustomPhrase");
        bool leaked = false;
        for (const auto &a : arcs) {
            if (a.rawEnd > 6) {
                leaked = true;
            }
        }
        check(!leaked, "oracle: does not extend past non-alphabetic byte");
    }

    // 13. User arc oracle: empty lexicon yields no arcs and does not throw.
    {
        EnglishUserLexicon lex;
        EnglishUserArcOracle oracle(&lex);
        auto arcs = oracle.arcsAt("hello", 0, 5);
        check(arcs.empty(), "oracle: empty lexicon yields no arcs");
    }

    // 14. Production persistence seam: StandardPaths::safeSave of the
    //     exact engine file "pinyin/mixed_english_user.tsv" (PkgData),
    //     then reload from disk through the same open path
    //     PinyinEngine::loadMixedResources uses. safeSave writes to a
    //     temporary file and renames atomically, so a killed process can
    //     only ever leave the old complete file or the new complete file;
    //     that crash-safe on-disk state is pinned here. (testpinyin
    //     cannot assert this: fcitx5's setupTestingEnvironment installs a
    //     global StandardPaths that skips user paths by design to keep
    //     tests from writing user data.)
    {
        const auto base = std::filesystem::temp_directory_path() /
                          ("fcitx5-eul-persist-" + std::to_string(::getpid()));
        std::error_code ec;
        std::filesystem::remove_all(base, ec);
        setenv("XDG_DATA_HOME", base.string().c_str(), 1);
        EnglishUserLexicon lex;
        lex.learn("iphone", "iPhone", 500);
        lex.learn("iphone", "iPhone", 600);
        const bool saved = fcitx::StandardPaths::global().safeSave(
            fcitx::StandardPathsType::PkgData, "pinyin/mixed_english_user.tsv",
            [&lex](int fd) {
                fcitx::OFDStreamBuf buffer(fd);
                std::ostream out(&buffer);
                lex.save(out);
                return static_cast<bool>(out);
            });
        check(saved, "safeSave persists user lexicon to disk");
        EnglishUserLexicon reloaded;
        auto file = fcitx::StandardPaths::global().open(
            fcitx::StandardPathsType::PkgData, "pinyin/mixed_english_user.tsv",
            fcitx::StandardPathsMode::User);
        check(file.isValid(), "reload: file readable at the engine path");
        if (file.isValid()) {
            fcitx::IFDStreamBuf buffer(file.fd());
            std::istream in(&buffer);
            check(reloaded.load(in), "reload: load returns true");
            const auto *e = reloaded.lookup("iphone");
            check(e && e->count == 2 && e->display == "iPhone" &&
                      e->firstSeenSec == 500 && e->lastUsedSec == 600,
                  "reload: persisted fields intact across process-level "
                  "save/load");
        }
        std::filesystem::remove_all(base, ec);
    }

    if (failures == 0) {
        std::printf("EnglishUserLexicon: ALL TESTS PASSED\n");
        return 0;
    }
    std::fprintf(stderr, "EnglishUserLexicon: %d failure(s)\n", failures);
    return 1;
}
