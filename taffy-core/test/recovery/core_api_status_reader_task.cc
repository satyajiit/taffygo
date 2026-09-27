// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "taffy/test/recovery/core_api_status_reader.h"

namespace taffy::test::internal {

std::optional<ObservedTaskStatus> CoreStatusWireReader::ReadTask() {
  std::optional<std::string> task_id = ReadString(api::kMaxIdentifierBytes);
  const std::optional<uint64_t> revision = ReadU64();
  const std::optional<api::TaskPhase> phase =
      ReadClosedEnum(api::TaskPhase::kIdle, api::TaskPhase::kPartial);
  std::optional<std::string> pending_action_id;
  if (!task_id || task_id->empty() || !revision || !phase ||
      !ReadBoundedU32(api::kMaxProgressBasisPoints)) {
    return std::nullopt;
  }
  const auto status_present = ReadBool();
  if (!status_present) {
    return std::nullopt;
  }
  std::optional<std::string> status_message;
  if (*status_present) {
    status_message = ReadString(api::kMaxMessageKeyBytes);
    if (!status_message) {
      return std::nullopt;
    }
  }
  std::optional<api::CoreFailureCode> failure_code;
  if (!SkipFailure(&failure_code) || !ReadString(api::kMaxTaskGoalBytes) ||
      !ReadClosedEnum(api::TaskTemplateId::kCompareProducts,
                      api::TaskTemplateId::kWebErrand) ||
      !ReadPendingAction(&pending_action_id)) {
    return std::nullopt;
  }

  const std::optional<bool> workspace_present = ReadBool();
  if (!workspace_present) {
    return std::nullopt;
  }
  std::optional<std::string> workspace_id;
  if (*workspace_present) {
    workspace_id = ReadString(api::kMaxIdentifierBytes);
    if (!workspace_id) {
      return std::nullopt;
    }
  }
  if (!SkipOptionalString(api::kMaxUserInputAnswerBytes) ||
      !SkipOptionalString(api::kMaxIdentifierBytes)) {
    return std::nullopt;
  }

  const std::optional<uint32_t> control_count =
      ReadLength(api::kMaxTaskControls);
  if (!control_count) {
    return std::nullopt;
  }
  for (uint32_t index = 0u; index < *control_count; ++index) {
    if (!ReadClosedEnum(api::TaskControlKind::kPause,
                        api::TaskControlKind::kStop)) {
      return std::nullopt;
    }
  }
  const std::optional<uint32_t> artifact_count =
      ReadLength(api::kMaxTaskArtifacts);
  if (!artifact_count) {
    return std::nullopt;
  }
  for (uint32_t index = 0u; index < *artifact_count; ++index) {
    if (!SkipTaskArtifact()) {
      return std::nullopt;
    }
  }
  const std::optional<uint32_t> activity_count =
      ReadLength(api::kMaxTaskActivity);
  if (!activity_count) {
    return std::nullopt;
  }
  for (uint32_t index = 0u; index < *activity_count; ++index) {
    if (!SkipTaskActivity()) {
      return std::nullopt;
    }
  }

  return ObservedTaskStatus{std::move(*task_id),
                            *revision,
                            *phase,
                            std::move(pending_action_id),
                            std::move(workspace_id),
                            *phase == api::TaskPhase::kWaitingForUser &&
                            status_message == "task.waiting_for_handover",
                            failure_code};
}

bool CoreStatusWireReader::ReadPendingAction(
    std::optional<std::string>* action_id) {
  const std::optional<bool> present = ReadBool();
  if (!present) {
    return false;
  }
  if (!*present) {
    action_id->reset();
    return true;
  }
  std::optional<std::string> parsed_action_id =
      ReadString(api::kMaxIdentifierBytes);
  if (!parsed_action_id || parsed_action_id->empty() ||
      !SkipOptionalString(api::kMaxIdentifierBytes) || !ReadU32() ||
      !ReadString(api::kMaxMessageKeyBytes)) {
    return false;
  }
  *action_id = std::move(*parsed_action_id);
  return true;
}

bool CoreStatusWireReader::SkipFailure(
    std::optional<api::CoreFailureCode>* failure_code) {
  const std::optional<bool> present = ReadBool();
  if (!present) {
    return false;
  }
  if (!*present) {
    if (failure_code) {
      failure_code->reset();
    }
    return true;
  }
  const auto code = ReadClosedEnum(api::CoreFailureCode::kCancelled,
                                    api::CoreFailureCode::kPolicyRefused);
  if (!code || !ReadBool() || !SkipOptionalString(api::kMaxMessageKeyBytes)) {
    return false;
  }
  if (failure_code) {
    *failure_code = code;
  }
  return true;
}

bool CoreStatusWireReader::SkipTaskArtifact() {
  return ReadString(api::kMaxIdentifierBytes).has_value() &&
         ReadClosedEnum(api::TaskArtifactKind::kMarkdown,
                        api::TaskArtifactKind::kFrameArchive)
             .has_value() &&
         ReadU64().has_value() && ReadBool().has_value();
}

bool CoreStatusWireReader::SkipTaskActivity() {
  return ReadU64().has_value() &&
         ReadClosedEnum(api::TaskActivityKind::kOpenedPage,
                        api::TaskActivityKind::kBuiltOutput)
             .has_value() &&
         SkipOptionalString(api::kMaxSourceHostBytes) &&
         ReadU32().has_value() && ReadU64().has_value();
}

bool CoreStatusWireReader::SkipOptionalAuth() {
  const std::optional<bool> present = ReadBool();
  return present && (!*present || SkipAuth());
}

bool CoreStatusWireReader::SkipAuth() {
  const std::optional<api::AuthPhase> phase =
      ReadClosedEnum(api::AuthPhase::kInitializing, api::AuthPhase::kFailed);
  const std::optional<bool> account_present = ReadBool();
  if (!phase || !account_present || (*account_present && !SkipAuthAccount())) {
    return false;
  }
  const std::optional<bool> email_present = ReadBool();
  if (!email_present ||
      (*email_present && !ReadString(api::kMaxAuthEmailBytes))) {
    return false;
  }
  const std::optional<bool> failure_present = ReadBool();
  if (!failure_present || (*failure_present && !SkipAuthFailure())) {
    return false;
  }
  const std::optional<uint32_t> method_count = ReadLength(api::kMaxAuthMethods);
  if (!method_count) {
    return false;
  }
  for (uint32_t index = 0u; index < *method_count; ++index) {
    if (!SkipAuthMethod()) {
      return false;
    }
  }
  const std::optional<bool> entitlement_present = ReadBool();
  if (!entitlement_present || (*entitlement_present && !SkipEntitlement())) {
    return false;
  }
  return (*phase == api::AuthPhase::kSignedIn) == *account_present &&
         (*phase == api::AuthPhase::kLinkSent) == *email_present &&
         (*phase == api::AuthPhase::kFailed) == *failure_present;
}

bool CoreStatusWireReader::SkipAuthAccount() {
  return ReadString(api::kMaxIdentifierBytes).has_value() &&
         SkipOptionalString(api::kMaxAuthDisplayNameBytes) &&
         SkipOptionalString(api::kMaxAuthEmailBytes) &&
         ReadClosedEnum(api::AuthProvider::kGoogle,
                        api::AuthProvider::kFacebook)
             .has_value();
}

bool CoreStatusWireReader::SkipAuthFailure() {
  return ReadClosedEnum(api::AuthFailureCode::kNotConfigured,
                        api::AuthFailureCode::kUnknown)
             .has_value() &&
         ReadBool().has_value();
}

bool CoreStatusWireReader::SkipAuthMethod() {
  return ReadClosedEnum(api::AuthProvider::kGoogle,
                        api::AuthProvider::kFacebook)
             .has_value() &&
         ReadClosedEnum(api::AuthMethodAvailability::kAvailable,
                        api::AuthMethodAvailability::kPlatformUnavailable)
             .has_value();
}

bool CoreStatusWireReader::SkipEntitlement() {
  return ReadString(api::kMaxIdentifierBytes).has_value() &&
         ReadU64().has_value() && ReadU64().has_value() &&
         ReadU64().has_value() && ReadU64().has_value();
}

}  // namespace taffy::test::internal
