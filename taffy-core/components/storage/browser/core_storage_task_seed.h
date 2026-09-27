// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_TASK_SEED_H_
#define TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_TASK_SEED_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace taffy::storage_internal {

inline constexpr size_t kTaskIdSeedBytes = 32u;

// StorageCommitEffect carries a fixed-size Mojo array even when the selected
// operation is not a task append. Those operations use the all-zero value as
// the sole neutral representation: an empty vector cannot cross Mojo, while a
// non-zero value would smuggle task identity into another storage domain.
inline bool IsNeutralTaskIdSeed(const std::vector<uint8_t>& seed) {
  return seed.size() == kTaskIdSeedBytes &&
         std::all_of(seed.begin(), seed.end(),
                     [](uint8_t value) { return value == 0u; });
}

}  // namespace taffy::storage_internal

#endif  // TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_TASK_SEED_H_
