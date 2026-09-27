// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_api/core_api_command_factory_workflow_test_support.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace taffy {

FixedWorkflowEntropy::FixedWorkflowEntropy() = default;

FixedWorkflowEntropy::~FixedWorkflowEntropy() = default;

std::string FixedWorkflowEntropy::NewOpaqueId(std::string_view domain) {
  return std::string(domain) + "-fixed";
}

std::array<uint8_t, 32> FixedWorkflowEntropy::NewTaskSeed() {
  std::array<uint8_t, 32> seed{};
  seed.fill(0x5a);
  return seed;
}

}  // namespace taffy
