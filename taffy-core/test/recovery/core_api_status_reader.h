// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_RECOVERY_CORE_API_STATUS_READER_H_
#define TAFFY_TEST_RECOVERY_CORE_API_STATUS_READER_H_

#include <stdint.h>

#include <optional>
#include <vector>

#include "base/containers/span.h"
#include "base/memory/raw_span.h"
#include "taffy/test/recovery/core_api_status_observer.h"

namespace taffy::test::internal {

namespace api = core_api::mojom;

struct ObservedCoreStatusPayload {
  api::CoreAvailability availability = api::CoreAvailability::kStarting;
  uint64_t generation = 0u;
  std::vector<ObservedTaskStatus> tasks;
  std::vector<ObservedWorkspaceStatus> workspaces;
  std::optional<ObservedWorkspaceExport> workspace_export;
  ObservedLibraryStatus library;
  std::optional<ObservedLibraryExport> library_export;
  ObservedMemoryStatus memory;
  std::vector<ObservedSiteSkill> site_skills;
  bool site_skills_complete = false;
};

// Test-only reader for the generated CoreStatus wire, retaining task, saved
// work and the entire site-skill review while skipping private saved values.
// Every traversed field is consumed with the same bounds, closed-enum,
// boolean and UTF-8 rules as the generated codec.
class CoreStatusWireReader final {
 public:
  explicit CoreStatusWireReader(base::span<const uint8_t> bytes);

  std::optional<ObservedCoreStatusPayload> Read(uint32_t schema_version);

 private:
  template <typename Enum>
  std::optional<Enum> ReadClosedEnum(Enum first, Enum last) {
    const std::optional<uint32_t> value = ReadU32();
    if (!value || *value < static_cast<uint32_t>(first) ||
        *value > static_cast<uint32_t>(last)) {
      return std::nullopt;
    }
    return static_cast<Enum>(*value);
  }

  bool ReadMagic(base::span<const uint8_t> magic);
  std::optional<bool> ReadBool();
  std::optional<uint32_t> ReadU32();
  std::optional<uint32_t> ReadBoundedU32(uint64_t limit);
  std::optional<uint64_t> ReadU64();
  std::optional<uint32_t> ReadLength(uint64_t limit);
  std::optional<std::string> ReadString(uint64_t limit);
  bool SkipString(uint64_t limit);
  bool SkipOptionalString(uint64_t limit);

  std::optional<ObservedTaskStatus> ReadTask();
  bool ReadPendingAction(std::optional<std::string>* action_id);
  bool SkipFailure(std::optional<api::CoreFailureCode>* failure_code = nullptr);
  bool SkipTaskArtifact();
  bool SkipTaskActivity();
  bool SkipOptionalAuth();
  bool SkipAuth();
  bool SkipAuthAccount();
  bool SkipAuthFailure();
  bool SkipAuthMethod();
  bool SkipEntitlement();

  std::optional<ObservedWorkspaceStatus> ReadWorkspace();
  std::optional<ObservedWorkspaceSource> ReadWorkspaceSource();
  std::optional<ObservedWorkspaceFact> ReadWorkspaceFact();
  std::optional<ObservedWorkspaceDeletionPreview> ReadDeletionPreview();
  std::optional<ObservedWorkspaceExport> ReadWorkspaceExport();

  bool SkipOptionalAssetDelivery();
  bool SkipAssetDelivery();
  bool SkipAsset();
  bool SkipProviderRoster();
  bool SkipProviderRosterEntry();
  bool SkipStoredCredential();
  bool SkipProviderPresentation();
  bool SkipProviderProbes();
  bool SkipProviderProbe();
  bool SkipProbeEndpoint();
  bool SkipCustomModel();
  bool SkipProviderModels();
  bool SkipProviderModel();
  bool SkipAssistantConfiguration();

  std::optional<ObservedLibraryStatus> ReadLibrary();
  std::optional<ObservedLibraryEntry> ReadLibraryEntry();
  std::optional<ObservedLibrarySource> ReadLibrarySource();
  std::optional<ObservedLibrarySearch> ReadLibrarySearch();
  bool SkipLibraryRefreshPreview();
  bool SkipLibraryRefreshResult();
  std::optional<ObservedLibraryExport> ReadLibraryExport();
  std::optional<ObservedMemoryStatus> ReadMemory();
  std::optional<ObservedMemoryRecord> ReadMemoryRecord();
  std::optional<ObservedMemoryWorkspace> ReadMemoryWorkspace();
  std::optional<ObservedMemorySearch> ReadMemorySearch();
  bool SkipSavedData();
  std::optional<ObservedSiteSkill> ReadSiteSkill();
  std::optional<ObservedSkillStep> ReadSkillStep();
  std::optional<ObservedSkillArgument> ReadSkillArgument();
  bool ReadProjectionTail(bool* site_skills_complete);

  size_t Remaining() const;

  const base::raw_span<const uint8_t> bytes_;
  size_t offset_ = 0u;
};

}  // namespace taffy::test::internal

#endif  // TAFFY_TEST_RECOVERY_CORE_API_STATUS_READER_H_
