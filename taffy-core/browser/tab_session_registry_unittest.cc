// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/tab_session_registry.h"

#include <string>
#include <vector>

#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

// PAR-TAB-001 (create, list, switch, close with no data loss across rapid
// operations) and the three refusals the registry owns.

namespace taffy {
namespace {

// A tab model that always succeeds and hands out sequential identifiers, so
// the tests are about the registry's rules and not about a platform's moods.
class FakeTabModel : public TabSessionDelegate {
 public:
  TabId OpenTab(const TabOpenRequest& request) override {
    ++open_calls;
    if (refuse_open) {
      return TabId{};
    }
    if (reissue_retired.is_valid()) {
      TabId reissued = reissue_retired;
      reissue_retired = TabId{};
      return reissued;
    }
    return TabId{"tab_" + std::to_string(++next_index)};
  }

  bool ActivateTab(const TabId& tab_id) override {
    ++activate_calls;
    activated = tab_id;
    return !refuse_activate;
  }

  bool CloseTab(const TabId& tab_id) override {
    ++close_calls;
    closed.push_back(tab_id);
    return !refuse_close;
  }

  int next_index = 0;
  int open_calls = 0;
  int activate_calls = 0;
  int close_calls = 0;
  bool refuse_open = false;
  bool refuse_activate = false;
  bool refuse_close = false;
  TabId reissue_retired;
  TabId activated;
  std::vector<TabId> closed;
};

class TabSessionRegistryTest : public testing::Test {
 protected:
  TabOpenRequest UserRequest() {
    TabOpenRequest request;
    request.profile_id = ProfileId{"profile_default"};
    request.window_id = BrowserWindowId{"window_1"};
    request.request_origin = TabRequestOrigin::kUserGesture;
    request.ownership = TabOwnership::kUser;
    request.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
    return request;
  }

  TabOpenRequest AssistantRequest() {
    TabOpenRequest request = UserRequest();
    request.request_origin = TabRequestOrigin::kAssistantTask;
    request.ownership = TabOwnership::kAssistant;
    request.owning_task_id = TaskId{"task_1"};
    request.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
    return request;
  }

  TabId OpenUserTab() {
    TabId opened;
    EXPECT_EQ(TabOpenResult::kOpened,
              registry_.OpenTab(UserRequest(), &opened));
    return opened;
  }

  content::BrowserTaskEnvironment task_environment_;
  FakeTabModel model_;
  TabSessionRegistry registry_;

  void SetUp() override { registry_.SetDelegate(&model_); }
  void TearDown() override { registry_.SetDelegate(nullptr); }
};

TEST_F(TabSessionRegistryTest, CreateListSwitchAndClose) {
  const TabId first = OpenUserTab();
  const TabId second = OpenUserTab();

  ASSERT_EQ(2u, registry_.tab_count());
  EXPECT_EQ(second, registry_.active_tab_id());

  std::vector<TabRecord> listed = registry_.ListTabs();
  ASSERT_EQ(2u, listed.size());
  EXPECT_EQ(first, listed[0].tab_id);
  EXPECT_EQ(second, listed[1].tab_id);

  EXPECT_EQ(TabActivateResult::kActivated,
            registry_.ActivateTab(first, TabRequestOrigin::kUserGesture));
  EXPECT_EQ(first, registry_.active_tab_id());
  EXPECT_TRUE(registry_.FindTab(first)->is_active);
  EXPECT_FALSE(registry_.FindTab(second)->is_active);

  EXPECT_EQ(TabCloseResult::kClosed,
            registry_.CloseTab(first, TabCloseOrigin::kUserGesture));
  EXPECT_EQ(1u, registry_.tab_count());
  // Focus does not stay on a closed tab.
  EXPECT_EQ(second, registry_.active_tab_id());
}

TEST_F(TabSessionRegistryTest, ClosedIdentifiersAreNeverReused) {
  const TabId first = OpenUserTab();
  ASSERT_EQ(TabCloseResult::kClosed,
            registry_.CloseTab(first, TabCloseOrigin::kUserGesture));
  EXPECT_TRUE(registry_.IsRetired(first));

  // A platform that reissues a retired identifier is a platform whose tabs
  // cannot be told apart, so the registry refuses rather than adopting it.
  model_.reissue_retired = first;
  TabId opened;
  EXPECT_EQ(TabOpenResult::kPlatformRefused,
            registry_.OpenTab(UserRequest(), &opened));
  EXPECT_FALSE(registry_.FindTab(first));
}

TEST_F(TabSessionRegistryTest, AssistantCannotCloseAUserTab) {
  const TabId user_tab = OpenUserTab();

  EXPECT_EQ(TabCloseResult::kRefusedAssistantMayNotCloseUserTab,
            registry_.CloseTab(user_tab, TabCloseOrigin::kAssistantCleanup));
  EXPECT_EQ(0, model_.close_calls);
  EXPECT_TRUE(registry_.FindTab(user_tab));
}

TEST_F(TabSessionRegistryTest, AssistantMayCloseItsOwnTab) {
  TabId assistant_tab;
  ASSERT_EQ(TabOpenResult::kOpened,
            registry_.OpenTab(AssistantRequest(), &assistant_tab));

  EXPECT_EQ(TabCloseResult::kClosed,
            registry_.CloseTab(assistant_tab, TabCloseOrigin::kAssistantCleanup));
  EXPECT_EQ(1, model_.close_calls);
}

TEST_F(TabSessionRegistryTest, AssistantCannotStealFocus) {
  const TabId user_tab = OpenUserTab();
  TabId assistant_tab;
  ASSERT_EQ(TabOpenResult::kOpened,
            registry_.OpenTab(AssistantRequest(), &assistant_tab));

  EXPECT_EQ(TabActivateResult::kRefusedAssistantMayNotStealFocus,
            registry_.ActivateTab(user_tab, TabRequestOrigin::kAssistantTask));
  EXPECT_EQ(0, model_.activate_calls);

  // Its own tab is fine.
  EXPECT_EQ(TabActivateResult::kActivated,
            registry_.ActivateTab(assistant_tab,
                                  TabRequestOrigin::kAssistantTask));
}

TEST_F(TabSessionRegistryTest, UnattributedRequestsAreRefused) {
  TabOpenRequest request = UserRequest();
  request.request_origin = TabRequestOrigin::kUnknown;
  TabId opened;
  EXPECT_EQ(TabOpenResult::kRefusedUnattributedOrigin,
            registry_.OpenTab(request, &opened));
  EXPECT_EQ(0, model_.open_calls);

  const TabId tab = OpenUserTab();
  EXPECT_EQ(TabActivateResult::kRefusedUnattributedOrigin,
            registry_.ActivateTab(tab, TabRequestOrigin::kUnknown));
  EXPECT_EQ(TabCloseResult::kRefusedUnattributedOrigin,
            registry_.CloseTab(tab, TabCloseOrigin::kUnknown));
}

TEST_F(TabSessionRegistryTest, AssistantWithoutATaskHasNoScope) {
  TabOpenRequest request = AssistantRequest();
  request.owning_task_id = TaskId{};
  TabId opened;

  EXPECT_EQ(TabOpenResult::kRefusedAssistantOutsideTaskScope,
            registry_.OpenTab(request, &opened));
  EXPECT_EQ(0, model_.open_calls);
}

TEST_F(TabSessionRegistryTest, UnsupportedDispositionIsRefusedNotApproximated) {
  TabOpenRequest request = UserRequest();
  request.disposition = WindowOpenDisposition::NEW_POPUP;
  TabId opened;

  // PAR-WEB-011 owns popups and lands at M4. Approximating one here would be a
  // capability nobody reviewed.
  EXPECT_EQ(TabOpenResult::kRefusedUnsupportedDisposition,
            registry_.OpenTab(request, &opened));
  EXPECT_EQ(0, model_.open_calls);
}

TEST_F(TabSessionRegistryTest, EveryMutationIsRefusedWithoutADelegate) {
  registry_.SetDelegate(nullptr);
  TabId opened;
  EXPECT_EQ(TabOpenResult::kRefusedNoDelegate,
            registry_.OpenTab(UserRequest(), &opened));
  EXPECT_EQ(0u, registry_.tab_count());
}

TEST_F(TabSessionRegistryTest, RapidOpenAndCloseLosesNothing) {
  // PAR-TAB-001's "no data loss across rapid tab operations". The registry is
  // synchronous, so the property to hold is that the record and the retirement
  // set stay consistent no matter the interleaving.
  std::vector<TabId> opened;
  for (int i = 0; i < 32; ++i) {
    opened.push_back(OpenUserTab());
  }
  ASSERT_EQ(32u, registry_.tab_count());

  for (size_t i = 0; i < opened.size(); i += 2) {
    ASSERT_EQ(TabCloseResult::kClosed,
              registry_.CloseTab(opened[i], TabCloseOrigin::kUserGesture));
  }

  EXPECT_EQ(16u, registry_.tab_count());
  for (size_t i = 0; i < opened.size(); ++i) {
    const bool closed = i % 2 == 0;
    EXPECT_EQ(closed, registry_.IsRetired(opened[i]));
    EXPECT_EQ(!closed, registry_.FindTab(opened[i]) != nullptr);
  }
  EXPECT_TRUE(registry_.FindTab(registry_.active_tab_id()));
}

TEST_F(TabSessionRegistryTest, AdoptingARecoveryPlanRestoresOwnershipAndFocus) {
  SessionRecoveryPlan plan;
  TabRestoreDirective user_tab;
  user_tab.tab_id = ToRecordIdentifier("tab_user");
  user_tab.index = 0;
  user_tab.action = TabRestoreAction::kRestoreAsUserTab;
  user_tab.activate = true;
  TabRestoreDirective assistant_tab;
  assistant_tab.tab_id = ToRecordIdentifier("tab_assistant");
  assistant_tab.index = 1;
  assistant_tab.action = TabRestoreAction::kRestoreAsAssistantTabPaused;
  TabRestoreDirective discarded;
  discarded.tab_id = ToRecordIdentifier("tab_gone");
  discarded.index = 2;
  discarded.action = TabRestoreAction::kDiscardTemporaryTab;
  plan.tabs = {user_tab, assistant_tab, discarded};

  registry_.AdoptRecoveryPlan(plan, ProfileId{"profile_default"},
                              BrowserWindowId{"window_1"});

  EXPECT_EQ(2u, registry_.tab_count());
  EXPECT_EQ(TabId{"tab_user"}, registry_.active_tab_id());
  ASSERT_TRUE(registry_.FindTab(TabId{"tab_assistant"}));
  EXPECT_EQ(TabOwnership::kAssistant,
            registry_.FindTab(TabId{"tab_assistant"})->ownership);
  EXPECT_FALSE(registry_.FindTab(TabId{"tab_gone"}));
}

}  // namespace
}  // namespace taffy
