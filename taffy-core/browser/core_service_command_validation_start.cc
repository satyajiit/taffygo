// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The start command's bounded shape, named one clause at a time.
//
// It sits in its own file for the reason the rest of this family does: each
// body owns its own rules, and this one owns the most of them. Naming them
// was decision 0151's other half — a start refused here never reached the
// ordered core and reached a person as "Taffy could not read this request",
// which is also what a malformed command, a stale revision and a refused
// consent say.

#include <optional>
#include <string>

#include "taffy/browser/core_service_command_validation_internal.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

}  // namespace

// Named, one clause per rule, because a start refused here is refused before
// the ordered core has seen it and reaches a person as "Taffy could not read
// this request" — the same sentence every other refusal on the path produces.
// The chain is `else if` on purpose: each `Add*` spends the byte budget, so
// the clauses must be evaluated in order and stop at the first that fails,
// exactly as the compounded `||` it replaced did.
std::optional<size_t> StartTaskByteSize(const mojom::StartTaskCommand& start,
                                        size_t ceiling,
                                        const char** refusal) {
  size_t total = 0;
  const char* named = nullptr;
  if (!AddIdentifier(start.task_id, ceiling, &total)) {
    named = "start/task-id";
  } else if (!AddOptionalIdentifier(start.workspace_id, ceiling, &total)) {
    named = "start/workspace-id";
  } else if (!AddIdentifier(start.browser_profile_id, ceiling, &total)) {
    named = "start/browser-profile-id";
  } else if (start.goal.empty()) {
    named = "start/goal-empty";
  } else if (start.goal.size() > mojom::kMaxGoalBytes) {
    named = "start/goal-too-long";
  } else if (!AddBounded(start.goal.size(), ceiling, &total)) {
    named = "start/goal-bytes";
  } else if (!AddOptionalIdentifier(start.provider_route_id, ceiling, &total)) {
    named = "start/provider-route-id";
  } else if (!AddOptionalIdentifier(start.skill_version_id, ceiling, &total)) {
    named = "start/skill-version-id";
  } else if (!AddOptionalIdentifier(start.predecessor_task_id, ceiling,
                                    &total)) {
    named = "start/predecessor-task-id";
  } else if (!AddIdentifier(start.trace_id, ceiling, &total)) {
    named = "start/trace-id";
  } else if (start.tool_allowlist.size() > mojom::kMaxToolAllowlistEntries) {
    named = "start/tool-allowlist-count";
  } else if (start.budgets.size() > mojom::kMaxTaskBudgetEntries) {
    named = "start/budget-count";
  } else if (!start.has_task_deadline &&
             start.task_deadline_monotonic_ms != 0u) {
    named = "start/task-deadline";
  } else if (!start.consent_preview) {
    named = "start/consent-preview";
  }
  if (named) {
    *refusal = named;
    return std::nullopt;
  }
  for (const std::string& tool : start.tool_allowlist) {
    if (!AddIdentifier(tool, ceiling, &total)) {
      *refusal = "start/tool-name";
      return std::nullopt;
    }
  }
  for (const auto& budget : start.budgets) {
    if (!budget) {
      *refusal = "start/budget-entry";
      return std::nullopt;
    }
  }
  for (const auto& source : start.consent_preview->sources) {
    if (!source) {
      named = "start/source-entry";
    } else if (!AddIdentifier(source->source_id, ceiling, &total)) {
      named = "start/source-id";
    } else if (!AddIdentifier(source->tab_id, ceiling, &total)) {
      named = "start/source-tab-id";
    } else if (source->normalized_origin.empty()) {
      named = "start/source-origin-empty";
    } else if (source->normalized_origin.size() >
               mojom::kMaxNormalizedOriginBytes) {
      named = "start/source-origin-too-long";
    } else if (!AddBounded(source->normalized_origin.size(), ceiling, &total)) {
      named = "start/source-origin-bytes";
    } else if (source->canonical_locator) {
      if (source->canonical_locator->empty()) {
        named = "start/source-locator-empty";
      } else if (source->canonical_locator->size() >
                 mojom::kMaxSourceLocatorBytes) {
        named = "start/source-locator-too-long";
      } else if (!AddBounded(source->canonical_locator->size(), ceiling,
                             &total)) {
        named = "start/source-locator-bytes";
      }
    }
    if (named) {
      *refusal = named;
      return std::nullopt;
    }
  }
  if (start.library_refresh) {
    const auto& refresh = *start.library_refresh;
    if (!refresh.sources.empty()) {
      named = "start/refresh-sources";
    } else if (!AddSha256Digest(refresh.preview_id, ceiling, &total)) {
      named = "start/refresh-preview-id";
    } else if (!AddIdentifier(refresh.collection_id, ceiling, &total)) {
      named = "start/refresh-collection-id";
    } else if (refresh.library_revision == 0u) {
      named = "start/refresh-library-revision";
    } else if (refresh.source_workspace_revision == 0u) {
      named = "start/refresh-workspace-revision";
    }
    if (named) {
      *refusal = named;
      return std::nullopt;
    }
  }
  return total;
}

}  // namespace taffy
