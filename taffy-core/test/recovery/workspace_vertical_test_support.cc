// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/recovery/workspace_vertical_test_support.h"

#include <algorithm>
#include <cstddef>
#include <utility>

#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::test {
namespace {

namespace api = core_api::mojom;

std::optional<ObservedWorkspaceExport> RequestWorkspaceExport(
    mojo::Remote<api::TaffyProfileCoreApi>* facade,
    CoreApiStatusObserver* observer,
    const std::string& request_id,
    const ObservedWorkspaceStatus& workspace,
    api::WorkspaceExportFormat format) {
  base::test::TestFuture<api::CoreApiSubmissionStatus> admission;
  (*facade)->RequestWorkspaceExport(request_id, workspace.workspace_id,
                                    workspace.revision, format,
                                    admission.GetCallback());
  if (admission.Get() != api::CoreApiSubmissionStatus::kAccepted) {
    ADD_FAILURE() << "The workspace export was not accepted: " << request_id;
    return std::nullopt;
  }
  if (!base::test::RunUntil([&]() {
        const std::optional<ObservedWorkspaceExport> value =
            observer->workspace_export();
        return value && value->request_id == request_id &&
               value->workspace_id == workspace.workspace_id &&
               value->revision == workspace.revision && value->format == format;
      })) {
    ADD_FAILURE() << "CoreStatus did not publish the workspace export: "
                  << request_id;
    return std::nullopt;
  }
  return observer->workspace_export();
}

}  // namespace

std::optional<ObservedWorkspaceExports> RequestObservedWorkspaceExports(
    mojo::Remote<api::TaffyProfileCoreApi>* facade,
    CoreApiStatusObserver* observer,
    const std::string& request_prefix,
    const ObservedWorkspaceStatus& workspace,
    bool require_source_backed_content) {
  std::optional<ObservedWorkspaceExport> markdown =
      RequestWorkspaceExport(facade, observer, request_prefix + "-markdown",
                             workspace, api::WorkspaceExportFormat::kMarkdown);
  std::optional<ObservedWorkspaceExport> csv =
      RequestWorkspaceExport(facade, observer, request_prefix + "-csv",
                             workspace, api::WorkspaceExportFormat::kCsv);
  if (!markdown || !csv) {
    return std::nullopt;
  }
  if (require_source_backed_content) {
    ExpectSourceBackedWorkspaceExports(workspace, *markdown, *csv);
  }
  return ObservedWorkspaceExports{std::move(*markdown), std::move(*csv)};
}

void ExpectRestoredWorkspace(const ObservedWorkspaceStatus& expected,
                             const ObservedWorkspaceStatus& actual) {
  EXPECT_EQ(expected.workspace_id, actual.workspace_id);
  EXPECT_EQ(expected.revision, actual.revision);
  EXPECT_EQ(expected.goal, actual.goal);
  EXPECT_EQ(expected.phase, actual.phase);
  EXPECT_EQ(expected.last_updated_epoch_ms, actual.last_updated_epoch_ms);
  EXPECT_EQ(expected.template_id, actual.template_id);
  EXPECT_EQ(expected.saved, actual.saved);
  EXPECT_EQ(expected.display_name, actual.display_name);
  ASSERT_EQ(expected.deletion_preview.has_value(),
            actual.deletion_preview.has_value());
  if (expected.deletion_preview) {
    EXPECT_EQ(expected.deletion_preview->sources,
              actual.deletion_preview->sources);
    EXPECT_EQ(expected.deletion_preview->facts, actual.deletion_preview->facts);
    EXPECT_EQ(expected.deletion_preview->artifact_metadata,
              actual.deletion_preview->artifact_metadata);
    EXPECT_EQ(expected.deletion_preview->derived_indexes,
              actual.deletion_preview->derived_indexes);
    EXPECT_EQ(expected.deletion_preview->confirmation_token,
              actual.deletion_preview->confirmation_token);
  }
  ASSERT_EQ(expected.sources.size(), actual.sources.size());
  for (size_t index = 0u; index < expected.sources.size(); ++index) {
    const ObservedWorkspaceSource& left = expected.sources[index];
    const ObservedWorkspaceSource& right = actual.sources[index];
    EXPECT_EQ(left.source_id, right.source_id);
    EXPECT_EQ(left.title, right.title);
    EXPECT_EQ(left.host, right.host);
    EXPECT_EQ(left.read_at_epoch_ms, right.read_at_epoch_ms);
    EXPECT_EQ(left.fact_count, right.fact_count);
    EXPECT_EQ(left.excluded, right.excluded);
  }
  ASSERT_EQ(expected.facts.size(), actual.facts.size());
  for (size_t index = 0u; index < expected.facts.size(); ++index) {
    const ObservedWorkspaceFact& left = expected.facts[index];
    const ObservedWorkspaceFact& right = actual.facts[index];
    EXPECT_EQ(left.fact_id, right.fact_id);
    EXPECT_EQ(left.field, right.field);
    EXPECT_EQ(left.value, right.value);
    EXPECT_EQ(left.kind, right.kind);
    EXPECT_EQ(left.sources, right.sources);
    EXPECT_EQ(left.correction, right.correction);
    EXPECT_EQ(left.has_conflict, right.has_conflict);
    EXPECT_EQ(left.needs_new_source, right.needs_new_source);
  }
}

void ExpectRestoredCompletedTask(const ObservedTaskStatus& expected,
                                 const CoreApiStatusObserver& actual_observer) {
  const std::optional<ObservedTaskStatus> actual = actual_observer.only_task();
  ASSERT_TRUE(actual);
  EXPECT_EQ(api::TaskPhase::kCompleted, expected.phase);
  EXPECT_EQ(expected.task_id, actual->task_id);
  EXPECT_EQ(expected.revision, actual->revision);
  EXPECT_EQ(expected.phase, actual->phase);
  EXPECT_EQ(expected.workspace_id, actual->workspace_id);
}

void ExpectSingleDomSourceWorkspace(const ObservedWorkspaceStatus& workspace,
                                    const std::string& expected_host) {
  ASSERT_EQ(1u, workspace.sources.size());
  const ObservedWorkspaceSource& source = workspace.sources.front();
  EXPECT_EQ(expected_host, source.host);
  EXPECT_FALSE(source.title.empty());
  EXPECT_GT(source.read_at_epoch_ms, 0u);
  EXPECT_FALSE(source.excluded);
  ASSERT_FALSE(workspace.facts.empty());
  for (const ObservedWorkspaceFact& fact : workspace.facts) {
    EXPECT_EQ(api::WorkspaceFactKind::kFromPage, fact.kind);
    ASSERT_EQ(1u, fact.sources.size());
    EXPECT_EQ(source.source_id, fact.sources.front());
    EXPECT_FALSE(fact.correction.has_value());
    EXPECT_FALSE(fact.has_conflict);
    EXPECT_FALSE(fact.needs_new_source);
  }
}

void ExpectSourceBackedWorkspaceExports(
    const ObservedWorkspaceStatus& workspace,
    const ObservedWorkspaceExport& markdown,
    const ObservedWorkspaceExport& csv) {
  ASSERT_FALSE(workspace.sources.empty());
  ASSERT_FALSE(workspace.facts.empty());
  for (const ObservedWorkspaceFact& fact : workspace.facts) {
    EXPECT_EQ(api::WorkspaceFactKind::kFromPage, fact.kind);
    ASSERT_FALSE(fact.sources.empty());
    for (const std::string& source_id : fact.sources) {
      EXPECT_NE(workspace.sources.end(),
                std::find_if(workspace.sources.begin(), workspace.sources.end(),
                             [&](const ObservedWorkspaceSource& source) {
                               return source.source_id == source_id;
                             }));
    }
  }
  for (const ObservedWorkspaceSource& source : workspace.sources) {
    size_t associations = 0u;
    for (const ObservedWorkspaceFact& fact : workspace.facts) {
      associations += std::count(fact.sources.begin(), fact.sources.end(),
                                 source.source_id);
    }
    EXPECT_EQ(static_cast<size_t>(source.fact_count), associations);
    EXPECT_GT(associations, 0u);
    EXPECT_NE(std::string::npos, markdown.content.find(source.host));
    EXPECT_NE(std::string::npos, csv.content.find(source.host));
  }
  EXPECT_EQ(api::WorkspaceExportFormat::kMarkdown, markdown.format);
  EXPECT_EQ(0u, markdown.content.find("# "));
  EXPECT_NE(std::string::npos, markdown.content.find("## Findings\n\n"));
  EXPECT_NE(std::string::npos, markdown.content.find("## Evidence\n\n"));
  EXPECT_NE(std::string::npos, markdown.content.find("## Sources\n\n"));
  EXPECT_EQ(api::WorkspaceExportFormat::kCsv, csv.format);
  EXPECT_EQ(0u, csv.content.find(
                    "subject,detail,value,unit,how_it_was_found,"
                    "confidence_basis_points,observed_at,sensitivity,sources,"
                    "source_locators\n"));
}

}  // namespace taffy::test
