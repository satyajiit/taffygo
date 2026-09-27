// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "taffy/browser/core_api/core_api_command_factory.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;
namespace service = core_service::mojom;

constexpr uint64_t kApprovalLifetimeMs = 30'000;

bool IsIdentifier(const std::string& value) {
  return !value.empty() && value.size() <= api::kMaxIdentifierBytes;
}

// Four-to-eight digits, ignoring spaces and hyphens: the shape of an OTP.
// Those stay on the page; the factory refuses them rather than forwarding.
bool IsCredentialShapedAnswer(std::string_view answer) {
  size_t digits = 0;
  for (unsigned char character : answer) {
    if (character >= '0' && character <= '9') {
      ++digits;
    } else if (character != ' ' && character != '-') {
      return false;
    }
  }
  return digits >= 4 && digits <= 8;
}

bool IsAdmissiblePersonAnswer(const std::string& answer) {
  return !answer.empty() && answer.size() <= api::kMaxUserInputAnswerBytes &&
         !IsCredentialShapedAnswer(answer);
}

bool IsSha256(const std::string& value) {
  return value.size() == 64u &&
         std::all_of(value.begin(), value.end(), [](char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

std::optional<service::PlatformPermission> ProjectPermission(
    api::PlatformPermission permission) {
  switch (permission) {
    case api::PlatformPermission::kNotifications:
      return service::PlatformPermission::kNotifications;
    case api::PlatformPermission::kCamera:
      return service::PlatformPermission::kCamera;
    case api::PlatformPermission::kMicrophone:
      return service::PlatformPermission::kMicrophone;
    case api::PlatformPermission::kLocation:
      return service::PlatformPermission::kLocation;
    case api::PlatformPermission::kReadUserFile:
    case api::PlatformPermission::kWriteUserFile:
      return std::nullopt;
  }
  return std::nullopt;
}

std::optional<service::PermissionDecision> ProjectPermissionDecision(
    api::PermissionDecision decision) {
  switch (decision) {
    case api::PermissionDecision::kGranted:
      return service::PermissionDecision::kGranted;
    case api::PermissionDecision::kDenied:
      return service::PermissionDecision::kDenied;
    case api::PermissionDecision::kUnavailable:
      return service::PermissionDecision::kUnavailable;
  }
  return std::nullopt;
}

}  // namespace

std::optional<ProjectedCoreCommand> CoreApiCommandFactory::BuildCancelTask(
    std::string task_id,
    uint64_t task_revision,
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  if (!IsIdentifier(task_id) || task_revision == 0u) {
    return std::nullopt;
  }
  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(task_revision, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kCancelTask;
  core_command->cancel_task = api::CancelTaskBody::New(task_id);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }

  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind = service::CoreServiceCommandKind::kCancelTask;
  service_command->cancel_task = service::CancelTaskCommand::New(
      std::move(task_id), service::CancelReason::kUser,
      entropy_source_->NewOpaqueId("trace"));
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

std::optional<ProjectedCoreCommand> CoreApiCommandFactory::BuildPauseTask(
    std::string task_id,
    uint64_t task_revision,
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  if (!IsIdentifier(task_id) || task_revision == 0u) {
    return std::nullopt;
  }
  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(task_revision, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kPauseTask;
  core_command->pause_task = api::PauseTaskBody::New(task_id);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }
  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind = service::CoreServiceCommandKind::kPauseTask;
  service_command->pause_task = service::PauseTaskCommand::New(
      std::move(task_id), entropy_source_->NewOpaqueId("trace"));
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

std::optional<ProjectedCoreCommand> CoreApiCommandFactory::BuildResumeTask(
    std::string task_id,
    uint64_t task_revision,
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  if (!IsIdentifier(task_id) || task_revision == 0u) {
    return std::nullopt;
  }
  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(task_revision, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kResumeTask;
  core_command->resume_task = api::ResumeTaskBody::New(task_id);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }
  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind = service::CoreServiceCommandKind::kResumeTask;
  service_command->resume_task = service::ResumeTaskCommand::New(
      std::move(task_id), entropy_source_->NewOpaqueId("trace"));
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

std::optional<ProjectedCoreCommand> CoreApiCommandFactory::BuildTakeOver(
    std::string task_id,
    uint64_t task_revision,
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  if (!IsIdentifier(task_id) || task_revision == 0u) {
    return std::nullopt;
  }
  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(task_revision, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kTakeOver;
  core_command->take_over = api::TakeOverBody::New(task_id);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }
  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind = service::CoreServiceCommandKind::kTakeOver;
  service_command->take_over = service::TakeOverCommand::New(
      std::move(task_id), entropy_source_->NewOpaqueId("trace"));
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

std::optional<ProjectedCoreCommand>
CoreApiCommandFactory::BuildCompleteHandover(std::string task_id,
                                             std::string handover_id,
                                             std::string lease_before,
                                             std::string resumed_with,
                                             uint32_t person_input,
                                             uint64_t task_revision,
                                             uint64_t service_generation,
                                             uint64_t now_monotonic_ms) {
  if (!IsIdentifier(task_id) || !IsIdentifier(handover_id) ||
      !IsIdentifier(lease_before) || !IsIdentifier(resumed_with) ||
      task_revision == 0u) {
    return std::nullopt;
  }
  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(task_revision, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kCompleteHandover;
  core_command->complete_handover = api::CompleteHandoverBody::New(task_id);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }

  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind = service::CoreServiceCommandKind::kCompleteHandover;
  service_command->complete_handover = service::CompleteHandoverCommand::New(
      std::move(task_id), std::move(handover_id), std::move(lease_before),
      std::move(resumed_with), person_input,
      entropy_source_->NewOpaqueId("trace"));
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

std::optional<ProjectedCoreCommand> CoreApiCommandFactory::BuildSupplyUserInput(
    std::string task_id,
    std::string answer,
    uint64_t task_revision,
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  if (!IsIdentifier(task_id) || !IsAdmissiblePersonAnswer(answer) ||
      task_revision == 0u) {
    return std::nullopt;
  }
  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(task_revision, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kSupplyUserInput;
  core_command->supply_user_input =
      api::SupplyUserInputBody::New(task_id, answer);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }

  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind = service::CoreServiceCommandKind::kSupplyUserInput;
  service_command->supply_user_input = service::SupplyUserInputCommand::New(
      std::move(task_id), std::move(answer),
      entropy_source_->NewOpaqueId("trace"));
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

// A follow-up is bounded exactly as an answer is (decision 0137): the same
// person, the same trusted chrome, the same refusal of a credential-shaped
// value before it crosses. Whether the task can take a question is the core's
// to decide from the state it holds; this only shapes the crossing.
std::optional<ProjectedCoreCommand> CoreApiCommandFactory::BuildFollowUp(
    std::string task_id,
    std::string question,
    uint64_t task_revision,
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  if (!IsIdentifier(task_id) || !IsAdmissiblePersonAnswer(question) ||
      task_revision == 0u) {
    return std::nullopt;
  }
  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(task_revision, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kFollowUp;
  core_command->follow_up = api::FollowUpBody::New(task_id, question);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }

  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind = service::CoreServiceCommandKind::kFollowUp;
  service_command->follow_up = service::FollowUpCommand::New(
      std::move(task_id), std::move(question),
      entropy_source_->NewOpaqueId("trace"));
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

std::optional<ProjectedCoreCommand> CoreApiCommandFactory::BuildApproveAction(
    std::string task_id,
    std::string action_id,
    std::string proposal_digest,
    uint64_t task_revision,
    uint64_t service_generation,
    uint64_t now_monotonic_ms,
    uint64_t now_utc_ms,
    std::string browser_session_id) {
  if (!IsIdentifier(task_id) || !IsIdentifier(action_id) ||
      !IsSha256(proposal_digest) || task_revision == 0u || now_utc_ms == 0u ||
      !IsIdentifier(browser_session_id)) {
    return std::nullopt;
  }
  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(task_revision, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kApproveAction;
  core_command->approve_action =
      api::ApproveActionBody::New(task_id, action_id);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }

  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind = service::CoreServiceCommandKind::kUserDecision;
  service_command->user_decision = service::UserDecisionCommand::New(
      std::move(task_id), std::move(action_id),
      service::UserDecisionKind::kAccept, std::move(proposal_digest),
      entropy_source_->NewOpaqueId("trace"),
      entropy_source_->NewOpaqueId("approval-receipt"),
      service_command->operation->deadline_monotonic_ms,
      now_utc_ms > std::numeric_limits<uint64_t>::max() - kApprovalLifetimeMs
          ? std::numeric_limits<uint64_t>::max()
          : now_utc_ms + kApprovalLifetimeMs,
      std::move(browser_session_id));
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

std::optional<ProjectedCoreCommand>
CoreApiCommandFactory::BuildPermissionResult(std::string task_id,
                                             std::string request_id,
                                             api::PlatformPermission permission,
                                             api::PermissionDecision decision,
                                             uint64_t task_revision,
                                             uint64_t service_generation,
                                             uint64_t now_monotonic_ms) {
  const auto service_permission = ProjectPermission(permission);
  const auto service_decision = ProjectPermissionDecision(decision);
  if (!IsIdentifier(task_id) || !IsIdentifier(request_id) ||
      !service_permission || !service_decision || task_revision == 0u) {
    return std::nullopt;
  }
  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(task_revision, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kPermissionResult;
  core_command->permission_result =
      api::PermissionResultBody::New(request_id, permission, decision);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }

  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind = service::CoreServiceCommandKind::kPermissionResult;
  service_command->permission_result = service::PermissionResultCommand::New(
      std::move(task_id), std::move(request_id), *service_permission,
      *service_decision, entropy_source_->NewOpaqueId("trace"));
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

}  // namespace taffy
