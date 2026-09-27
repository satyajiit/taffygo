// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_TASK_SOURCE_SELECTION_REGISTRY_ACTIONS_TEST_SUPPORT_H_
#define TAFFY_BROWSER_TASK_SOURCE_SELECTION_REGISTRY_ACTIONS_TEST_SUPPORT_H_

#include <functional>
#include <optional>
#include <string>

#include "content/public/test/test_renderer_host.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/browser/task_source_selection_registry.h"

namespace taffy::task_source_selection_test {

class FakeTaskBrowserActionPlatform final : public TaskBrowserActionPlatform {
 public:
  FakeTaskBrowserActionPlatform();
  ~FakeTaskBrowserActionPlatform() override;
  std::optional<std::string> ResolveSearchAddress(
      const std::string& query) override;
  content::WebContents* OpenTaskTab(
      const std::string& task_id,
      const std::string& action_id,
      const std::string& destination_address) override;
  void OpenTaskDiscoveryTab(const std::string& task_id,
                            const std::string& effect_id,
                            TaskDiscoveryTabCallback callback) override;
  bool StartSearch(content::WebContents* web_contents,
                   const std::string& query,
                   const std::string& destination_address) override;
  bool ActivateTaskTab(content::WebContents* web_contents) override;
  bool CloseTaskTab(content::WebContents* web_contents) override;

  std::function<bool(content::WebContents*)> activate;
  std::function<bool(content::WebContents*)> close;
  size_t activate_calls = 0u;
  size_t close_calls = 0u;
};

class TaskSourceSelectionRegistryActionsTest
    : public content::RenderViewHostTestHarness {
 public:
  TaskSourceSelectionRegistryActionsTest();
  ~TaskSourceSelectionRegistryActionsTest() override;

 protected:
  void SetUp() override;
  void AttachHost(content::WebContents* contents);
  TaskSourceSelectionRegistry::WindowToken RegisterWindow(
      TaskSourceSelectionRegistry* registry);
  void SetGraphRevision(content::WebContents* contents, uint64_t revision);

  FakeTaskBrowserActionPlatform platform_;
  ActorLeaseRegistry host_leases_;
  CapabilityLedger host_capabilities_;
  ValueReferenceVault host_values_;
};

}  // namespace taffy::task_source_selection_test

#endif  // TAFFY_BROWSER_TASK_SOURCE_SELECTION_REGISTRY_ACTIONS_TEST_SUPPORT_H_
