// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// One composer completion request, and its withdrawal.
//
// It is the only builder in this directory that is not about a task, an
// account, a workspace, an artifact or a provider, and it answers with a plain
// projected command rather than with a `ProviderCommandResult`: nothing here is
// a provider request, and borrowing that vocabulary would put refusals about a
// person's typing into an enumeration whose every member names a provider
// field.
//
// What travels is a prefix and a suffix of what the person has typed, bounded
// by the contract's own limits. Nothing on this path is journalled as a task
// and nothing about it is durable: the answer comes back as one observer push
// and is gone.

#include "taffy/browser/core_api/core_api_command_factory.h"

#include <optional>
#include <string>
#include <utility>

namespace taffy {
namespace {

namespace api = core_api::mojom;
namespace service = core_service::mojom;

bool IsRequestIdentifier(const std::string &value) {
  return !value.empty() && value.size() <= api::kMaxIdentifierBytes;
}

} // namespace

std::optional<ProjectedCoreCommand>
CoreApiCommandFactory::BuildRequestComposerCompletion(
    std::string request_id, std::string prefix,
    std::optional<std::string> suffix, uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  // An empty prefix is refused rather than sent. There is nothing to continue,
  // and a request that asked a model to continue nothing would spend a turn to
  // be told so.
  if (!IsRequestIdentifier(request_id) || prefix.empty() ||
      prefix.size() > api::kMaxComposerPrefixBytes ||
      (suffix && suffix->size() > api::kMaxComposerSuffixBytes)) {
    return std::nullopt;
  }

  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kRequestComposerCompletion;
  core_command->request_composer_completion =
      api::RequestComposerCompletionBody::New(request_id, prefix, suffix);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }

  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind =
      service::CoreServiceCommandKind::kRequestComposerCompletion;
  service_command->request_composer_completion =
      service::RequestComposerCompletionCommand::New(
          std::move(request_id), std::move(prefix), std::move(suffix));
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

// The withdrawal (decision 0097 section 3).
//
// "Cancelled rather than awaited" needed two halves and had one: the core
// already knew which dispatch a newer request displaced, and the browser
// already stopped it, but a surface that simply stopped wanting an answer
// could only mint a newer identity and pay for a model call it would then
// discard. This is the half a surface can state on its own, and the browser
// needs no new leg for it — a withdrawal is the shape it already handles for a
// superseded request, so the answer is one submission that dispatches nothing
// and names what to stop.
//
// The identity is the only field, and an unknown one is not an error here: a
// request that has already answered, or one the core never admitted, is a
// withdrawal with nothing to withdraw, and the core says so rather than this
// process guessing which of those happened.
std::optional<ProjectedCoreCommand>
CoreApiCommandFactory::BuildCancelComposerCompletion(
    std::string request_id, uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  if (!IsRequestIdentifier(request_id)) {
    return std::nullopt;
  }

  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kCancelComposerCompletion;
  core_command->cancel_composer_completion =
      api::CancelComposerCompletionBody::New(request_id);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }

  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind =
      service::CoreServiceCommandKind::kCancelComposerCompletion;
  service_command->cancel_composer_completion =
      service::CancelComposerCompletionCommand::New(std::move(request_id));
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

} // namespace taffy
