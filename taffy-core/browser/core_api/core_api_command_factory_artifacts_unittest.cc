// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
#include <memory>
#include <string>
#include <string_view>

#include "taffy/browser/core_api/core_api_command_factory.h"
#include "taffy/browser/core_service_command_validation.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

class CountingEntropy final : public CoreApiEntropySource {
 public:
  std::string NewOpaqueId(std::string_view domain) override {
    ++calls;
    return std::string(domain) + "-artifact";
  }

  std::array<uint8_t, 32> NewTaskSeed() override { return {}; }

  size_t calls = 0u;
};

TEST(CoreApiCommandFactoryArtifactsTest,
     AcceptKeepsExactTaskArtifactAndRevision) {
  CoreApiCommandFactory factory("profile", std::make_unique<CountingEntropy>());
  auto projected =
      factory.BuildAcceptTaskArtifact("task-1", "artifact-1", 7u, 11u, 100u);
  ASSERT_TRUE(projected);
  EXPECT_EQ(core_api::mojom::CoreCommandKind::kAcceptTaskArtifact,
            projected->core_api_command->kind);
  EXPECT_EQ(core_service::mojom::CoreServiceCommandKind::kAcceptTaskArtifact,
            projected->core_service_command->kind);
  EXPECT_EQ(7u, projected->core_service_command->operation->task_revision);
  EXPECT_EQ("artifact-1",
            projected->core_service_command->accept_task_artifact->artifact_id);
  EXPECT_TRUE(IsStructurallyValidCoreServiceCommand(
      *projected->core_service_command,
      core_service::mojom::kMaxCommandBytes));
}

TEST(CoreApiCommandFactoryArtifactsTest,
     ExportProjectsEveryClosedFormatWithoutForwardingRequestIdentity) {
  struct KindPair {
    core_api::mojom::TaskArtifactKind api;
    core_service::mojom::TaskArtifactKind service;
  };
  constexpr std::array<KindPair, 8> kKinds = {{
      {core_api::mojom::TaskArtifactKind::kMarkdown,
       core_service::mojom::TaskArtifactKind::kMarkdown},
      {core_api::mojom::TaskArtifactKind::kCsv,
       core_service::mojom::TaskArtifactKind::kCsv},
      {core_api::mojom::TaskArtifactKind::kXlsx,
       core_service::mojom::TaskArtifactKind::kXlsx},
      {core_api::mojom::TaskArtifactKind::kPdf,
       core_service::mojom::TaskArtifactKind::kPdf},
      {core_api::mojom::TaskArtifactKind::kDocx,
       core_service::mojom::TaskArtifactKind::kDocx},
      {core_api::mojom::TaskArtifactKind::kPptx,
       core_service::mojom::TaskArtifactKind::kPptx},
      {core_api::mojom::TaskArtifactKind::kWaveAudio,
       core_service::mojom::TaskArtifactKind::kWaveAudio},
      {core_api::mojom::TaskArtifactKind::kFrameArchive,
       core_service::mojom::TaskArtifactKind::kFrameArchive},
  }};
  for (const KindPair& kind : kKinds) {
    CoreApiCommandFactory factory("profile",
                                  std::make_unique<CountingEntropy>());
    auto projected = factory.BuildRequestTaskArtifactExport(
        "request-1", "task-1", "artifact-1", kind.api, 7u, 11u,
        100u);
    ASSERT_TRUE(projected);
    EXPECT_EQ(core_api::mojom::CoreCommandKind::kRequestTaskArtifactExport,
              projected->core_api_command->kind);
    EXPECT_EQ("request-1",
              projected->core_api_command->request_task_artifact_export
                  ->request_id);
    EXPECT_EQ(core_service::mojom::CoreServiceCommandKind::kExportTaskArtifact,
              projected->core_service_command->kind);
    EXPECT_EQ(kind.service,
              projected->core_service_command->export_task_artifact->kind);
    EXPECT_TRUE(IsStructurallyValidCoreServiceCommand(
        *projected->core_service_command,
        core_service::mojom::kMaxCommandBytes));
  }
}

TEST(CoreApiCommandFactoryArtifactsTest, InvalidInputMintsNoIdentity) {
  auto entropy = std::make_unique<CountingEntropy>();
  CountingEntropy* entropy_view = entropy.get();
  CoreApiCommandFactory factory("profile", std::move(entropy));
  EXPECT_FALSE(factory.BuildAcceptTaskArtifact("", "artifact", 7u, 11u,
                                               100u));
  EXPECT_FALSE(factory.BuildRequestTaskArtifactExport(
      "request", "task", "artifact", core_api::mojom::TaskArtifactKind::kPdf,
      0u, 11u, 100u));
  EXPECT_EQ(0u, entropy_view->calls);
}

}  // namespace
}  // namespace taffy
