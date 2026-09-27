// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_task_effect.h"

#include <array>
#include <iterator>
#include <utility>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr uint64_t kGeneration = 5u;
constexpr uint64_t kRevision = 9u;
constexpr uint64_t kNow = 10'000u;

mojom::TaskEffectBindingPtr ExportBinding(
    mojom::TaskArtifactKind kind,
    std::vector<uint8_t> content) {
  auto binding = mojom::TaskEffectBinding::New();
  binding->operation = mojom::OperationEnvelope::New(
      "artifact-operation", kGeneration, kRevision, 13'000u,
      "artifact-idempotency");
  binding->effect_id = "artifact-effect";
  binding->task_id = "task-1";
  binding->kind = mojom::TaskReducerEffectKind::kExportArtifact;
  binding->export_artifact = mojom::TaskArtifactEffect::New(
      kind, "artifact-1", kRevision, std::move(content));
  return binding;
}

TEST(CoreTaskEffectArtifactTest,
     RichToolArtifactsUseEmptyCustodyWhileInlineRichBytesStayValid) {
  constexpr std::array<uint8_t, 4> kZip = {'P', 'K', 0x03u, 0x04u};
  for (const mojom::TaskArtifactKind kind : {
           mojom::TaskArtifactKind::kDocx,
           mojom::TaskArtifactKind::kXlsx,
       }) {
    EXPECT_TRUE(IsStructurallyValidTaskEffectBinding(
        *ExportBinding(kind, {}), kGeneration, kRevision, kNow));
    EXPECT_TRUE(IsStructurallyValidTaskEffectBinding(
        *ExportBinding(kind,
                       std::vector<uint8_t>(kZip.begin(), kZip.end())),
        kGeneration, kRevision, kNow));
  }

  EXPECT_TRUE(IsStructurallyValidTaskEffectBinding(
      *ExportBinding(mojom::TaskArtifactKind::kWaveAudio, {}), kGeneration,
      kRevision, kNow));
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(
      *ExportBinding(mojom::TaskArtifactKind::kWaveAudio,
                     std::vector<uint8_t>{'R', 'I', 'F', 'F'}),
      kGeneration, kRevision, kNow));
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(
      *ExportBinding(mojom::TaskArtifactKind::kPdf, {}), kGeneration,
      kRevision, kNow));
}

}  // namespace
}  // namespace taffy
