// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_CORE_RUST_CORE_MODEL_REGISTRATION_H_
#define TAFFY_SERVICES_CORE_RUST_CORE_MODEL_REGISTRATION_H_

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/service_bridge.rs.h"

namespace taffy::core_service_internal {

// Validates the isolated core's complete local-model registration snapshot
// and appends its typed Mojo projection. False rejects the whole state; a
// partial model register must never become browser authority.
bool PopulateModelArtifactRegistrations(
    const rust::Vec<core_bridge::BridgeModelArtifactRegistration>& artifacts,
    core_service::mojom::CoreStateUpdate* state);

}  // namespace taffy::core_service_internal

#endif  // TAFFY_SERVICES_CORE_RUST_CORE_MODEL_REGISTRATION_H_
