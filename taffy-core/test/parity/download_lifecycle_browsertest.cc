// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/json/string_escape.h"
#include "base/threading/thread_restrictions.h"
#include "components/download/public/common/download_interrupt_reasons.h"
#include "components/download/public/common/download_item.h"
#include "taffy/browser/download_record.h"
#include "taffy/browser/download_state_machine.h"
#include "taffy/test/support/taffy_browser_test_base.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/download_manager_delegate.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/public/test/download_test_observer.h"
#include "content/public/test/slow_download_http_response.h"
#include "content/shell/browser/shell.h"
#include "content/shell/browser/shell_download_manager_delegate.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

// The user-initiated download lifecycle over the corpus (PAR-FILE-001).
//
// The corpus's download endpoint always serves the same bytes and lets the
// query change only the offered filename. That is what makes the interesting
// cases reachable without a second file on disk: a duplicate name, a name that
// disagrees with the content type, and a name with path separators in it.
//
// The state machine one directory up is already tested exhaustively without a
// browser. What is here is the half that needs Chromium's own download system:
// that a user-initiated download actually completes, that the file lands where
// the browser put it, and that a second download of the same name is still a
// whole second download.
//
// Where the duplicate-name row stops here: whether that second download gets a
// *different* path is the embedder's target-determination policy, and the
// embedder in this binary is content_shell rather than the product. The case
// below says so at length; the short version is that content_shell's delegate
// does no path reservation, the product's does, and no content_shell binary can
// show the product's answer.
//
// The assistant appears nowhere in this file, and that is the point of
// PAR-FILE-001 at this milestone: downloads are the user's, and the download
// command seam refuses an assistant-attributed request before anything else
// happens. The refusal itself is proved without a browser, one directory up.

namespace taffy::test {
namespace {

class ParityDownloadTest : public TaffyBrowserTestBase {
 public:
  void SetUpOnMainThread() override {
    TaffyBrowserTestBase::SetUpOnMainThread();
    ASSERT_TRUE(downloads_.CreateUniqueTempDir());
    // VERIFY AT SP-01: how a content_shell browser test sets the download
    // directory at the pinned milestone. Upstream files to read:
    // content/shell/browser/shell_download_manager_delegate.h and
    // content/public/test/download_test_observer.h. If content_shell has no
    // usable download delegate in this build, this whole file moves to the
    // product instrumentation suite rather than being weakened into a fake:
    // the row is about Chromium's download system being preserved, and a fake
    // cannot show that.
    content::DownloadManager* manager =
        web_contents()->GetBrowserContext()->GetDownloadManager();
    ASSERT_TRUE(manager)
        << "No download manager in this test binary. PAR-FILE-001 is about "
           "Chromium's own download system; there is nothing here to test "
           "without it.";

    // Give the shell somewhere to put the file, and tell it not to prompt.
    // ShellDownloadManagerDelegate has no target-path picker outside Windows —
    // ChooseDownloadPath() is NOTIMPLEMENTED and answers with an empty path —
    // so without this every download in this fixture reaches a terminal state
    // with no target file and zero bytes received. That is the shell being a
    // shell, not Chromium's download system misbehaving, and this row is about
    // the latter. Upstream does the same thing
    // (content/browser/navigation_browsertest.cc), and so does
    // DownloadCommandRouterBrowserTest one directory up, which is why that
    // suite completes real downloads and this one did not.
    content::DownloadManagerDelegate* delegate =
        web_contents()->GetBrowserContext()->GetDownloadManagerDelegate();
    ASSERT_TRUE(delegate);
    static_cast<content::ShellDownloadManagerDelegate*>(delegate)
        ->SetDownloadBehaviorForTesting(downloads_.GetPath());

    // A second server, separate from the corpus map, serving one download that
    // does not finish until the test lets it. The corpus endpoint serves a
    // small file that completes before a test body can do anything to it, and
    // there is no way to assert anything about an in-progress download that is
    // already finished.
    slow_server_.RegisterRequestHandler(base::BindRepeating(
        &content::SlowDownloadHttpResponse::HandleSlowDownloadRequest));
    ASSERT_TRUE(slow_server_.Start());
  }

 protected:
  // Runs one download and waits for it to reach a terminal state.
  void DownloadAndWait(const GURL& url) {
    content::DownloadTestObserverTerminal observer(
        web_contents()->GetBrowserContext()->GetDownloadManager(),
        /*wait_count=*/1,
        content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_FAIL);
    // A download is a navigation that does not commit, so the ordinary
    // navigate-and-wait helpers cannot be used here.
    ASSERT_TRUE(content::ExecJs(
        web_contents(), "location.href = " + base::GetQuotedJSONString(url.spec())));
    observer.WaitForFinished();
  }

  content::DownloadManager* download_manager() {
    return web_contents()->GetBrowserContext()->GetDownloadManager();
  }

  base::ScopedTempDir downloads_;
  net::EmbeddedTestServer slow_server_;
};

// PAR-FILE-001. The ordinary case: a user-initiated download completes and the
// bytes on disk are the bytes the corpus serves.
IN_PROC_BROWSER_TEST_F(ParityDownloadTest, UserInitiatedDownloadCompletes) {
  ASSERT_TRUE(NavigateToFixture("download-and-upload"));
  DownloadAndWait(OriginUrl("primary", "/files/download?name=product-specs.csv"));

  content::DownloadManager::DownloadVector items;
  web_contents()->GetBrowserContext()->GetDownloadManager()->GetAllDownloads(
      &items);
  ASSERT_EQ(1u, items.size());
  EXPECT_EQ(download::DownloadItem::COMPLETE, items.front()->GetState());
  EXPECT_GT(items.front()->GetReceivedBytes(), 0);
}

// PAR-FILE-001, the duplicate-filename row's content-layer half. A second
// download that offers a filename already on disk is still a whole second
// download: it is not refused, not folded into the first, and not truncated
// because a file of that name is in the way.
//
// WHAT THIS CASE ASSERTED UNTIL 2026-08-21, AND WHY THAT WAS WRONG.
// It required the two target paths to differ, on the premise that "Chromium"
// uniquifies a duplicate name. Chromium does — but not in this binary, and the
// distinction is the whole of the correction:
//
//   * Target determination is the *embedder's* job, reached through
//     content::DownloadManagerDelegate::DetermineDownloadTarget. Uniquifying is
//     a policy the embedder chooses, not a content-layer guarantee.
//   * The product is a downstream full-Chromium //chrome Android build (parity
//     matrix section 3, and //taffy:taffy_public_apk is declared
//     through chrome_public_apk_or_module_tmpl). Its delegate is
//     ChromeDownloadManagerDelegate, which runs DownloadTargetDeterminer and
//     reserves the path with DownloadPathReservationTracker::UNIQUIFY. There
//     the old assertion is true.
//   * This binary is content_shell. Its delegate is
//     ShellDownloadManagerDelegate, whose GenerateFilename() calls
//     net::GenerateFileName and then `suggested_directory.Append(name)` —
//     no reservation, no uniquifier. `//chrome/browser/download` is not linked
//     here and cannot be: //taffy sits below //chrome and its DEPS
//     forbids reaching up.
//
// So the old assertion measured content_shell's target policy, called it
// Chromium's, and would have failed on the day it was written whatever the fork
// did. Reproducing chrome's determiner in this fixture is the fake this file's
// header refuses: it would prove that upstream's own path-reservation tracker
// uniquifies — which upstream's own unit test already proves — and would fail
// for no TaffyGo reason ever, while reporting the row as covered.
//
// The target-policy half of the row therefore belongs to the device matrix,
// with the product's real delegate underneath it: TEST-INDEX.md section 2
// already places download flows there, and its section 3 row now says which
// half went where. What stays here is the half content_shell genuinely owns.
IN_PROC_BROWSER_TEST_F(ParityDownloadTest,
                       ADuplicateFilenameIsStillASecondWholeDownload) {
  ASSERT_TRUE(NavigateToFixture("download-and-upload"));
  const GURL url = OriginUrl("primary", "/files/download?name=product-specs.csv");
  DownloadAndWait(url);
  DownloadAndWait(url);

  content::DownloadManager::DownloadVector items;
  web_contents()->GetBrowserContext()->GetDownloadManager()->GetAllDownloads(
      &items);
  ASSERT_EQ(2u, items.size());

  // Two downloads, not one seen twice. The download system tracks them
  // separately even though they agree on every name they were offered.
  EXPECT_NE(items[0]->GetId(), items[1]->GetId());

  for (download::DownloadItem* item : items) {
    EXPECT_EQ(download::DownloadItem::COMPLETE, item->GetState())
        << "A duplicate name is not a reason to interrupt a download.";
    EXPECT_GT(item->GetReceivedBytes(), 0);
    EXPECT_EQ(item->GetTotalBytes(), item->GetReceivedBytes())
        << "The second download stopped short of the body it was offered.";
    // The path is the delegate's choice and TaffyGo contributes nothing to it,
    // so what is checked is that the delegate's choice survived intact: the
    // directory this fixture set, and the name the corpus offered.
    EXPECT_EQ(downloads_.GetPath(), item->GetTargetFilePath().DirName());
    EXPECT_EQ(FILE_PATH_LITERAL("product-specs.csv"),
              item->GetTargetFilePath().BaseName().value());
  }

  // The premise of everything above, pinned so it cannot drift silently. If
  // this ever fails, the binary has gained a target determiner that uniquifies
  // and the stronger assertion — two distinct paths — belongs back in this
  // file, along with the device-half note in TEST-INDEX.md.
  ASSERT_EQ(items[0]->GetTargetFilePath(), items[1]->GetTargetFilePath())
      << "content_shell's ShellDownloadManagerDelegate appends a generated "
         "name to the download directory and stops, so two downloads offering "
         "one name share one path here. A different answer means the delegate "
         "under this binary changed.";

  // And the bytes that landed are a whole file rather than a half-written one.
  base::ScopedAllowBlockingForTesting allow_blocking;
  const std::optional<int64_t> size =
      base::GetFileSize(items[1]->GetTargetFilePath());
  ASSERT_TRUE(size.has_value());
  EXPECT_EQ(items[1]->GetReceivedBytes(), size.value());
}

// PAR-FILE-001. The offered filename and the content type may disagree, and the
// browser's own record is what the product shows. The corpus serves CSV bytes
// under an executable-looking name on purpose: the record has to carry both
// facts rather than resolving them into one.
IN_PROC_BROWSER_TEST_F(ParityDownloadTest, FilenameAndContentTypeAreBothRecorded) {
  ASSERT_TRUE(NavigateToFixture("download-and-upload"));
  DownloadAndWait(
      OriginUrl("primary", "/files/download?name=product-specs.csv&as=report.exe"));

  content::DownloadManager::DownloadVector items;
  web_contents()->GetBrowserContext()->GetDownloadManager()->GetAllDownloads(
      &items);
  ASSERT_EQ(1u, items.size());

  // The record carries the name Chromium settled on. The corpus served CSV
  // bytes under an executable-looking name, so the two facts have to be
  // separately readable: a record that reconciled them would hide exactly the
  // mismatch a user needs to see.
  DownloadRecord record;
  record.download_id = items.front()->GetId();
  record.tab_id = broker()->tab_id();
  record.state = DownloadState::kComplete;
  record.target_file_name =
      items.front()->GetTargetFilePath().BaseName().AsUTF8Unsafe();
  record.received_bytes = items.front()->GetReceivedBytes();
  record.total_bytes = items.front()->GetTotalBytes();

  EXPECT_TRUE(DownloadIsFinished(record));
  EXPECT_NE(std::string::npos, record.target_file_name.find(".exe"))
      << "Chromium settled on " << record.target_file_name
      << ", which does not carry the offered extension. The mismatch case "
         "cannot be observed if the offered name never survives.";
  EXPECT_EQ("text/csv", items.front()->GetMimeType())
      << "The bytes are CSV whatever the name says. Both facts have to stay "
         "readable side by side.";
}

// PAR-FILE-001. Cancelling a download leaves no completed file and a terminal
// state that says cancelled rather than failed. The distinction matters because
// a failure invites a retry and a cancellation does not.
IN_PROC_BROWSER_TEST_F(ParityDownloadTest, CancellationIsTerminalAndDistinct) {
  ASSERT_TRUE(NavigateToFixture("download-and-upload"));

  // The download has to still be running for cancelling it to mean anything.
  // Cancel() on a download that already reached a terminal state is a no-op,
  // so asserting CANCELLED after one would assert nothing about cancellation
  // at all — it would only be true if the download had already failed.
  content::DownloadTestObserverInProgress started(download_manager(),
                                                  /*wait_count=*/1);
  const GURL slow = slow_server_.GetURL(
      content::SlowDownloadHttpResponse::kSlowResponseHostName,
      content::SlowDownloadHttpResponse::kKnownSizeUrl);
  ASSERT_TRUE(content::ExecJs(
      web_contents(), "location.href = " + base::GetQuotedJSONString(slow.spec())));
  started.WaitForFinished();

  content::DownloadManager::DownloadVector items;
  download_manager()->GetAllDownloads(&items);
  ASSERT_EQ(1u, items.size());
  ASSERT_EQ(download::DownloadItem::IN_PROGRESS, items.front()->GetState());

  content::DownloadTestObserverTerminal settled(
      download_manager(), /*wait_count=*/1,
      content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_FAIL);
  items.front()->Cancel(/*user_cancel=*/true);
  settled.WaitForFinished();

  EXPECT_EQ(download::DownloadItem::CANCELLED, items.front()->GetState());
  EXPECT_EQ(download::DOWNLOAD_INTERRUPT_REASON_USER_CANCELED,
            items.front()->GetLastReason())
      << "A cancellation recorded as a failure invites a retry the user did "
         "not ask for. The distinction is the whole point of this case.";
}

}  // namespace
}  // namespace taffy::test
