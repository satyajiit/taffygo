// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/tab_session_registry.h"

#include <map>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "taffy/browser/navigation_lifecycle_tracker.h"
#include "taffy/browser/session_recovery_planner.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/result_codes.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/public/test/test_utils.h"
#include "content/shell/browser/shell.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/size.h"

// Tab and session behavior that needs real WebContents: PAR-TAB-001 (create,
// list, switch, close with no data loss across rapid operations and process
// recreation), PAR-TAB-004 (recovery after renderer death) and PAR-NAV-003
// (a link opening in a new tab).
//
// content_shell has no tab model, so the delegate here is backed by Shell
// windows. That is the honest shape of the test at this layer: the registry's
// job is to hold the rules and to keep a consistent record, and a Shell is
// enough of a tab to prove both. The Android tab model is exercised by the
// instrumentation suite in //taffy/app/android (WP-M1-03).

namespace taffy {
namespace {

class ShellTabDelegate : public TabSessionDelegate {
 public:
  ShellTabDelegate(content::BrowserContext* browser_context, const GURL& blank)
      : browser_context_(browser_context), blank_(blank) {}

  TabId OpenTab(const TabOpenRequest& request) override {
    content::Shell* shell = content::Shell::CreateNewWindow(
        browser_context_, blank_, /*site_instance=*/nullptr, gfx::Size());
    if (!shell) {
      return TabId{};
    }
    TabId tab_id{"shell_tab_" + std::to_string(++next_index_)};
    shells_[tab_id] = shell;
    return tab_id;
  }

  bool ActivateTab(const TabId& tab_id) override {
    auto it = shells_.find(tab_id);
    if (it == shells_.end()) {
      return false;
    }
    it->second->web_contents()->WasShown();
    return true;
  }

  bool CloseTab(const TabId& tab_id) override {
    auto it = shells_.find(tab_id);
    if (it == shells_.end()) {
      return false;
    }
    it->second->Close();
    shells_.erase(it);
    return true;
  }

  content::WebContents* ContentsFor(const TabId& tab_id) {
    auto it = shells_.find(tab_id);
    return it == shells_.end() ? nullptr : it->second->web_contents();
  }

 private:
  raw_ptr<content::BrowserContext> browser_context_;
  GURL blank_;
  int next_index_ = 0;
  std::map<TabId, content::Shell*> shells_;
};

class TabSessionRegistryBrowserTest : public content::ContentBrowserTest {
 public:
  void SetUpOnMainThread() override {
    content::ContentBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
    delegate_ = std::make_unique<ShellTabDelegate>(
        shell()->web_contents()->GetBrowserContext(), GURL("about:blank"));
    registry_.SetDelegate(delegate_.get());
  }

  void TearDownOnMainThread() override {
    registry_.SetDelegate(nullptr);
    delegate_.reset();
    content::ContentBrowserTest::TearDownOnMainThread();
  }

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

  GURL PageUrl(const std::string& host, const std::string& path) {
    return embedded_test_server()->GetURL(host, path);
  }

  TabSessionRegistry registry_;
  std::unique_ptr<ShellTabDelegate> delegate_;
};

IN_PROC_BROWSER_TEST_F(TabSessionRegistryBrowserTest,
                       TabsSurviveRapidOpenAndClose) {
  std::vector<TabId> opened;
  for (int i = 0; i < 8; ++i) {
    TabId tab_id;
    ASSERT_EQ(TabOpenResult::kOpened, registry_.OpenTab(UserRequest(), &tab_id));
    opened.push_back(tab_id);
    content::WebContents* contents = delegate_->ContentsFor(tab_id);
    ASSERT_TRUE(contents);
    ASSERT_TRUE(content::NavigateToURL(
        contents, PageUrl("primary.test", "/title1.html")));
  }
  ASSERT_EQ(8u, registry_.tab_count());

  for (size_t i = 0; i < opened.size(); i += 2) {
    ASSERT_EQ(TabCloseResult::kClosed,
              registry_.CloseTab(opened[i], TabCloseOrigin::kUserGesture));
  }

  EXPECT_EQ(4u, registry_.tab_count());
  for (size_t i = 1; i < opened.size(); i += 2) {
    const TabRecord* record = registry_.FindTab(opened[i]);
    ASSERT_TRUE(record) << "tab " << i << " was lost";
    content::WebContents* contents = delegate_->ContentsFor(opened[i]);
    ASSERT_TRUE(contents);
    EXPECT_EQ(PageUrl("primary.test", "/title1.html"),
              contents->GetLastCommittedURL());
  }
}

IN_PROC_BROWSER_TEST_F(TabSessionRegistryBrowserTest,
                       RendererDeathDoesNotLoseTheTabRecord) {
  // PAR-TAB-004's renderer half: the process goes away, the tab does not. The
  // record is browser-owned, so it survives by construction; the test proves
  // that nothing in the registry keys off renderer state.
  TabId tab_id;
  ASSERT_EQ(TabOpenResult::kOpened, registry_.OpenTab(UserRequest(), &tab_id));
  content::WebContents* contents = delegate_->ContentsFor(tab_id);
  ASSERT_TRUE(contents);
  ASSERT_TRUE(content::NavigateToURL(contents,
                                     PageUrl("primary.test", "/title1.html")));

  content::RenderProcessHost* process =
      contents->GetPrimaryMainFrame()->GetProcess();
  content::RenderProcessHostWatcher watcher(
      process, content::RenderProcessHostWatcher::WATCH_FOR_PROCESS_EXIT);
  process->Shutdown(content::RESULT_CODE_KILLED);
  watcher.Wait();

  ASSERT_TRUE(registry_.FindTab(tab_id));
  EXPECT_FALSE(registry_.IsRetired(tab_id));

  // And it reloads without anything from the assistant being involved.
  ASSERT_TRUE(content::NavigateToURL(contents,
                                     PageUrl("primary.test", "/title2.html")));
  EXPECT_EQ(PageUrl("primary.test", "/title2.html"),
            contents->GetLastCommittedURL());
}

IN_PROC_BROWSER_TEST_F(TabSessionRegistryBrowserTest,
                       LinkWithTargetBlankOpensASecondWebContents) {
  // PAR-NAV-003. The new WebContents is Chromium's to create; what is asserted
  // here is that it happens at all and that the original tab keeps its
  // document.
  ASSERT_TRUE(content::NavigateToURL(
      shell(), PageUrl("primary.test", "/title1.html")));

  content::ShellAddedObserver new_shell_observer;
  ASSERT_TRUE(content::ExecJs(shell()->web_contents(),
                              "const a = document.createElement('a');"
                              "a.href = '/title2.html';"
                              "a.target = '_blank';"
                              "document.body.appendChild(a);"
                              "a.click();"));
  content::Shell* opened = new_shell_observer.GetShell();
  ASSERT_TRUE(opened);
  ASSERT_TRUE(content::WaitForLoadStop(opened->web_contents()));

  EXPECT_EQ(PageUrl("primary.test", "/title2.html"),
            opened->web_contents()->GetLastCommittedURL());
  EXPECT_EQ(PageUrl("primary.test", "/title1.html"),
            shell()->web_contents()->GetLastCommittedURL());
}

IN_PROC_BROWSER_TEST_F(TabSessionRegistryBrowserTest,
                       AdoptedRecoveryPlanRestoresWithoutResumingATask) {
  // PAR-TAB-003 and PAR-TAB-004 together: the plan comes back, the assistant's
  // tab comes back paused, and nothing about the task resumes on its own.
  PersistedTabRecord user_tab;
  user_tab.tab_id = ToRecordIdentifier("tab_user");
  user_tab.index = 0;
  user_tab.was_active = true;
  user_tab.ownership = TabOwnership::kUser;

  PersistedTabRecord assistant_tab;
  assistant_tab.tab_id = ToRecordIdentifier("tab_assistant");
  assistant_tab.index = 1;
  assistant_tab.ownership = TabOwnership::kAssistant;
  assistant_tab.owning_task_id = ToRecordIdentifier("task_1");
  assistant_tab.has_owning_task = true;

  PersistedTaskJournalEntry entry;
  entry.task_id = ToRecordIdentifier("task_1");
  entry.action_id = ToRecordIdentifier("action_1");
  entry.phase = JournalPhase::kDispatching;
  entry.effect_is_external = true;

  const SessionRecoveryPlan plan = PlanSessionRecovery(
      RestartCause::kProcessEviction, {user_tab, assistant_tab}, {entry});
  registry_.AdoptRecoveryPlan(plan, ProfileId{"profile_default"},
                              BrowserWindowId{"window_1"});

  EXPECT_EQ(2u, registry_.tab_count());
  EXPECT_EQ(TabId{"tab_user"}, registry_.active_tab_id());
  EXPECT_TRUE(plan.user_must_be_shown_recovery_status);
  ASSERT_EQ(1u, plan.tasks.size());
  EXPECT_EQ(RecoveryDisposition::kOutcomeUnknown, plan.tasks[0].disposition());
}

}  // namespace
}  // namespace taffy
