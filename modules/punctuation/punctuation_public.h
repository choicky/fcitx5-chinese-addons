/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _PUNCTUATION_PUNCTUATION_PUBLIC_H_
#define _PUNCTUATION_PUNCTUATION_PUBLIC_H_

#include <fcitx-config/rawconfig.h>
#include <fcitx/addoninstance.h>
#include <string>
namespace fcitx {
class InputContext;

/**
 * Outcome of Punctuation::setSubConfigChecked.
 *
 * Every value other than Ok means the canonical punc.mb file and the live map still hold
 * the table they held before the call, so the same submission can be corrected or retried.
 */
enum class PunctuationMapSaveResult {
    /// The complete replacement was persisted and is now the live map.
    Ok,
    /// At least one submitted row cannot be stored, so nothing was written or published.
    Invalid,
    /// Fcitx's atomic save did not complete, so nothing was published.
    SaveFailed,
    /// The path does not name a loaded punctuation profile.
    UnknownLanguage,
};
} // namespace fcitx

FCITX_ADDON_DECLARE_FUNCTION(
    Punctuation, getPunctuation,
    const std::pair<std::string, std::string> &(const std::string &language,
                                                uint32_t unicode));
FCITX_ADDON_DECLARE_FUNCTION(Punctuation, cancelLast,
                             const std::string &(const std::string &language,
                                                 InputContext *ic));
FCITX_ADDON_DECLARE_FUNCTION(Punctuation, pushPunctuation,
                             const std::string &(const std::string &language,
                                                 InputContext *ic,
                                                 uint32_t unicode));
FCITX_ADDON_DECLARE_FUNCTION(
    Punctuation, pushPunctuationV2,
    std::pair<std::string, std::string>(const std::string &language,
                                        InputContext *ic, uint32_t unicode));

FCITX_ADDON_DECLARE_FUNCTION(
    Punctuation, getPunctuationCandidates,
    std::vector<std::string>(const std::string &language, uint32_t unicode));

/**
 * Checked counterpart of AddonInstance::setSubConfig for a punctuation map.
 *
 * `path` names a loaded profile the way getSubConfig does: "punctuationmap/<language>",
 * i.e. "punctuationmap/zh_CN" for the zh_CN table. `config` is the complete replacement
 * map, in the shape getSubConfig returns it — an "Entries" list of rows carrying "Key",
 * "Mapping" and "AltMapping". It replaces the whole table rather than merging into it;
 * submitted order is the candidate order, repeated keys stay separate candidates, and an
 * empty "Entries" list is a valid, deliberately empty table.
 *
 * The addon parses the submission once, builds the complete replacement — rows, lookup map
 * and serialized bytes — beside the live profile, and refuses it as a whole if any row
 * cannot be stored. Only an accepted map is written, once, through the addon's own
 * serializer and Fcitx's atomic save; only after that write succeeds does the prepared
 * profile become the live map. Nothing is written for a rejected map, and nothing is
 * published for a failed write.
 *
 * Every result other than Ok leaves the canonical punc.mb file and the live map at the
 * table they held before the call, so the same submission can be corrected or retried.
 * The generic AddonInstance::setSubConfig callers keep its fire-and-forget void contract.
 */
FCITX_ADDON_DECLARE_FUNCTION(
    Punctuation, setSubConfigChecked,
    fcitx::PunctuationMapSaveResult(const std::string &path,
                                    const fcitx::RawConfig &config));

#endif // _PUNCTUATION_PUNCTUATION_PUBLIC_H_
