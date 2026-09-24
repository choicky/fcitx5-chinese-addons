/*
 * SPDX-FileCopyrightText: 2026 Choicky
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "moqi.h"

#include <fcitx-utils/fdstreambuf.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitx-utils/stringutils.h>
#include <fcitx-utils/utf8.h>
#include <future>
#include <istream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fcitx {

void MoQi::loadAsync() {
    if (loadFuture_.valid()) {
        return;
    }

    loadFuture_ = std::async(std::launch::async, []() {
        libime::DATrie<int32_t> reverseDict;
        auto file = StandardPaths::global().open(
            StandardPathsType::PkgData, "pinyinhelper/moqima_gb18030.txt");
        if (!file.isValid()) {
            throw std::runtime_error("Failed to open MoQi table");
        }

        IFDStreamBuf buffer(file.fd());
        std::istream in(&buffer);
        std::string buf;
        while (std::getline(in, buf)) {
            if (!utf8::validate(buf)) {
                continue;
            }

            auto line = stringutils::trimView(buf);
            if (line.empty() || line[0] == '#') {
                continue;
            }

            auto firstTab = line.find('\t');
            if (firstTab == std::string_view::npos) {
                continue;
            }
            auto secondTab = line.find('\t', firstTab + 1);
            if (secondTab == std::string_view::npos) {
                continue;
            }

            auto hanzi = line.substr(0, firstTab);
            auto code = line.substr(firstTab + 1, secondTab - firstTab - 1);
            if (utf8::length(hanzi) != 1 || code.size() != 2 ||
                code.find_first_not_of("abcdefghijklmnopqrstuvwxyz") !=
                    std::string_view::npos) {
                continue;
            }

            reverseDict.set(stringutils::concat(hanzi, "|", code), 1);
        }

        reverseDict.shrink_tail();
        return reverseDict;
    });
}

bool MoQi::load() {
    if (loaded_) {
        return loadResult_;
    }
    if (!loadFuture_.valid()) {
        loadAsync();
    }
    try {
        reverseDict_ = loadFuture_.get();
        loadResult_ = true;
    } catch (...) {
        loadResult_ = false;
    }
    loaded_ = true;
    return loadResult_;
}

std::string MoQi::reverseLookup(const std::string &hanzi) const {
    using position_type = decltype(reverseDict_)::position_type;
    position_type pos = 0;
    auto result = reverseDict_.traverse(hanzi, pos);
    if (libime::DATrie<int32_t>::isNoPath(result)) {
        return {};
    }
    result = reverseDict_.traverse("|", pos);
    if (libime::DATrie<int32_t>::isNoPath(result)) {
        return {};
    }

    std::optional<position_type> onlyMatch;
    size_t onlyMatchLength = 0;
    if (reverseDict_.foreach(
            [&onlyMatch, &onlyMatchLength](int32_t, size_t len, uint64_t pos) {
                if (onlyMatch) {
                    return false;
                }
                onlyMatch = pos;
                onlyMatchLength = len;
                return true;
            },
            pos) &&
        onlyMatch) {
        std::string code;
        reverseDict_.suffix(code, onlyMatchLength, *onlyMatch);
        return code;
    }
    return {};
}

} // namespace fcitx
