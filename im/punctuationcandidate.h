/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _CHINESE_ADDONS_PUNCTUATIONCANDIDATE_H_
#define _CHINESE_ADDONS_PUNCTUATIONCANDIDATE_H_

#include "../modules/punctuation/punctuation_public.h"
#include <cstddef>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/inputcontext.h>

namespace fcitx {

inline void commitPunctuationCandidate(InputContext *inputContext,
                                       const PunctuationCandidatePair &pair,
                                       bool pairedTogether) {
    const auto &mapping = pair.first;
    const auto &altMapping = pair.second;
    if (!pairedTogether || altMapping.empty()) {
        inputContext->commitString(mapping);
        return;
    }
    const auto paired = mapping + altMapping;
    if (inputContext->capabilityFlags().test(
            CapabilityFlag::CommitStringWithCursor)) {
        if (const auto length = utf8::lengthValidated(mapping);
            length != 0 && length != utf8::INVALID_LENGTH) {
            inputContext->commitStringWithCursor(paired, length);
        } else {
            inputContext->commitString(paired);
        }
    } else {
        inputContext->commitString(paired);
        if (const auto length = utf8::lengthValidated(altMapping);
            length != 0 && length != utf8::INVALID_LENGTH) {
            for (size_t i = 0; i < length; i++) {
                inputContext->forwardKey(Key(FcitxKey_Left));
            }
        }
    }
}

} // namespace fcitx

#endif // _CHINESE_ADDONS_PUNCTUATIONCANDIDATE_H_
