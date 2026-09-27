// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/filtering/renderer/class_id_tokens.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/flat_set.h"

namespace taffy::filtering {
namespace {

bool IsHtmlWhitespace(char byte) {
  return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r' ||
         byte == '\f';
}

}  // namespace

std::vector<std::string> SplitClassAttribute(std::string_view value) {
  std::vector<std::string> tokens;
  size_t index = 0;
  while (index < value.size()) {
    while (index < value.size() && IsHtmlWhitespace(value[index])) {
      ++index;
    }
    const size_t start = index;
    while (index < value.size() && !IsHtmlWhitespace(value[index])) {
      ++index;
    }
    if (start < index) {
      tokens.emplace_back(value.substr(start, index - start));
    }
  }
  return tokens;
}

std::vector<std::string> TakeUnseen(base::flat_set<std::string>& seen,
                                    std::vector<std::string> tokens) {
  std::vector<std::string> unseen;
  for (std::string& token : tokens) {
    if (token.empty()) {
      continue;
    }
    const auto [it, inserted] = seen.insert(token);
    (void)it;
    if (inserted) {
      unseen.push_back(std::move(token));
    }
  }
  return unseen;
}

}  // namespace taffy::filtering
