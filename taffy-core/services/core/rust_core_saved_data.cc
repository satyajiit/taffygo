// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/rust_core_saved_data.h"

#include <utility>

namespace taffy::core_service_internal {
namespace {

core_bridge::BridgeSavedDataOperation ToOperation(
    const core_service::mojom::OperationEnvelope& input) {
  core_bridge::BridgeSavedDataOperation output;
  output.operation_id = input.operation_id;
  output.service_generation = input.service_generation;
  output.task_revision = input.task_revision;
  output.deadline_monotonic_ms = input.deadline_monotonic_ms;
  output.idempotency_key = input.idempotency_key;
  return output;
}

}  // namespace

std::optional<core_bridge::BridgeSavedDataCommand> ToBridgeSavedDataCommand(
    const core_service::mojom::CoreServiceCommand& command) {
  if (command.kind != core_service::mojom::CoreServiceCommandKind::
                          kReplaceSavedDataSnapshot ||
      !command.operation || !command.replace_saved_data_snapshot) {
    return std::nullopt;
  }
  const auto& input = *command.replace_saved_data_snapshot;
  core_bridge::BridgeSavedDataCommand output;
  output.operation = ToOperation(*command.operation);
  output.sign_ins_availability =
      static_cast<uint8_t>(input.sign_ins_availability);
  output.sign_ins_revision = input.sign_ins_revision;
  output.details_availability =
      static_cast<uint8_t>(input.details_availability);
  output.details_revision = input.details_revision;
  for (const auto& record : input.sign_ins) {
    if (!record) {
      return std::nullopt;
    }
    core_bridge::BridgeSavedSignIn projected;
    projected.id = record->id;
    projected.site = record->site;
    projected.username = record->username;
    projected.last_used_epoch_ms = record->last_used_epoch_ms;
    output.sign_ins.push_back(std::move(projected));
  }
  for (const auto& record : input.details) {
    if (!record) {
      return std::nullopt;
    }
    core_bridge::BridgeSavedDetail projected;
    projected.id = record->id;
    projected.given_name = record->given_name;
    projected.family_name = record->family_name;
    projected.email = record->email;
    projected.phone = record->phone;
    projected.address = record->address;
    projected.postcode = record->postcode;
    projected.country = record->country;
    output.details.push_back(std::move(projected));
  }
  return output;
}

}  // namespace taffy::core_service_internal
