// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_CORE_RUST_CORE_COMMAND_CONVERSIONS_H_
#define TAFFY_SERVICES_CORE_RUST_CORE_COMMAND_CONVERSIONS_H_

#include <optional>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/service_bridge.rs.h"
#include "taffy/services/core/service_bridge_bootstrap_ffi.rs.h"
#include "taffy/services/core/service_bridge_configuration_ffi.rs.h"
#include "taffy/services/core/service_bridge_storage_completion_ffi.rs.h"

namespace taffy::core_service_internal {

core_bridge::BridgeOperation ToBridgeOperation(
    const core_service::mojom::OperationEnvelope& operation);

core_bridge::BridgeStorageCompletionOperation
ToBridgeStorageCompletionOperation(
    const core_service::mojom::OperationEnvelope& operation);

std::optional<core_bridge::BridgeStartTask> ToBridgeStart(
    const core_service::mojom::CoreServiceCommand& command);

std::optional<core_bridge::BridgeBootstrap> ToBridgeBootstrap(
    const core_service::mojom::CoreBootstrap& bootstrap);

std::optional<core_bridge::BridgeAssistantConfigurationCommand>
ToBridgeAssistantConfiguration(
    const core_service::mojom::CoreServiceCommand& command);

}  // namespace taffy::core_service_internal

#endif  // TAFFY_SERVICES_CORE_RUST_CORE_COMMAND_CONVERSIONS_H_
