// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
#include <utility>
#include <vector>

#include "taffy/browser/core_api/core_api_command_factory.h"

// The one command this factory builds that has no Core API half.
//
// Every other method here projects a Core API intent into a Core Service
// command, because every other command starts as something a person did on a
// surface. This one does not. A person's answer to a field-value request goes
// into a control the *browser* draws, the browser mints each answer into its
// own vault, and what crosses to the core is a count (decision 0088).
//
// So there is deliberately no `CoreCommandKind` for it, and this returns a
// bare Core Service command rather than a `ProjectedCoreCommand`. The
// asymmetry is the same one `BuildCompleteHandover` has — that command's Core
// API body is the task identity alone, and the lease identities and the input
// count are browser facts the surface never sees — carried one step further:
// here the surface has no body at all, because a method on
// `TaffyProfileCoreApi` that took a person's typed values would be a route
// from a keyboard to the plane that is projected and journalled.
//
// What a surface *does* learn is that a request is open, from
// `TaskViewState.pending_field_value_request` on the status plane. That is an
// identity and nothing else, which is the whole of what a surface needs to
// know and the whole of what it may be told.

namespace taffy {
namespace {

namespace service = core_service::mojom;

bool IsIdentifier(const std::string &value) {
  return !value.empty() &&
         value.size() <= core_api::mojom::kMaxIdentifierBytes;
}

} // namespace

service::CoreServiceCommandPtr CoreApiCommandFactory::BuildSupplyFieldValues(
    std::string task_id, std::string request_id, uint32_t supplied,
    service::FieldValueAskOutcome outcome,
    std::vector<std::string> field_node_ids, uint64_t task_revision,
    uint64_t service_generation, uint64_t now_monotonic_ms) {
  if (!IsIdentifier(task_id) || !IsIdentifier(request_id) ||
      task_revision == 0u) {
    return nullptr;
  }
  // The count is not bounded here, and that is not an oversight: its bound has
  // one owner in C++ (`kMaxSuppliedFieldValues` in
  // core_service_command_validation.h, which reads it off the task engine's
  // own `MAX_REQUESTED_FIELDS`) and the gate that owns it refuses the command
  // on the way to the core. A second copy of the number in this file is a
  // number that drifts.

  // The operation envelope is still minted through the Core API's own
  // constructor and projected, because an operation identity, an idempotency
  // key and a deadline are the same three facts whatever the body is, and
  // giving this command a private way to invent them would be a second
  // vocabulary for identity.
  core_api::mojom::OperationEnvelopePtr operation =
      NewCoreOperation(task_revision, service_generation, now_monotonic_ms);
  auto command = service::CoreServiceCommand::New();
  command->operation = ProjectOperation(*operation);
  command->kind = service::CoreServiceCommandKind::kSupplyFieldValues;
  // The outcome is passed through rather than decided here. This factory
  // mints identities and knows nothing about why a sheet closed; the
  // coordinator that abandoned the ask is the only party that does
  // (decision 0215).
  // The field identities travel as given and are bounded where the count is,
  // by the gate that refuses the command on the way to the core.
  command->supply_field_values = service::SupplyFieldValuesCommand::New(
      std::move(task_id), std::move(request_id), supplied,
      entropy_source_->NewOpaqueId("trace"), outcome,
      std::move(field_node_ids));
  return command;
}

} // namespace taffy
