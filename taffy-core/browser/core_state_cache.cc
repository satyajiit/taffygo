// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_state_cache.h"

#include <limits>
#include <utility>

namespace taffy {

CoreStateCache::CoreStateCache() = default;
CoreStateCache::~CoreStateCache() = default;

bool CoreStateCache::Accept(
    core_service::mojom::CoreStateUpdatePtr update,
    uint64_t expected_generation) {
  if (!update || update->service_generation != expected_generation ||
      !IsNextSequence(update->sequence)) {
    return false;
  }
  last_sequence_ = update->sequence;
  latest_ = std::move(update);
  return true;
}

bool CoreStateCache::IsNextSequence(uint64_t sequence) const {
  return last_sequence_ != std::numeric_limits<uint64_t>::max() &&
         sequence == last_sequence_ + 1u;
}

void CoreStateCache::Reset() {
  latest_.reset();
  last_sequence_ = 0;
}

}  // namespace taffy
