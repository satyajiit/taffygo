// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_RECOVERY_OBSERVED_SITE_SKILL_H_
#define TAFFY_TEST_RECOVERY_OBSERVED_SITE_SKILL_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"

namespace taffy::test {

struct ObservedSkillSemanticTarget {
  uint32_t role = 0u;
  uint32_t phrase = 0u;
};

struct ObservedSkillArgument {
  uint32_t parameter = 0u;
  core_api::mojom::SiteSkillArgumentKind kind =
      core_api::mojom::SiteSkillArgumentKind::kFromEarlierStep;
  uint64_t value = 0u;
  uint32_t purpose = 0u;
  std::optional<std::string> public_address;
  std::optional<ObservedSkillSemanticTarget> semantic_target;
};

struct ObservedSkillStep {
  std::string verb;
  std::vector<ObservedSkillArgument> arguments;
  uint32_t postcondition = 0u;
  bool has_fill = false;
  uint32_t fill_purpose = 0u;
};

struct ObservedSiteSkill {
  std::string skill_id;
  std::string origin;
  core_api::mojom::SiteSkillProvenanceView provenance =
      core_api::mojom::SiteSkillProvenanceView::kAuthored;
  core_api::mojom::SiteSkillStatusView status =
      core_api::mojom::SiteSkillStatusView::kDraft;
  uint32_t active_version = 0u;
  uint32_t step_count = 0u;
  uint64_t installed_at_epoch_ms = 0u;
  uint64_t updated_at_epoch_ms = 0u;
  std::optional<std::string> recorded_from_task_id;
  std::vector<ObservedSkillStep> reviewed_steps;
  // Derived only from the actual first navigation step's public operand.
  std::optional<std::string> starting_address;
  // Says every declared step is present, not that its behavior is approved.
  bool review_complete = false;
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_RECOVERY_OBSERVED_SITE_SKILL_H_
