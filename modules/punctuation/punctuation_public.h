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
 * The map is validated as a complete replacement, persisted once through the addon's own
 * serializer and Fcitx's atomic save, and only then published as the live map. The generic
 * setSubConfig keeps its fire-and-forget contract.
 */
FCITX_ADDON_DECLARE_FUNCTION(
    Punctuation, setSubConfigChecked,
    fcitx::PunctuationMapSaveResult(const std::string &path,
                                    const fcitx::RawConfig &config));

#endif // _PUNCTUATION_PUNCTUATION_PUBLIC_H_
