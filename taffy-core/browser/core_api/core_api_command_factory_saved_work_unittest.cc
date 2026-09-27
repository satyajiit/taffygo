// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "taffy/browser/core_api/core_api_command_factory.h"
#include "taffy/browser/core_service_command_validation.h"
#include "taffy/browser/generated/product_capabilities.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

constexpr char kMemoryId[] = "11111111111111111111111111111111";
constexpr char kWorkspaceId[] = "22222222222222222222222222222222";

class SavedWorkEntropy final : public CoreApiEntropySource {
 public:
  std::string NewOpaqueId(std::string_view domain) override {
    ++calls;
    return std::string(domain) + "-fixed";
  }

  std::array<uint8_t, 32> NewTaskSeed() override { return {}; }

  size_t calls = 0u;
};

TEST(CoreApiCommandFactorySavedWorkTest,
     LibraryCommandsKeepExactRevisionsAndBrowserTimes) {
  CoreApiCommandFactory search_factory("profile",
                                       std::make_unique<SavedWorkEntropy>());
  auto search = search_factory.BuildSearchLibrary(
      "search-1", "battery life", 7u, 1'800'000'000'000u, 13u, 100u);
  ASSERT_TRUE(search);
  ASSERT_TRUE(search->core_api_command->search_library);
  ASSERT_TRUE(search->core_service_command->search_library);
  EXPECT_EQ(7u, search->core_service_command->search_library->limit);
  EXPECT_EQ(
      1'800'000'000'000u,
      search->core_service_command->search_library->requested_at_epoch_ms);

  CoreApiCommandFactory save_factory("profile",
                                     std::make_unique<SavedWorkEntropy>());
  auto save = save_factory.BuildSaveLibraryFact(
      "workspace-1", 9u, "fact-1", 3u, 2u, 1'800'000'000'001u, 13u, 100u);
  ASSERT_TRUE(save);
  ASSERT_TRUE(save->core_service_command->save_library_fact);
  EXPECT_EQ(9u, save->core_service_command->save_library_fact
                    ->expected_workspace_revision);
  EXPECT_EQ(
      3u,
      save->core_service_command->save_library_fact->expected_library_revision);
  EXPECT_EQ(
      2u,
      save->core_service_command->save_library_fact->expected_entry_revision);

  CoreApiCommandFactory remove_factory("profile",
                                       std::make_unique<SavedWorkEntropy>());
  auto remove = remove_factory.BuildRemoveLibraryEntry(
      "entry-1", 4u, 3u, 1'800'000'000'002u, 13u, 100u);
  ASSERT_TRUE(remove);
  ASSERT_TRUE(remove->core_service_command->remove_library_entry);
  EXPECT_EQ(4u, remove->core_service_command->remove_library_entry
                    ->expected_library_revision);
  EXPECT_EQ(3u, remove->core_service_command->remove_library_entry
                    ->expected_entry_revision);

  CoreApiCommandFactory export_factory("profile",
                                       std::make_unique<SavedWorkEntropy>());
  auto export_result = export_factory.BuildRequestLibraryExport(
      "export-1", 4u, std::optional<std::string>("collection-1"),
      core_api::mojom::WorkspaceExportFormat::kCsv, 13u, 100u);
  ASSERT_TRUE(export_result);
  ASSERT_TRUE(export_result->core_service_command->request_library_export);
  EXPECT_EQ("collection-1", *export_result->core_service_command
                                 ->request_library_export->collection_id);

  EXPECT_TRUE(IsStructurallyValidCoreServiceCommand(
      *search->core_service_command, core_service::mojom::kMaxCommandBytes));
  EXPECT_TRUE(IsStructurallyValidCoreServiceCommand(
      *save->core_service_command, core_service::mojom::kMaxCommandBytes));
  EXPECT_TRUE(IsStructurallyValidCoreServiceCommand(
      *remove->core_service_command, core_service::mojom::kMaxCommandBytes));
  EXPECT_TRUE(IsStructurallyValidCoreServiceCommand(
      *export_result->core_service_command,
      core_service::mojom::kMaxCommandBytes));
}

TEST(CoreApiCommandFactorySavedWorkTest,
     LibraryShapeAndInputFailClosedWithoutEntropy) {
  auto entropy = std::make_unique<SavedWorkEntropy>();
  SavedWorkEntropy* entropy_view = entropy.get();
  CoreApiCommandFactory factory("profile", std::move(entropy));

  auto mismatched =
      factory.BuildSearchLibrary("search-1", "query", 1u, 1u, 13u, 100u);
  ASSERT_TRUE(mismatched);
  mismatched->core_service_command->kind =
      core_service::mojom::CoreServiceCommandKind::kRemoveLibraryEntry;
  EXPECT_FALSE(IsStructurallyValidCoreServiceCommand(
      *mismatched->core_service_command,
      core_service::mojom::kMaxCommandBytes));

  auto invalid_query =
      factory.BuildSearchLibrary("search-2", "query", 1u, 1u, 13u, 100u);
  ASSERT_TRUE(invalid_query);
  invalid_query->core_service_command->search_library->query =
      std::string(core_service::mojom::kMaxLibraryQueryBytes + 1u, 'x');
  EXPECT_FALSE(IsStructurallyValidCoreServiceCommand(
      *invalid_query->core_service_command,
      core_service::mojom::kMaxCommandBytes));

  const size_t valid_entropy_calls = entropy_view->calls;
  EXPECT_EQ(4u, valid_entropy_calls);

  EXPECT_FALSE(
      factory.BuildSearchLibrary("search-3", "   ", 7u, 1u, 13u, 100u));
  EXPECT_FALSE(factory.BuildSaveLibraryFact("workspace-1", 0u, "fact-1", 0u, 0u,
                                            1u, 13u, 100u));
  EXPECT_FALSE(
      factory.BuildRemoveLibraryEntry("entry-1", 1u, 0u, 1u, 13u, 100u));
  EXPECT_FALSE(factory.BuildRequestLibraryExport(
      "export-1", 1u, std::optional<std::string>(""),
      core_api::mojom::WorkspaceExportFormat::kCsv, 13u, 100u));
  EXPECT_EQ(valid_entropy_calls, entropy_view->calls);
}

TEST(CoreApiCommandFactorySavedWorkTest,
     LibraryRefreshStartsExactNoModelWorkOnlyAfterApproval) {
  if (!product_capabilities::Active().delegated_task_start) {
    GTEST_SKIP() << "delegated tasks are quarantined in this profile";
  }
  CoreApiCommandFactory factory("profile",
                                std::make_unique<SavedWorkEntropy>());
  auto refresh = factory.BuildStartLibraryRefresh(
      std::string(64u, 'a'), "collection-1", 9u, 4u, 3u,
      "browser-session-1", 13u, 100u);

  ASSERT_TRUE(refresh);
  ASSERT_TRUE(refresh->core_api_command->start_library_refresh);
  EXPECT_EQ(9u, refresh->core_api_command->start_library_refresh
                    ->expected_library_revision);
  EXPECT_EQ(4u, refresh->core_api_command->start_library_refresh
                    ->expected_workspace_revision);
  EXPECT_EQ(3u,
            refresh->core_api_command->start_library_refresh->source_count);
  ASSERT_TRUE(refresh->core_service_command->start_task);
  const auto& start = refresh->core_service_command->start_task;
  EXPECT_EQ(core_service::mojom::TaskProviderRoute::kNoModelRequired,
            start->consent_preview->provider_route);
  EXPECT_TRUE(start->consent_preview->sources.empty());
  EXPECT_EQ(3u, start->consent_preview->new_source_cap);
  ASSERT_TRUE(start->library_refresh);
  EXPECT_TRUE(start->library_refresh->sources.empty());
  ASSERT_TRUE(start->workspace_id);
  EXPECT_NE(*start->workspace_id, start->library_refresh->collection_id);
  EXPECT_TRUE(IsStructurallyValidCoreServiceCommand(
      *refresh->core_service_command,
      core_service::mojom::kMaxCommandBytes));
}

TEST(CoreApiCommandFactorySavedWorkTest,
     LibraryRefreshRefusesStaleOrMalformedApprovalBeforeEntropy) {
  if (!product_capabilities::Active().delegated_task_start) {
    GTEST_SKIP() << "delegated tasks are quarantined in this profile";
  }
  auto entropy = std::make_unique<SavedWorkEntropy>();
  SavedWorkEntropy* entropy_view = entropy.get();
  CoreApiCommandFactory factory("profile", std::move(entropy));

  EXPECT_FALSE(factory.BuildStartLibraryRefresh(
      std::string(63u, 'a'), "collection-1", 9u, 4u, 3u,
      "browser-session-1", 13u, 100u));
  EXPECT_FALSE(factory.BuildStartLibraryRefresh(
      std::string(64u, 'A'), "collection-1", 9u, 4u, 3u,
      "browser-session-1", 13u, 100u));
  EXPECT_FALSE(factory.BuildStartLibraryRefresh(
      std::string(64u, 'a'), "collection-1", 0u, 4u, 3u,
      "browser-session-1", 13u, 100u));
  EXPECT_FALSE(factory.BuildStartLibraryRefresh(
      std::string(64u, 'a'), "collection-1", 9u, 4u, 0u,
      "browser-session-1", 13u, 100u));
  EXPECT_EQ(0u, entropy_view->calls);
}

TEST(CoreApiCommandFactorySavedWorkTest,
     MemoryCommandsKeepExactRevisionsScopeAndBrowserTimes) {
  CoreApiCommandFactory search_factory("profile",
                                       std::make_unique<SavedWorkEntropy>());
  auto search = search_factory.BuildSearchMemory(
      "search-1", "concise answers", 7u, 1'800'000'000'000u, 13u, 100u);
  ASSERT_TRUE(search);
  ASSERT_TRUE(search->core_service_command->search_memory);
  EXPECT_EQ(7u, search->core_service_command->search_memory->limit);
  EXPECT_EQ(1'800'000'000'000u,
            search->core_service_command->search_memory->requested_at_epoch_ms);

  CoreApiCommandFactory save_factory("profile",
                                     std::make_unique<SavedWorkEntropy>());
  auto workspace = core_api::mojom::MemoryWorkspaceView::New(
      kWorkspaceId, "Research preferences");
  auto save = save_factory.BuildUpsertMemory(
      std::optional<std::string>(kMemoryId), "Prefer concise answers",
      core_api::mojom::MemoryScopeKind::kWorkspace, std::move(workspace),
      core_api::mojom::MemorySensitivity::kSensitive, 9u, 4u,
      1'900'000'000'000u, 1'800'000'000'001u, 13u, 100u);
  ASSERT_TRUE(save);
  ASSERT_TRUE(save->core_service_command->upsert_memory);
  const auto& saved = save->core_service_command->upsert_memory;
  ASSERT_TRUE(saved->memory_id);
  EXPECT_EQ(kMemoryId, *saved->memory_id);
  EXPECT_EQ(9u, saved->expected_memory_revision);
  EXPECT_EQ(4u, saved->expected_record_revision);
  EXPECT_EQ(core_service::mojom::MemoryScopeKind::kWorkspace,
            saved->scope_kind);
  ASSERT_TRUE(saved->scope_workspace);
  EXPECT_EQ(kWorkspaceId, saved->scope_workspace->workspace_id);
  EXPECT_EQ(core_service::mojom::MemorySensitivity::kSensitive,
            saved->sensitivity);

  CoreApiCommandFactory delete_factory("profile",
                                       std::make_unique<SavedWorkEntropy>());
  auto deletion = delete_factory.BuildDeleteMemory(
      kMemoryId, 10u, 5u, 1'800'000'000'002u, 13u, 100u);
  ASSERT_TRUE(deletion);
  ASSERT_TRUE(deletion->core_service_command->delete_memory);
  EXPECT_EQ(
      10u,
      deletion->core_service_command->delete_memory->expected_memory_revision);
  EXPECT_EQ(
      5u,
      deletion->core_service_command->delete_memory->expected_record_revision);

  EXPECT_TRUE(IsStructurallyValidCoreServiceCommand(
      *search->core_service_command, core_service::mojom::kMaxCommandBytes));
  EXPECT_TRUE(IsStructurallyValidCoreServiceCommand(
      *save->core_service_command, core_service::mojom::kMaxCommandBytes));
  EXPECT_TRUE(IsStructurallyValidCoreServiceCommand(
      *deletion->core_service_command, core_service::mojom::kMaxCommandBytes));
}

TEST(CoreApiCommandFactorySavedWorkTest,
     MemoryShapeAndInputFailClosedWithoutEntropy) {
  auto entropy = std::make_unique<SavedWorkEntropy>();
  SavedWorkEntropy* entropy_view = entropy.get();
  CoreApiCommandFactory factory("profile", std::move(entropy));

  auto mismatched =
      factory.BuildSearchMemory("search-1", "query", 1u, 1u, 13u, 100u);
  ASSERT_TRUE(mismatched);
  mismatched->core_service_command->kind =
      core_service::mojom::CoreServiceCommandKind::kDeleteMemory;
  EXPECT_FALSE(IsStructurallyValidCoreServiceCommand(
      *mismatched->core_service_command,
      core_service::mojom::kMaxCommandBytes));

  const size_t valid_entropy_calls = entropy_view->calls;
  EXPECT_EQ(2u, valid_entropy_calls);

  EXPECT_FALSE(factory.BuildSearchMemory("search-2", "   ", 1u, 1u, 13u, 100u));
  EXPECT_FALSE(
      factory.BuildSearchMemory("search-2", "query", 0u, 1u, 13u, 100u));
  EXPECT_FALSE(factory.BuildUpsertMemory(
      std::optional<std::string>(kMemoryId), "Prefer concise answers",
      core_api::mojom::MemoryScopeKind::kAllTasks, nullptr,
      core_api::mojom::MemorySensitivity::kStandard, 1u, 0u, 0u, 1u, 13u,
      100u));
  EXPECT_FALSE(factory.BuildUpsertMemory(
      std::nullopt, " hidden", core_api::mojom::MemoryScopeKind::kAllTasks,
      nullptr, core_api::mojom::MemorySensitivity::kStandard, 1u, 0u, 0u, 1u,
      13u, 100u));
  EXPECT_FALSE(factory.BuildDeleteMemory(kMemoryId, 1u, 0u, 1u, 13u, 100u));
  EXPECT_EQ(valid_entropy_calls, entropy_view->calls);
}

}  // namespace
}  // namespace taffy
