// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_FILTERING_CORE_SCOPED_ADBLOCK_STRING_H_
#define TAFFY_COMPONENTS_FILTERING_CORE_SCOPED_ADBLOCK_STRING_H_

#include <memory>

#include "taffy/third_party/adblock-rust/c_abi/include/taffy_adblock.h"

namespace taffy::filtering {

struct AdblockStringDeleter {
  void operator()(char* ptr) const { taffy_adblock_string_free(ptr); }
};

using ScopedAdblockString = std::unique_ptr<char, AdblockStringDeleter>;

}  // namespace taffy::filtering

#endif  // TAFFY_COMPONENTS_FILTERING_CORE_SCOPED_ADBLOCK_STRING_H_
