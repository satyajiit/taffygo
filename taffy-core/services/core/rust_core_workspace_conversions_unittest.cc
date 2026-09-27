// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdint.h>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_workspace.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;

bridge::BridgeWorkspaceEffect WorkspacePersistEffect() {
  bridge::BridgeWorkspaceEffect effect{};
  effect.operation.operation_id = "workspace-save-operation-1";
  effect.operation.service_generation = 1u;
  effect.operation.deadline_monotonic_ms = 10'000u;
  effect.operation.idempotency_key = "workspace-save-key-1";
  effect.effect_id = "workspace-save-effect-1";
  effect.operation_kind = 3u;  // UPSERT_WORKSPACE
  effect.workspace_id = "workspace-1";
  effect.expected_revision = 2u;
  effect.resulting_revision = 3u;
  effect.snapshot = {0x01u, 0x02u, 0x03u};
  return effect;
}

TEST(RustCoreWorkspaceConversionTest,
     PersistProjectsSerializableNeutralTaskSeed) {
  mojom::EffectEnvelopePtr projected =
      core_service_internal::ToMojoWorkspaceEffect(WorkspacePersistEffect());

  ASSERT_TRUE(projected);
  ASSERT_TRUE(projected->storage_commit);
  EXPECT_EQ(mojom::StorageOperation::kUpsertWorkspace,
            projected->storage_commit->operation_kind);
  ASSERT_TRUE(projected->storage_commit->workspace);
  EXPECT_EQ("workspace-1",
            projected->storage_commit->workspace->workspace_id);
  EXPECT_EQ(32u, projected->storage_commit->task_id_seed.size());
}

}  // namespace
}  // namespace taffy
