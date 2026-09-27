// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/download_command_router.h"

#include <string>
#include <vector>

#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

// The three rules the router owns: the state matrix is applied, the assistant
// is refused, and a retry is re-decided rather than repeated.

namespace taffy {
namespace {

class RecordingDelegate : public DownloadCommandDelegate {
 public:
  bool Pause(uint32_t download_id) override {
    calls.push_back("pause");
    return !refuse;
  }
  bool Resume(uint32_t download_id, bool user_gesture) override {
    calls.push_back(user_gesture ? "resume-user" : "resume-automatic");
    return !refuse;
  }
  bool Cancel(uint32_t download_id) override {
    calls.push_back("cancel");
    return !refuse;
  }
  bool OpenWhenComplete(uint32_t download_id) override {
    calls.push_back("open-when-complete");
    return !refuse;
  }
  bool OpenNow(uint32_t download_id) override {
    calls.push_back("open-now");
    return !refuse;
  }
  bool Retry(const DownloadRecord& record) override {
    calls.push_back("retry");
    return !refuse;
  }

  std::vector<std::string> calls;
  bool refuse = false;
};

class DownloadCommandRouterTest : public testing::Test {
 protected:
  void SetUp() override { router_.SetDelegate(&delegate_); }
  void TearDown() override { router_.SetDelegate(nullptr); }

  DownloadRecord Record(uint32_t id, DownloadState state) {
    DownloadRecord record;
    record.download_id = id;
    record.tab_id = TabId{"tab_1"};
    record.initiator = NavigationInitiator::kUser;
    record.state = state;
    record.target_file_name = "report.pdf";
    record.total_bytes = 4096;
    record.is_resumable = true;
    return record;
  }

  content::BrowserTaskEnvironment task_environment_;
  RecordingDelegate delegate_;
  DownloadCommandRouter router_;
};

TEST_F(DownloadCommandRouterTest, AUserCancelReachesTheDownloadSystem) {
  router_.NoteDownloadChanged(Record(1, DownloadState::kInProgress));

  EXPECT_EQ(DownloadCommandResult::kExecuted,
            router_.Execute(1, DownloadCommand::kCancel,
                            DownloadCommandOrigin::kUserGesture));
  ASSERT_EQ(1u, delegate_.calls.size());
  EXPECT_EQ("cancel", delegate_.calls[0]);
}

TEST_F(DownloadCommandRouterTest, TheRecordIsNotEditedByACommand) {
  router_.NoteDownloadChanged(Record(1, DownloadState::kInProgress));
  ASSERT_EQ(DownloadCommandResult::kExecuted,
            router_.Execute(1, DownloadCommand::kCancel,
                            DownloadCommandOrigin::kUserGesture));

  // Whether the state actually changed is the download system's answer, and it
  // arrives through NoteDownloadChanged. A router that wrote the state it
  // expected would be a second source of truth about a file on disk.
  ASSERT_TRUE(router_.Find(1));
  EXPECT_EQ(DownloadState::kInProgress, router_.Find(1)->state);

  router_.NoteDownloadChanged(Record(1, DownloadState::kCancelled));
  EXPECT_EQ(DownloadState::kCancelled, router_.Find(1)->state);
}

TEST_F(DownloadCommandRouterTest, TheAssistantControlsNoDownload) {
  router_.NoteDownloadChanged(Record(1, DownloadState::kInProgress));

  for (DownloadCommand command :
       {DownloadCommand::kPause, DownloadCommand::kCancel,
        DownloadCommand::kOpenWhenComplete}) {
    EXPECT_EQ(DownloadCommandResult::kRefusedAssistantInitiated,
              router_.Execute(1, command, DownloadCommandOrigin::kAssistantTask))
        << "command " << static_cast<int>(command);
  }
  EXPECT_TRUE(delegate_.calls.empty());
}

TEST_F(DownloadCommandRouterTest, AnUnattributedCommandIsRefused) {
  router_.NoteDownloadChanged(Record(1, DownloadState::kInProgress));

  EXPECT_EQ(DownloadCommandResult::kRefusedUnattributedOrigin,
            router_.Execute(1, DownloadCommand::kCancel,
                            DownloadCommandOrigin::kUnknown));
  EXPECT_TRUE(delegate_.calls.empty());
}

TEST_F(DownloadCommandRouterTest, AnIllegalCommandNeverReachesTheDelegate) {
  router_.NoteDownloadChanged(Record(1, DownloadState::kComplete));

  EXPECT_EQ(DownloadCommandResult::kRefusedIllegalInState,
            router_.Execute(1, DownloadCommand::kCancel,
                            DownloadCommandOrigin::kUserGesture));
  EXPECT_TRUE(delegate_.calls.empty());
}

TEST_F(DownloadCommandRouterTest, ADangerousFileIsNotOpenedWithoutConfirming) {
  DownloadRecord record = Record(1, DownloadState::kComplete);
  record.requires_danger_confirmation = true;
  router_.NoteDownloadChanged(record);

  EXPECT_EQ(DownloadCommandResult::kRefusedNeedsDangerConfirmation,
            router_.Execute(1, DownloadCommand::kOpenNow,
                            DownloadCommandOrigin::kUserGesture));
  EXPECT_TRUE(delegate_.calls.empty());
}

TEST_F(DownloadCommandRouterTest, AUserRetryIsReDecidedAndAllowed) {
  router_.NoteDownloadChanged(Record(1, DownloadState::kInterrupted));

  // The retry goes back through the download and external-intent seam exactly
  // as the original download did, and a user-attributed retry passes it.
  EXPECT_EQ(DownloadCommandResult::kExecuted,
            router_.Execute(1, DownloadCommand::kRetry,
                            DownloadCommandOrigin::kUserGesture));
  ASSERT_EQ(1u, delegate_.calls.size());
  EXPECT_EQ("retry", delegate_.calls[0]);
}

TEST_F(DownloadCommandRouterTest,
       ABrowserRetryOfAnUnattributedDownloadIsRefused) {
  // The laundering case. A download whose initiator could not be attributed
  // must not become an allowed request by being retried from a browser
  // context that carries no user gesture.
  DownloadRecord record = Record(1, DownloadState::kInterrupted);
  record.initiator = NavigationInitiator::kUnknown;
  router_.NoteDownloadChanged(record);

  EXPECT_EQ(DownloadCommandResult::kRefusedByIntentRouter,
            router_.Execute(1, DownloadCommand::kRetry,
                            DownloadCommandOrigin::kBrowser));
  EXPECT_TRUE(delegate_.calls.empty());
}

TEST_F(DownloadCommandRouterTest, AUserGestureIsForwardedToResume) {
  router_.NoteDownloadChanged(Record(1, DownloadState::kPaused));

  ASSERT_EQ(DownloadCommandResult::kExecuted,
            router_.Execute(1, DownloadCommand::kResume,
                            DownloadCommandOrigin::kUserGesture));
  // Chromium's Resume takes the gesture because a user-driven resume is
  // allowed to do things an automatic one is not.
  EXPECT_EQ("resume-user", delegate_.calls[0]);
}

TEST_F(DownloadCommandRouterTest, APlatformRefusalIsReportedAsOne) {
  router_.NoteDownloadChanged(Record(1, DownloadState::kInProgress));
  delegate_.refuse = true;

  EXPECT_EQ(DownloadCommandResult::kPlatformRefused,
            router_.Execute(1, DownloadCommand::kCancel,
                            DownloadCommandOrigin::kUserGesture));
}

TEST_F(DownloadCommandRouterTest, CommandsWithoutADelegateAreRefused) {
  router_.NoteDownloadChanged(Record(1, DownloadState::kInProgress));
  router_.SetDelegate(nullptr);

  EXPECT_EQ(DownloadCommandResult::kRefusedNoDelegate,
            router_.Execute(1, DownloadCommand::kCancel,
                            DownloadCommandOrigin::kUserGesture));
}

TEST_F(DownloadCommandRouterTest, UnknownDownloadsAreRefusedNotCreated) {
  EXPECT_EQ(DownloadCommandResult::kRefusedUnknownDownload,
            router_.Execute(99, DownloadCommand::kCancel,
                            DownloadCommandOrigin::kUserGesture));
  EXPECT_EQ(0u, router_.record_count());
}

TEST_F(DownloadCommandRouterTest, RecordsAreListedPerTabAndDroppedOnRemoval) {
  router_.NoteDownloadChanged(Record(1, DownloadState::kInProgress));
  DownloadRecord other_tab = Record(2, DownloadState::kInProgress);
  other_tab.tab_id = TabId{"tab_2"};
  router_.NoteDownloadChanged(other_tab);

  EXPECT_EQ(1u, router_.ListForTab(TabId{"tab_1"}).size());
  EXPECT_EQ(1u, router_.ListForTab(TabId{"tab_2"}).size());
  EXPECT_EQ(0u, router_.ListForTab(TabId{"tab_3"}).size());

  router_.NoteDownloadRemoved(2);
  EXPECT_EQ(1u, router_.record_count());
  EXPECT_FALSE(router_.Find(2));
}

TEST_F(DownloadCommandRouterTest, DuplicateNamesAreRecordedNotComputed) {
  // Chromium uniquifies. The record says which of the three outcomes happened
  // so a parity test can assert on it; nothing in this component picks a name.
  DownloadRecord first = Record(1, DownloadState::kComplete);
  first.target_file_name = "report.pdf";
  first.duplicate_resolution = DuplicateResolution::kNotApplicable;
  router_.NoteDownloadChanged(first);

  DownloadRecord second = Record(2, DownloadState::kComplete);
  second.target_file_name = "report (1).pdf";
  second.duplicate_resolution = DuplicateResolution::kUniquifiedByBrowser;
  router_.NoteDownloadChanged(second);

  EXPECT_EQ(DuplicateResolution::kUniquifiedByBrowser,
            router_.Find(2)->duplicate_resolution);
  EXPECT_NE(router_.Find(1)->target_file_name,
            router_.Find(2)->target_file_name);
}

}  // namespace
}  // namespace taffy
