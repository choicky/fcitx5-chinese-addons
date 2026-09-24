/*
 * SPDX-FileCopyrightText: 2026 Choicky
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _PINYINHELPER_MOQI_H_
#define _PINYINHELPER_MOQI_H_

#include <future>
#include <libime/core/datrie.h>
#include <string>
#include <tuple>

namespace fcitx {

class MoQi {
public:
    void loadAsync();
    bool load();
    std::string reverseLookup(const std::string &hanzi) const;

private:
    libime::DATrie<int32_t> reverseDict_;
    bool loaded_ = false;
    bool loadResult_ = false;
    std::future<libime::DATrie<int32_t>> loadFuture_;
};

} // namespace fcitx

#endif // _PINYINHELPER_MOQI_H_
