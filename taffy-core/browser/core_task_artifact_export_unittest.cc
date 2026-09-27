// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_task_artifact_export.h"

#include <stdint.h>

#include <string>
#include <vector>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

class ArtifactObserver final : public CoreServiceObserver {
 public:
  explicit ArtifactObserver(bool accepts) : accepts_(accepts) {}

  bool OnTaskArtifactExport(
      const std::string& task_id,
      const std::string& artifact_id,
      core_service::mojom::TaskArtifactKind kind,
      const std::vector<uint8_t>& content) override {
    seen_task_id = task_id;
    seen_artifact_id = artifact_id;
    seen_kind = kind;
    seen_content = content;
    return accepts_;
  }

  std::string seen_task_id;
  std::string seen_artifact_id;
  core_service::mojom::TaskArtifactKind seen_kind =
      core_service::mojom::TaskArtifactKind::kMarkdown;
  std::vector<uint8_t> seen_content;

 private:
  const bool accepts_;
};

core_service::mojom::TaskArtifactEffect Artifact() {
  core_service::mojom::TaskArtifactEffect artifact;
  artifact.kind = core_service::mojom::TaskArtifactKind::kXlsx;
  artifact.artifact_id = "artifact-1";
  artifact.workspace_revision = 7u;
  artifact.content = {0x50u, 0x4bu, 0x03u, 0x04u};
  return artifact;
}

TEST(CoreTaskArtifactExportTest, RefusesWhenNoSurfaceAcceptsCustody) {
  base::ObserverList<CoreServiceObserver> observers;
  ArtifactObserver refusing(/*accepts=*/false);
  observers.AddObserver(&refusing);

  EXPECT_FALSE(DeliverTaskArtifactExport(observers, "task-1", Artifact()));
  EXPECT_EQ("artifact-1", refusing.seen_artifact_id);
}

TEST(CoreTaskArtifactExportTest, DeliversExactValidatedBytesToTrustedSurface) {
  base::ObserverList<CoreServiceObserver> observers;
  ArtifactObserver accepting(/*accepts=*/true);
  observers.AddObserver(&accepting);

  EXPECT_TRUE(DeliverTaskArtifactExport(observers, "task-1", Artifact()));
  EXPECT_EQ("task-1", accepting.seen_task_id);
  EXPECT_EQ("artifact-1", accepting.seen_artifact_id);
  EXPECT_EQ(core_service::mojom::TaskArtifactKind::kXlsx,
            accepting.seen_kind);
  EXPECT_EQ((std::vector<uint8_t>{0x50u, 0x4bu, 0x03u, 0x04u}),
            accepting.seen_content);
}

}  // namespace
}  // namespace taffy
