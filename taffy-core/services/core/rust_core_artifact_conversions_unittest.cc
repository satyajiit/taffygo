// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdint.h>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_task_effect.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;

bridge::BridgeTaskEffect ArtifactBinding(mojom::TaskArtifactKind artifact_kind,
                                         bool carries_content) {
  bridge::BridgeTaskEffect effect{};
  effect.operation.operation_id = "artifact-operation";
  effect.operation.service_generation = 1u;
  effect.operation.task_revision = 1u;
  effect.operation.deadline_monotonic_ms = 1000u;
  effect.operation.idempotency_key = "artifact-key";
  effect.effect_id = "artifact-effect";
  effect.task_id = "task-1";
  effect.kind = 9u;  // EXPORT_ARTIFACT
  effect.artifact_kind = static_cast<uint8_t>(artifact_kind);
  effect.artifact_id = "artifact-1";
  effect.artifact_workspace_revision = 1u;
  if (carries_content) {
    effect.artifact_content = {'P', 'K', 0x03u, 0x04u};
  }
  return effect;
}

TEST(RustCoreArtifactConversionTest,
     DocumentsSelectBrowserCustodyByContentPresence) {
  for (const mojom::TaskArtifactKind kind : {mojom::TaskArtifactKind::kDocx,
                                             mojom::TaskArtifactKind::kXlsx}) {
    SCOPED_TRACE(static_cast<int>(kind));
    const auto retained =
        core_service_internal::ToMojoTaskEffect(ArtifactBinding(kind, false));
    ASSERT_TRUE(retained);
    ASSERT_TRUE((*retained)->export_artifact);
    EXPECT_TRUE((*retained)->export_artifact->content.empty());

    const auto inline_bytes =
        core_service_internal::ToMojoTaskEffect(ArtifactBinding(kind, true));
    ASSERT_TRUE(inline_bytes);
    ASSERT_TRUE((*inline_bytes)->export_artifact);
    EXPECT_FALSE((*inline_bytes)->export_artifact->content.empty());
  }
}

TEST(RustCoreArtifactConversionTest, MediaCannotEscapeBrowserCustody) {
  for (const mojom::TaskArtifactKind kind : {
           mojom::TaskArtifactKind::kWaveAudio,
           mojom::TaskArtifactKind::kFrameArchive}) {
    SCOPED_TRACE(static_cast<int>(kind));
    EXPECT_TRUE(
        core_service_internal::ToMojoTaskEffect(ArtifactBinding(kind, false)));
    EXPECT_FALSE(
        core_service_internal::ToMojoTaskEffect(ArtifactBinding(kind, true)));
  }
}

TEST(RustCoreArtifactConversionTest,
     InlineOnlyFormatsCannotClaimBrowserCustody) {
  for (const mojom::TaskArtifactKind kind : {
           mojom::TaskArtifactKind::kMarkdown, mojom::TaskArtifactKind::kCsv,
           mojom::TaskArtifactKind::kPdf, mojom::TaskArtifactKind::kPptx}) {
    SCOPED_TRACE(static_cast<int>(kind));
    EXPECT_FALSE(
        core_service_internal::ToMojoTaskEffect(ArtifactBinding(kind, false)));
    EXPECT_TRUE(
        core_service_internal::ToMojoTaskEffect(ArtifactBinding(kind, true)));
  }
}

}  // namespace
}  // namespace taffy
