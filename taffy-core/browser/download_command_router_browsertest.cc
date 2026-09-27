// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/download_command_router.h"

#include <memory>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "components/download/public/common/download_item.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/public/test/download_test_observer.h"
#include "content/shell/browser/shell.h"
#include "content/shell/browser/shell_download_manager_delegate.h"
#include "net/dns/mock_host_resolver.h"
#include "net/http/http_status_code.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gtest/include/gtest/gtest.h"

// PAR-FILE-001 and CAP-DL-001 against Chromium's real download system:
// destination, progress, cancel, duplicate filename and failure behavior.
//
// The point of running these against the real system rather than a fake is
// that the parity row is about *Chromium's* behavior being preserved. A fake
// download manager would prove only that the router calls the methods it says
// it calls, which the unit test already proves.
//
// VERIFY AT SP-01: the download test scaffolding at the pinned milestone —
// content::DownloadTestObserverTerminal from
// content/public/test/download_test_observer.h, BrowserContext's download
// manager accessor, and download::DownloadItem's state and target accessors
// from components/download/public/common/download_item.h. If content_shell no
// longer carries a download manager delegate that can complete a download,
// this file moves to the Android instrumentation suite in
// //taffy/app/android, which has a real one.

namespace taffy {
namespace {

constexpr char kAttachmentPath[] = "/taffy-attachment";
constexpr char kAttachmentBody[] = "TaffyGo download parity fixture body.";

std::unique_ptr<net::test_server::HttpResponse> ServeAttachment(
    const net::test_server::HttpRequest& request) {
  if (request.relative_url.find(kAttachmentPath) != 0) {
    return nullptr;
  }
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_code(net::HTTP_OK);
  response->set_content_type("application/octet-stream");
  response->AddCustomHeader("Content-Disposition",
                            "attachment; filename=\"report.pdf\"");
  response->set_content(kAttachmentBody);
  return response;
}

// Projects a live download::DownloadItem onto the record the router holds.
// This is the only translation in the file, and it is deliberately mechanical:
// every field is copied, none is computed.
DownloadRecord RecordFor(download::DownloadItem* item, const TabId& tab_id) {
  DownloadRecord record;
  record.download_id = item->GetId();
  record.tab_id = tab_id;
  record.initiator = NavigationInitiator::kUser;
  record.target_file_name = item->GetTargetFilePath().BaseName().AsUTF8Unsafe();
  record.received_bytes = item->GetReceivedBytes();
  record.total_bytes = item->GetTotalBytes() > 0 ? item->GetTotalBytes() : -1;
  record.is_resumable = item->CanResume();
  record.destination = DownloadDestinationKind::kDefaultDownloadsDirectory;

  switch (item->GetState()) {
    case download::DownloadItem::IN_PROGRESS:
      record.state = item->IsPaused() ? DownloadState::kPaused
                                      : DownloadState::kInProgress;
      break;
    case download::DownloadItem::COMPLETE:
      record.state = DownloadState::kComplete;
      break;
    case download::DownloadItem::CANCELLED:
      record.state = DownloadState::kCancelled;
      record.failure = DownloadFailureClass::kCancelledByUser;
      break;
    case download::DownloadItem::INTERRUPTED:
      record.state = DownloadState::kInterrupted;
      record.failure = DownloadFailureClass::kUnknown;
      break;
    case download::DownloadItem::MAX_DOWNLOAD_STATE:
      record.state = DownloadState::kCreated;
      break;
  }
  return record;
}

// A delegate over the real download manager. Every method performs; none
// decides.
class ChromiumDownloadDelegate : public DownloadCommandDelegate {
 public:
  explicit ChromiumDownloadDelegate(content::DownloadManager* manager)
      : manager_(manager) {}

  bool Pause(uint32_t download_id) override {
    download::DownloadItem* item = Item(download_id);
    if (!item) {
      return false;
    }
    item->Pause();
    return true;
  }
  bool Resume(uint32_t download_id, bool user_gesture) override {
    download::DownloadItem* item = Item(download_id);
    if (!item) {
      return false;
    }
    item->Resume(user_gesture);
    return true;
  }
  bool Cancel(uint32_t download_id) override {
    download::DownloadItem* item = Item(download_id);
    if (!item) {
      return false;
    }
    item->Cancel(/*user_cancel=*/true);
    return true;
  }
  bool OpenWhenComplete(uint32_t download_id) override {
    download::DownloadItem* item = Item(download_id);
    if (!item) {
      return false;
    }
    item->SetOpenWhenComplete(true);
    return true;
  }
  bool OpenNow(uint32_t download_id) override {
    // Deliberately not implemented in a test: opening a file hands it to the
    // platform, which is not something a browser test should do. The command's
    // refusal rules are covered by the unit test.
    return false;
  }
  bool Retry(const DownloadRecord& record) override {
    retry_calls++;
    return true;
  }

  download::DownloadItem* Item(uint32_t download_id) {
    // DownloadManager::DownloadVector, not a plain vector of pointers: at the
    // pinned milestone the element type is raw_ptr<DownloadItem,
    // VectorExperimental>, and GetAllDownloads takes that exact type.
    content::DownloadManager::DownloadVector items;
    manager_->GetAllDownloads(&items);
    for (download::DownloadItem* item : items) {
      if (item->GetId() == download_id) {
        return item;
      }
    }
    return nullptr;
  }

  int retry_calls = 0;

 private:
  raw_ptr<content::DownloadManager> manager_;
};

class DownloadCommandRouterBrowserTest : public content::ContentBrowserTest {
 public:
  void SetUpOnMainThread() override {
    content::ContentBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating(&ServeAttachment));
    ASSERT_TRUE(embedded_test_server()->Start());

    manager_ = shell()->web_contents()->GetBrowserContext()->GetDownloadManager();
    ASSERT_TRUE(manager_);

    // Give the shell somewhere to put the file. ShellDownloadManagerDelegate
    // has no target-path picker — ChooseDownloadPath() is NOTIMPLEMENTED —
    // so without a default directory every download in this fixture is
    // cancelled at target determination and reaches CANCELLED rather than
    // COMPLETE. That is the shell being a shell, not Chromium's download
    // system misbehaving, and the parity rows here are about the latter.
    // Upstream sets it up identically
    // (content/browser/navigation_browsertest.cc,
    // NavigationDownloadBrowserTest).
    ASSERT_TRUE(downloads_directory_.CreateUniqueTempDir());
    static_cast<content::ShellDownloadManagerDelegate*>(
        shell()->web_contents()->GetBrowserContext()
            ->GetDownloadManagerDelegate())
        ->SetDownloadBehaviorForTesting(downloads_directory_.GetPath());
    delegate_ = std::make_unique<ChromiumDownloadDelegate>(manager_);
    router_.SetDelegate(delegate_.get());
  }

  void TearDownOnMainThread() override {
    router_.SetDelegate(nullptr);
    delegate_.reset();
    content::ContentBrowserTest::TearDownOnMainThread();
  }

 protected:
  // Starts a download and waits for it to reach a terminal state.
  content::DownloadManager::DownloadVector DownloadOnce() {
    content::DownloadTestObserverTerminal observer(
        manager_, /*wait_count=*/1,
        content::DownloadTestObserverTerminal::ON_DANGEROUS_DOWNLOAD_FAIL);
    // NavigateToURL reports failure for a navigation that turned into a
    // download, which is the expected outcome here.
    std::ignore = content::NavigateToURL(
        shell(), embedded_test_server()->GetURL("primary.test",
                                                kAttachmentPath));
    observer.WaitForFinished();

    content::DownloadManager::DownloadVector items;
    manager_->GetAllDownloads(&items);
    return items;
  }

  raw_ptr<content::DownloadManager> manager_ = nullptr;
  std::unique_ptr<ChromiumDownloadDelegate> delegate_;
  DownloadCommandRouter router_;
  base::ScopedTempDir downloads_directory_;
};

IN_PROC_BROWSER_TEST_F(DownloadCommandRouterBrowserTest,
                       AUserInitiatedDownloadCompletesWithADestination) {
  content::DownloadManager::DownloadVector items = DownloadOnce();
  ASSERT_EQ(1u, items.size());
  ASSERT_EQ(download::DownloadItem::COMPLETE, items[0]->GetState());

  const DownloadRecord record = RecordFor(items[0], TabId{"tab_1"});
  router_.NoteDownloadChanged(record);

  EXPECT_EQ(DownloadState::kComplete, record.state);
  EXPECT_EQ("report.pdf", record.target_file_name);
  EXPECT_EQ(static_cast<int64_t>(sizeof(kAttachmentBody) - 1),
            record.received_bytes);
  EXPECT_TRUE(DownloadIsFinished(record));
  ASSERT_TRUE(router_.Find(record.download_id));
}

// PAR-FILE-001's duplicate filename half, at the seam TaffyGo actually owns:
// two downloads that share a name are two router entries, and the router
// renames neither.
//
// WHAT THIS CASE ASSERTED UNTIL 2026-08-21, AND WHY THAT WAS WRONG. It was
// called ASecondDownloadOfTheSameNameIsUniquifiedByChromium and required the
// two target basenames to differ. The premise it stated is right —
// "TaffyGo contributes no naming logic of its own" — but the assertion it drew
// from it is about the other party, and the other party in this binary is not
// the one the product ships:
//
//   * Uniquifying is the embedder's target-determination policy, chosen behind
//     content::DownloadManagerDelegate::DetermineDownloadTarget, not a
//     content-layer guarantee.
//   * The product is a downstream full-Chromium //chrome Android build, so its
//     delegate is ChromeDownloadManagerDelegate, which runs
//     DownloadTargetDeterminer and reserves the path with
//     DownloadPathReservationTracker::UNIQUIFY.
//   * This binary links //content/shell:content_shell_lib, so its delegate is
//     the one SetUpOnMainThread casts to above: ShellDownloadManagerDelegate,
//     whose GenerateFilename() appends net::GenerateFileName's result to the
//     download directory with no reservation and no uniquifier. Two downloads
//     of report.pdf therefore get one path, always, and the old assertion could
//     never have passed here — not because the router regressed, but because
//     the assertion was never about the router.
//
// Installing a uniquifying delegate to make it pass would assert that
// upstream's path-reservation tracker works, which upstream's own unit test
// asserts, and would then be incapable of ever failing for a TaffyGo reason.
// So the assertion is moved onto the class this file exists to test. The
// embedder-policy half of the row is device-matrix work, over the product's
// real delegate; parity/download_lifecycle_browsertest.cc carries the same
// argument for the content-layer half.
IN_PROC_BROWSER_TEST_F(DownloadCommandRouterBrowserTest,
                       TwoDownloadsOfTheSameNameAreTwoRouterEntries) {
  ASSERT_EQ(1u, DownloadOnce().size());
  content::DownloadManager::DownloadVector items = DownloadOnce();
  ASSERT_EQ(2u, items.size());
  ASSERT_NE(items[0]->GetId(), items[1]->GetId());

  // The premise, pinned rather than assumed: under this binary's delegate the
  // two downloads really do share a name, so what follows is tested against the
  // hard case rather than against two names that happened to differ.
  const std::string first =
      items[0]->GetTargetFilePath().BaseName().AsUTF8Unsafe();
  const std::string second =
      items[1]->GetTargetFilePath().BaseName().AsUTF8Unsafe();
  ASSERT_EQ(first, second)
      << "content_shell's delegate reused a name until now; if it no longer "
         "does, this binary gained a real target determiner and the "
         "uniquification assertion belongs back in this file.";

  const DownloadRecord first_record = RecordFor(items[0], TabId{"tab_1"});
  const DownloadRecord second_record = RecordFor(items[1], TabId{"tab_1"});
  router_.NoteDownloadChanged(first_record);
  router_.NoteDownloadChanged(second_record);

  // Keyed on the download id, never on the name. A router that collapsed two
  // same-named downloads into one entry would lose a download from every
  // surface that reads it, and would refuse a command for the one it dropped.
  EXPECT_EQ(2u, router_.record_count());
  const DownloadRecord* held_first = router_.Find(first_record.download_id);
  const DownloadRecord* held_second = router_.Find(second_record.download_id);
  ASSERT_TRUE(held_first);
  ASSERT_TRUE(held_second);
  EXPECT_EQ(first_record.download_id, held_first->download_id);
  EXPECT_EQ(second_record.download_id, held_second->download_id);
  EXPECT_EQ(2u, router_.ListForTab(TabId{"tab_1"}).size());

  // And no naming logic of its own: what the router holds is the name Chromium
  // settled on, character for character, for each of the two.
  EXPECT_EQ(first, held_first->target_file_name);
  EXPECT_EQ(second, held_second->target_file_name);
}

IN_PROC_BROWSER_TEST_F(DownloadCommandRouterBrowserTest,
                       CancelIsRefusedOnceTheDownloadHasFinished) {
  content::DownloadManager::DownloadVector items = DownloadOnce();
  ASSERT_EQ(1u, items.size());
  router_.NoteDownloadChanged(RecordFor(items[0], TabId{"tab_1"}));

  EXPECT_EQ(DownloadCommandResult::kRefusedIllegalInState,
            router_.Execute(items[0]->GetId(), DownloadCommand::kCancel,
                            DownloadCommandOrigin::kUserGesture));
  // The file is still there. A refusal is a refusal, not a rollback.
  EXPECT_EQ(download::DownloadItem::COMPLETE, items[0]->GetState());
}

IN_PROC_BROWSER_TEST_F(DownloadCommandRouterBrowserTest,
                       TheAssistantCannotControlARealDownload) {
  content::DownloadManager::DownloadVector items = DownloadOnce();
  ASSERT_EQ(1u, items.size());
  router_.NoteDownloadChanged(RecordFor(items[0], TabId{"tab_1"}));

  EXPECT_EQ(DownloadCommandResult::kRefusedAssistantInitiated,
            router_.Execute(items[0]->GetId(), DownloadCommand::kOpenNow,
                            DownloadCommandOrigin::kAssistantTask));
  EXPECT_EQ(0, delegate_->retry_calls);
}

}  // namespace
}  // namespace taffy
