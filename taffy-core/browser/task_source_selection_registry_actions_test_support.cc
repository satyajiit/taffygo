// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/task_source_selection_registry_actions_test_support.h"

#include <utility>

#include "content/public/browser/web_contents.h"
#include "taffy/components/intelligence/content/frame_observation_endpoint.h"
#include "taffy/components/intelligence/content/page_intelligence_broker.h"
#include "url/gurl.h"

namespace taffy::task_source_selection_test {

FakeTaskBrowserActionPlatform::FakeTaskBrowserActionPlatform() = default;
FakeTaskBrowserActionPlatform::~FakeTaskBrowserActionPlatform() = default;

std::optional<std::string> FakeTaskBrowserActionPlatform::ResolveSearchAddress(
    const std::string& query) {
  return query.empty() ? std::nullopt
                       : std::optional<std::string>("https://search.test/");
}

content::WebContents* FakeTaskBrowserActionPlatform::OpenTaskTab(
    const std::string& task_id,
    const std::string& action_id,
    const std::string& destination_address) {
  return nullptr;
}

void FakeTaskBrowserActionPlatform::OpenTaskDiscoveryTab(
    const std::string& task_id,
    const std::string& effect_id,
    TaskDiscoveryTabCallback callback) {
  std::move(callback).Run(nullptr);
}

bool FakeTaskBrowserActionPlatform::StartSearch(
    content::WebContents* web_contents,
    const std::string& query,
    const std::string& destination_address) {
  return false;
}

bool FakeTaskBrowserActionPlatform::ActivateTaskTab(
    content::WebContents* web_contents) {
  ++activate_calls;
  return activate ? activate(web_contents) : false;
}

bool FakeTaskBrowserActionPlatform::CloseTaskTab(
    content::WebContents* web_contents) {
  ++close_calls;
  return close ? close(web_contents) : false;
}

TaskSourceSelectionRegistryActionsTest::
    TaskSourceSelectionRegistryActionsTest() = default;
TaskSourceSelectionRegistryActionsTest::
    ~TaskSourceSelectionRegistryActionsTest() = default;

void TaskSourceSelectionRegistryActionsTest::SetUp() {
  content::RenderViewHostTestHarness::SetUp();
  NavigateAndCommit(GURL("https://user.example/"));
  AttachHost(web_contents());
}

void TaskSourceSelectionRegistryActionsTest::AttachHost(
    content::WebContents* contents) {
  TaffyPageIntelligenceHost::AttachWithAuthority(
      contents, &host_leases_, &host_capabilities_, &host_values_);
}

TaskSourceSelectionRegistry::WindowToken
TaskSourceSelectionRegistryActionsTest::RegisterWindow(
    TaskSourceSelectionRegistry* registry) {
  const auto window = registry->RegisterProductWindow();
  EXPECT_NE(window, 0u);
  EXPECT_TRUE(registry->BindBrowserActionPlatform(window, &platform_));
  return window;
}

void TaskSourceSelectionRegistryActionsTest::SetGraphRevision(
    content::WebContents* contents,
    uint64_t revision) {
  auto* broker = PageIntelligenceBroker::FromWebContents(contents);
  ASSERT_TRUE(broker);
  content::RenderFrameHost* frame = contents->GetPrimaryMainFrame();
  ASSERT_TRUE(frame);
  const FrameId frame_id = broker->GetOrAssignFrameId(frame);
  FrameObservationEndpoint* endpoint =
      broker->GetOrCreateActionableEndpoint(frame_id);
  ASSERT_TRUE(endpoint);
  endpoint->NoteReportedRevision(revision);
}

}  // namespace taffy::task_source_selection_test
