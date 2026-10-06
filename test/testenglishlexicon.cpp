/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "english/englisharcoracle.h"
#include "english/englishlexicon.h"

#include <algorithm>
#include <cstdio>
#include <sstream>
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

static std::vector<EnglishLexiconEntry> sampleSeed() {
    // Test-only seed data. Each entry's tier is authored here as a coarse
    // bucket for deterministic assertions.
    return {
        {"i", "I", 9, false, false, false},
        {"a", "a", 9, false, false, false},
        {"am", "am", 8, false, false, false},
        {"app", "app", 6, false, false, false},
        {"apple", "apple", 7, false, false, false},
        {"application", "application", 6, false, false, false},
        {"apply", "apply", 5, false, false, false},
        {"applied", "applied", 4, false, false, false},
        {"iphone", "iPhone", 7, true, false, false},
        {"ipad", "iPad", 6, true, false, false},
        {"http", "HTTP", 5, false, true, false},
        {"https", "HTTPS", 4, false, true, false},
        {"hello", "hello", 8, false, false, false},
        {"help", "help", 7, false, false, false},
        {"helper", "helper", 4, false, false, false},
        {"software", "software", 5, false, false, false},
        {"firebase", "Firebase", 4, true, false, false},
        // literalOnly: obscure surface form accepted but no completion source
        {"emoji", "emoji", 0, false, false, true},
    };
}

static const SegmentationArc *findArc(const std::vector<SegmentationArc> &arcs,
                                      size_t b, size_t e,
                                      CandidateProvenance prov) {
    for (const auto &a : arcs) {
        if (a.rawBegin == b && a.rawEnd == e && a.provenance == prov) {
            return &a;
        }
    }
    return nullptr;
}

int main() {
    EnglishLexicon lex;
    lex.loadFromEntries(sampleSeed());

    // 1. Exact lookup by folded key + isExact on mixed casing.
    check(lex.lookup("apple") != nullptr, "lex: folded lookup apple hit");
    check(lex.lookup("iphone") != nullptr, "lex: folded lookup iphone hit");
    check(lex.lookup("nope") == nullptr, "lex: miss returns null");
    check(lex.isExact("Apple"), "lex: isExact accepts Title case");
    check(lex.isExact("IPHONE"), "lex: isExact accepts UPPER case");
    check(!lex.isExact("iphone!"), "lex: non-alphabetic byte rejects");
    check(!lex.isExact("app!e"), "lex: reject before end of span");

    // 2. Canonicalize returns display form.
    auto cIphone = lex.canonicalize("IPHONE");
    check(cIphone.has_value() && *cIphone == "iPhone",
          "lex: canonicalize UPPER -> display iPhone");
    auto cNope = lex.canonicalize("nope");
    check(!cNope.has_value(), "lex: canonicalize miss returns nullopt");

    // 3. Completions: bounded, exclude the exact key itself.
    auto comps = lex.completions("app", 10);
    bool hasApple = false, hasApplication = false, hasApply = false,
         hasApplied = false, selfIncluded = false;
    for (const auto *p : comps) {
        if (p->key == "apple") {
            hasApple = true;
        }
        if (p->key == "application") {
            hasApplication = true;
        }
        if (p->key == "apply") {
            hasApply = true;
        }
        if (p->key == "applied") {
            hasApplied = true;
        }
        if (p->key == "app") {
            selfIncluded = true;
        }
    }
    check(hasApple && hasApplication && hasApply && hasApplied,
          "lex: completions(app) enumerates expected longer words");
    check(!selfIncluded, "lex: completions excludes the exact key");

    auto comps2 = lex.completions("ap", 2);
    check(comps2.size() <= 2, "lex: completions respects maxResults");

    check(lex.completions("", 10).empty(),
          "lex: empty prefix returns no completions (defensive)");

    // 4. Literal-only entries do not appear in completions.
    auto compsEmoji = lex.completions("emo", 10);
    check(compsEmoji.empty(), "lex: literalOnly excluded from completions");

    // 5. Evidence is monotone in tier and clamped.
    const auto *apple = lex.lookup("apple");     // tier 7
    const auto *applied = lex.lookup("applied"); // tier 4
    check(apple && applied, "lex: apple & applied present");
    check(apple && applied &&
              lex.exactEvidence(*apple) > lex.exactEvidence(*applied),
          "lex: higher tier => higher exact evidence");
    const auto *emoji = lex.lookup("emoji");
    check(emoji && lex.exactEvidence(*emoji) > 0.5F &&
              lex.exactEvidence(*emoji) < 0.65F,
          "lex: literalOnly exact evidence around 0.55");

    // Canonical evidence is slightly below exact (case variant is still valid).
    check(apple && lex.canonicalEvidence(*apple) < lex.exactEvidence(*apple) &&
              lex.canonicalEvidence(*apple) >= lex.exactEvidence(*apple) * 0.9F,
          "lex: canonical evidence just below exact");

    // Completion evidence increases with prefix length (specificity bonus).
    const auto fanout = lex.completionCount("ap");
    const float eShort = lex.completionEvidence(*apple, 2, fanout);
    const float eLong = lex.completionEvidence(*apple, 3, fanout);
    check(eLong > eShort, "lex: completion evidence grows with prefix length");

    // No fixed min prefix: a 1-byte prefix that has completions still
    // produces positive evidence.
    const float eOneByte = lex.completionEvidence(*apple, 1, 100);
    check(eOneByte > 0.0F, "lex: 1-byte prefix still yields positive evidence");

    // 6. TSV loader parses sample resource (comments, blanks, flags).
    std::stringstream ss;
    ss << "# comment line\n"
       << "\n"
       << "cat\t8\tcat\t\n"
       << "cats\t6\tcats\t\n"
       << "kitten\t3\tkitten\t\n"
       << "foobar\t1\tFooBar\tp\n"
       << "badline\n"; // malformed: skipped
    EnglishLexicon lex2;
    lex2.load(ss);
    check(lex2.lookup("cat") != nullptr, "tsv: cat parsed");
    check(lex2.lookup("cats") != nullptr, "tsv: cats parsed");
    check(lex2.lookup("kitten") != nullptr, "tsv: kitten parsed");
    const auto *fb = lex2.lookup("foobar");
    check(fb != nullptr && fb->display == "FooBar" && fb->proper,
          "tsv: proper flag and display parsed");
    check(lex2.lookup("badline") == nullptr, "tsv: malformed line skipped");

    // 7. Arc oracle: exact / canonical / completion provenance.
    EnglishArcOracle oracle(&lex);
    std::string raw = "IwantiPhone"; // single-letter "I" + junk + "iPhone"
    // The oracle is not a word-segmenter — spans must already be word
    // boundaries. Test each span independently.
    // Production-scale contract (closure §6): spans shorter than
    // minSpanLength (default 2) never become English arcs. Single-letter
    // SCOWL "words" are pinyin-initial noise that only fragments the beam,
    // so begin=0 here must yield nothing even though "i" is a lexicon key.
    {
        auto arcs = oracle.arcsAt(raw, 0, raw.size());
        check(std::none_of(arcs.begin(), arcs.end(),
                           [](const SegmentationArc &a) {
                               return a.rawEnd - a.rawBegin < 2;
                           }),
              "oracle: no arc spans fewer than minSpanLength bytes");
        const auto *exactI = findArc(arcs, 0, 1, CandidateProvenance::Exact);
        check(exactI == nullptr,
              "oracle: single-letter 'I' does not surface as an arc");
    }
    {
        // "iPhone" at the real word boundary still resolves through the
        // typed-case Exact rule (2-byte floor does not touch it).
        std::string s = "iPhone";
        auto arcs = oracle.arcsAt(s, 0, s.size());
        const auto *exact = findArc(arcs, 0, 6, CandidateProvenance::Exact);
        check(exact != nullptr && exact->source == SegmentSource::English &&
                  exact->resolvedOutput == "iPhone",
              "oracle: typed-case 'iPhone' surfaces as exact");
        check(exact && exact->boundaryConfidence > 0.9F,
              "oracle: exact arc has strong boundary");
    }
    {
        std::string s = "iphone";
        auto arcs = oracle.arcsAt(s, 0, s.size());
        const auto *canon = findArc(arcs, 0, 6, CandidateProvenance::Canonical);
        check(canon != nullptr,
              "oracle: 'iphone' typed lowercase is canonical");
    }
    {
        std::string s = "iPho"; // prefix of iPhone
        auto arcs = oracle.arcsAt(s, 0, s.size());
        const auto *comp = findArc(arcs, 0, 4, CandidateProvenance::Completion);
        check(comp != nullptr, "oracle: 'iPho' yields a completion arc");
        check(comp && comp->confidence > 0.0F && comp->confidence < 1.0F,
              "oracle: completion evidence in (0,1)");
    }
    {
        std::string s = "zzz"; // no hits
        auto arcs = oracle.arcsAt(s, 0, s.size());
        check(arcs.empty(), "oracle: unknown span yields no arcs");
    }
    {
        std::string s = "hello world"; // space breaks the span
        auto arcs = oracle.arcsAt(s, 0, s.size());
        // Should stop extending past the 5th byte when the space breaks fold.
        bool hasHello = false;
        bool leaked = false;
        for (const auto &a : arcs) {
            if (a.rawBegin == 0 && a.rawEnd == 5 &&
                (a.provenance == CandidateProvenance::Exact ||
                 a.provenance == CandidateProvenance::Canonical)) {
                hasHello = true;
            }
            if (a.rawEnd > 5) {
                leaked = true;
            }
        }
        check(hasHello, "oracle: 'hello' exact before the space");
        check(!leaked, "oracle: scan stops at non-ASCII-alpha byte");
    }

    // 8. Fan-out cap: bounded at maxCompletionsPerSpan per span.
    {
        EnglishArcOracle::Config cfg;
        cfg.maxCompletionsPerSpan = 2;
        EnglishArcOracle capped(&lex, cfg);
        auto arcs = capped.arcsAt("app", 0, 3);
        std::size_t perSpan[3] = {0, 0, 0}; // rawEnd == 1, 2, 3
        for (const auto &a : arcs) {
            if (a.provenance == CandidateProvenance::Completion &&
                a.rawBegin == 0 && a.rawEnd >= 1 && a.rawEnd <= 3) {
                ++perSpan[a.rawEnd - 1];
            }
        }
        check(perSpan[0] <= 2 && perSpan[1] <= 2 && perSpan[2] <= 2,
              "oracle: fan-out cap respected per span");
    }

    if (failures == 0) {
        std::printf("EnglishLexicon/ArcOracle: ALL TESTS PASSED\n");
        return 0;
    }
    std::fprintf(stderr, "EnglishLexicon/ArcOracle: %d failure(s)\n", failures);
    return 1;
}
