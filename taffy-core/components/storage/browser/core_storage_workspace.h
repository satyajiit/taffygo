// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_WORKSPACE_H_
#define TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_WORKSPACE_H_

#include <stdint.h>

#include <string>
#include <string_view>

#include "base/containers/span.h"
#include "sql/database.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

struct WorkspaceSnapshotShape {
  std::string workspace_id;
  uint64_t revision = 0u;
  uint64_t source_count = 0u;
  uint64_t fact_count = 0u;
  bool saved = false;
};

struct WorkspaceDeletionCounts {
  uint64_t sources = 0u;
  uint64_t facts = 0u;
  uint64_t artifact_metadata = 0u;
  uint64_t derived_indexes = 0u;

  bool operator==(const WorkspaceDeletionCounts&) const = default;
};

struct WorkspaceDeletionRequest {
  std::string effect_id;
  std::string workspace_id;
  uint64_t expected_revision = 0u;
  uint64_t resulting_revision = 0u;
  WorkspaceDeletionCounts counts;
  std::string confirmation_token;
};

// Reads the bounded snapshot without exposing any fact/source content.
bool DecodeWorkspaceSnapshotShape(base::span<const uint8_t> snapshot,
                                  std::string_view expected_workspace_id,
                                  uint64_t expected_revision,
                                  WorkspaceSnapshotShape* shape);

// A deletion tombstone is content-free, replay-stable, and never restored as
// an active workspace.
bool IsWorkspaceDeletionTombstone(base::span<const uint8_t> snapshot,
                                  std::string_view expected_workspace_id,
                                  uint64_t stored_revision);

// Internal physical transaction. The generated operation mapping lands only
// after the Core Service contract hold is released.
bool CommitWorkspaceDeletion(sql::Database* database,
                             const WorkspaceDeletionRequest& request);

bool LoadWorkspaceSnapshots(sql::Database* database,
                            core_service::mojom::CoreBootstrap* bootstrap);

bool CommitWorkspaceSnapshot(sql::Database* database,
                             const core_service::mojom::EffectEnvelope& effect);

// The snapshot write on its own, inside a transaction the caller owns and
// commits. Source deletion needs it: removing the skills recorded against a
// site and rewriting the workspace without that source are one atomic change,
// so neither half may carry its own transaction.
bool WriteWorkspaceSnapshot(sql::Database* database,
                            const core_service::mojom::EffectEnvelope& effect);

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_WORKSPACE_H_
