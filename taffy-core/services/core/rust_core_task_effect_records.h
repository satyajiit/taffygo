// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_CORE_RUST_CORE_TASK_EFFECT_RECORDS_H_
#define TAFFY_SERVICES_CORE_RUST_CORE_TASK_EFFECT_RECORDS_H_

#include <stdint.h>

#include <optional>
#include <string>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/service_bridge.rs.h"

namespace taffy::core_service_internal {

// The checks every task-effect projection makes before it builds anything,
// and the projections large enough to have taken a file each. They are
// declared here rather than kept file-local because the action and model
// effects moved out and took nothing with them: the checks are still the same
// checks.

inline bool ValidIdentifier(const rust::String& value) {
  return !value.empty() &&
         value.size() <= core_service::mojom::kMaxIdentifierBytes;
}

inline bool ValidDigest(const rust::String& value) {
  if (value.size() != 64u) {
    return false;
  }
  for (char byte : value) {
    if (!((byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f'))) {
      return false;
    }
  }
  return true;
}

// A closed enum refuses an unknown value rather than widening to it. The
// ceiling is the LAST member of the enum, never the last one a caller happens
// to handle.
template <typename Enum>
std::optional<Enum> ClosedEnum(uint8_t value, Enum last) {
  if (value > static_cast<uint8_t>(last)) {
    return std::nullopt;
  }
  return static_cast<Enum>(value);
}

std::optional<core_service::mojom::TaskActionEffectPtr> ActionEffect(
    const core_bridge::BridgeTaskEffect& input);

// Exact non-secret executable metadata carried only on a form approval
// effect. A null optional means the form-shaped FFI record was malformed;
// callers decide separately whether the action class is a form class at all.
std::optional<core_service::mojom::TaskExecutableActionPtr>
FormApprovalAction(const core_bridge::BridgeTaskEffect& input);

std::optional<core_service::mojom::TaskModelEffectPtr> ModelEffect(
    const core_bridge::BridgeTaskEffect& input);

inline bool ActionInputMatchesOperation(
    core_service::mojom::TaskActionOperationKind operation,
    core_service::mojom::TaskActionInputKind input) {
  namespace mojom = core_service::mojom;
  switch (operation) {
    case mojom::TaskActionOperationKind::kFormFill:
    case mojom::TaskActionOperationKind::kFormSelect:
      return input == mojom::TaskActionInputKind::kSuppliedValue;
    case mojom::TaskActionOperationKind::kFormToggle:
      return input == mojom::TaskActionInputKind::kToggleState;
    case mojom::TaskActionOperationKind::kNavigate:
    case mojom::TaskActionOperationKind::kSearch:
    case mojom::TaskActionOperationKind::kHistoryBack:
    case mojom::TaskActionOperationKind::kHistoryForward:
    case mojom::TaskActionOperationKind::kReload:
    case mojom::TaskActionOperationKind::kStopLoading:
    case mojom::TaskActionOperationKind::kTabsOpen:
    case mojom::TaskActionOperationKind::kTabsList:
    case mojom::TaskActionOperationKind::kTabsActivate:
    case mojom::TaskActionOperationKind::kTabsClose:
    case mojom::TaskActionOperationKind::kDomQuery:
    case mojom::TaskActionOperationKind::kDomRead:
    case mojom::TaskActionOperationKind::kDomClick:
    case mojom::TaskActionOperationKind::kDomFocus:
    case mojom::TaskActionOperationKind::kDomScroll:
    case mojom::TaskActionOperationKind::kFormInspect:
    case mojom::TaskActionOperationKind::kFormSubmit:
    case mojom::TaskActionOperationKind::kDownloadStart:
    case mojom::TaskActionOperationKind::kDownloadList:
    case mojom::TaskActionOperationKind::kDownloadCancel:
    case mojom::TaskActionOperationKind::kSelectionRead:
    case mojom::TaskActionOperationKind::kImageDescribe:
    case mojom::TaskActionOperationKind::kImageReadText:
    case mojom::TaskActionOperationKind::kVideoInspect:
    case mojom::TaskActionOperationKind::kPdfInspect:
    case mojom::TaskActionOperationKind::kPageScreenshotInspect:
    case mojom::TaskActionOperationKind::kToolJob:
    case mojom::TaskActionOperationKind::kLinkOpen:
    case mojom::TaskActionOperationKind::kLibrarySearch:
    case mojom::TaskActionOperationKind::kLibrarySave:
    case mojom::TaskActionOperationKind::kLibraryRemove:
    case mojom::TaskActionOperationKind::kMemorySearch:
    case mojom::TaskActionOperationKind::kMemorySave:
    case mojom::TaskActionOperationKind::kMemoryUpdate:
    case mojom::TaskActionOperationKind::kMemoryDelete:
    case mojom::TaskActionOperationKind::kHistorySearch:
    case mojom::TaskActionOperationKind::kHistoryRecent:
    case mojom::TaskActionOperationKind::kBookmarksSearch:
    case mojom::TaskActionOperationKind::kBookmarksList:
    case mojom::TaskActionOperationKind::kOpenTabsList:
      return input == mojom::TaskActionInputKind::kNone;
  }
  return false;
}

inline std::optional<core_service::mojom::TaskActionInputPtr> ActionInput(
    const core_bridge::BridgeTaskEffect& input,
    core_service::mojom::TaskActionOperationKind operation) {
  namespace mojom = core_service::mojom;
  const auto kind = ClosedEnum(input.action_input_kind,
                               mojom::TaskActionInputKind::kToggleState);
  if (!kind || !ActionInputMatchesOperation(operation, *kind)) {
    return std::nullopt;
  }
  auto out = mojom::TaskActionInput::New();
  out->kind = *kind;
  switch (*kind) {
    case mojom::TaskActionInputKind::kNone:
      if (input.has_supplied_value ||
          !input.supplied_value_request_id.empty() ||
          input.supplied_value_index != 0u || input.has_toggle_state ||
          input.toggle_checked) {
        return std::nullopt;
      }
      break;
    case mojom::TaskActionInputKind::kSuppliedValue:
      if (!input.has_supplied_value ||
          !ValidIdentifier(input.supplied_value_request_id) ||
          input.supplied_value_index >= mojom::kMaxTaskSuppliedValues ||
          input.has_toggle_state || input.toggle_checked) {
        return std::nullopt;
      }
      out->supplied_value = mojom::TaskSuppliedValuePosition::New(
          input.supplied_value_index,
          std::string(input.supplied_value_request_id));
      break;
    case mojom::TaskActionInputKind::kToggleState:
      if (input.has_supplied_value ||
          !input.supplied_value_request_id.empty() ||
          input.supplied_value_index != 0u || !input.has_toggle_state) {
        return std::nullopt;
      }
      out->toggle_state = mojom::TaskToggleState::New(input.toggle_checked);
      break;
  }
  return std::optional<mojom::TaskActionInputPtr>(std::move(out));
}

}  // namespace taffy::core_service_internal

#endif  // TAFFY_SERVICES_CORE_RUST_CORE_TASK_EFFECT_RECORDS_H_
