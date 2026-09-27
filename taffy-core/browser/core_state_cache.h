// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_STATE_CACHE_H_
#define TAFFY_BROWSER_CORE_STATE_CACHE_H_

#include <stdint.h>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

// One generation-bound immutable state cache. It accepts only strictly newer
// updates and retains the complete generated payload for observers that attach
// after the utility process published it.
class CoreStateCache final {
 public:
  CoreStateCache();
  CoreStateCache(const CoreStateCache&) = delete;
  CoreStateCache& operator=(const CoreStateCache&) = delete;
  ~CoreStateCache();

  bool Accept(core_service::mojom::CoreStateUpdatePtr update,
              uint64_t expected_generation);
  void Reset();
  bool IsNextSequence(uint64_t sequence) const;

  const core_service::mojom::CoreStateUpdate* latest() const {
    return latest_.get();
  }
  uint64_t last_sequence() const { return last_sequence_; }

 private:
  core_service::mojom::CoreStateUpdatePtr latest_;
  uint64_t last_sequence_ = 0;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_STATE_CACHE_H_
