// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/rust_core_state_permissions.h"

#include <string>

#include "openssl/sha.h"

namespace taffy::core_service_internal {
namespace {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;

std::string PermissionIdentity(const bridge::BridgePendingPermission& in) {
  const std::string material =
      "v1:" + std::to_string(in.service_generation) + ":" +
      std::string(in.task_id) + ":" + std::to_string(in.task_revision) + ":" +
      std::string(in.request_id) + ":" + std::to_string(in.permission) + ":" +
      std::to_string(in.deadline_monotonic_ms) + ":" +
      std::to_string(in.deadline_utc_ms) + ":" +
      std::string(in.browser_session_id);
  std::array<uint8_t, SHA256_DIGEST_LENGTH> digest{};
  SHA256(reinterpret_cast<const uint8_t*>(material.data()), material.size(),
         digest.data());
  std::string result;
  result.reserve(digest.size() * 2u);
  const auto append_nibble = [&result](uint8_t nibble) {
    const uint8_t encoded = nibble < 10u
                                ? static_cast<uint8_t>('0') + nibble
                                : static_cast<uint8_t>('a') + nibble - 10u;
    result.push_back(static_cast<char>(encoded));
  };
  for (uint8_t byte : digest) {
    append_nibble(byte >> 4);
    append_nibble(byte & 0x0f);
  }
  return result;
}

}  // namespace

std::optional<mojom::PlatformPermission> PermissionFromWire(uint8_t value) {
  switch (value) {
    case 0:
      return mojom::PlatformPermission::kNotifications;
    case 1:
      return mojom::PlatformPermission::kMicrophone;
    case 2:
      return mojom::PlatformPermission::kCamera;
    case 3:
      return mojom::PlatformPermission::kLocation;
    default:
      return std::nullopt;
  }
}

mojom::EffectEnvelopePtr ToPermissionEffect(
    const bridge::BridgePendingPermission& in,
    mojom::PlatformPermission permission) {
  const std::string identity = PermissionIdentity(in);
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = mojom::OperationEnvelope::New();
  effect->operation->operation_id = "permission-operation-" + identity;
  effect->operation->service_generation = in.service_generation;
  effect->operation->task_revision = in.task_revision;
  effect->operation->deadline_monotonic_ms = in.deadline_monotonic_ms;
  effect->operation->idempotency_key = "permission-idempotency-" + identity;
  effect->effect_id = "permission-effect-" + identity;
  effect->kind = mojom::EffectKind::kRequestPermission;
  effect->retry_class = mojom::RetryClass::kNever;
  effect->permission_request = mojom::PermissionRequestEffect::New();
  effect->permission_request->request_id = std::string(in.request_id);
  effect->permission_request->permission = permission;
  effect->permission_request->task_id = std::string(in.task_id);
  effect->permission_request->task_revision = in.task_revision;
  effect->permission_request->deadline_monotonic_ms = in.deadline_monotonic_ms;
  effect->permission_request->deadline_utc_ms = in.deadline_utc_ms;
  effect->permission_request->browser_session_id =
      std::string(in.browser_session_id);
  return effect;
}

}  // namespace taffy::core_service_internal
