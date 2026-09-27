// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Generated from //taffy/build/product-capabilities.json. Do not edit.

#ifndef TAFFY_BROWSER_GENERATED_PRODUCT_CAPABILITIES_H_
#define TAFFY_BROWSER_GENERATED_PRODUCT_CAPABILITIES_H_

#include <optional>
#include <string_view>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy::product_capabilities {

struct Profile final {
  std::string_view name;
  core_service::mojom::TaskMilestone accepted_milestone;
  bool delegated_task_start;
  std::optional<core_service::mojom::TaskMilestone> task_milestone;
};

inline constexpr std::string_view kConfigurationFingerprint = "sha256:f72e05085633c2d0a1ac32ecf34ec37c00ecd27d80668c61cee0276bf35e4723";
inline constexpr Profile kCandidate{
    "candidate", core_service::mojom::TaskMilestone::kM7,
    true,
    std::optional<core_service::mojom::TaskMilestone>{core_service::mojom::TaskMilestone::kM7}};
inline constexpr Profile kDevelopment{
    "development", core_service::mojom::TaskMilestone::kM7,
    true,
    std::optional<core_service::mojom::TaskMilestone>{core_service::mojom::TaskMilestone::kM7}};

// Kept out of line so the caller's validation path is live in both compiled
// profiles; compile-time folding would make one branch an -Wunreachable-code
// error before the other profile could be built.
const Profile& Active();

// The generator owns when the candidate task surface opens. These checks
// assert only the language-local projection shape, so accepting a later
// milestone cannot leave behind an obsolete fail-closed assertion.
static_assert(kCandidate.delegated_task_start ==
              kCandidate.task_milestone.has_value());
static_assert(!kCandidate.task_milestone.has_value() ||
              *kCandidate.task_milestone == kCandidate.accepted_milestone);

}  // namespace taffy::product_capabilities

#endif  // TAFFY_BROWSER_GENERATED_PRODUCT_CAPABILITIES_H_
