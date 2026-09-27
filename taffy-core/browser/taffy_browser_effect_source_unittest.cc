// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/taffy_browser_effect_source.h"

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "components/download/public/common/download_item.h"
#include "content/public/browser/download_item_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/fake_download_item.h"
#include "content/public/test/test_renderer_host.h"
#include "taffy/common/public/taffy_download_facts.h"
#include "taffy/components/intelligence/content/browser_effect_source.h"
#include "taffy/components/intelligence/content/postcondition_evidence.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

// What the download witness may say, against a download system driven a state
// at a time.
//
// Who it may say it about is the other half of the rule and lives next door,
// in taffy_browser_effect_source_attribution_unittest.cc. The two are split
// because they fail differently: this file's rules break by leaking text the
// page wrote, and that file's break by crediting the assistant with something
// a person did.
//
// Three cases here are the three ways a transfer ends. The one that would
// still matter if every other case in this file were deleted is
// `TheFileNameNeverReachesTheEvidence`: it is asserted over the whole evidence
// rather than over the field the name would obviously land in, because the way
// that rule breaks is not somebody adding a `file_name` member — it is
// somebody carrying the name through a field that already exists and looks
// harmless.
//
// A real download manager is not needed and is not used. The witness never
// dereferences the manager it is handed — its observation already names the
// one it watches — so the observer entry points are driven directly, and what
// is under test is the projection rather than Chromium's plumbing. The
// plumbing is the download parity suite's subject
// (download_command_router_browsertest.cc), against the real system.

namespace taffy {
namespace {

constexpr uint32_t kDownloadId = 71u;

// Distinctive enough that finding it anywhere is unambiguous, and shaped like
// a file a person would actually be sent. Both are attached to the items these
// tests drive, so that a witness which grew a file-name or URL member would
// have something to leak; the evidence has nowhere to put either, which is why
// no assertion here searches for them.
constexpr char kFileNameMarker[] = "TaffyCanaryQ3-statement";
constexpr char kUrlMarker[] = "taffy-canary-url-token";

const base::FilePath::CharType kDownloadsDirectory[] =
    FILE_PATH_LITERAL("/taffy-test/downloads");
const base::FilePath::CharType kApplicationDirectory[] =
    FILE_PATH_LITERAL("/taffy-test/downloads/app-private");

class RecordingObserver : public BrowserEffectObserver {
 public:
  void OnBrowserFlowStarted(const BrowserFlowEvidence& evidence) override {
    evidence_.push_back(evidence);
  }

  const std::vector<BrowserFlowEvidence>& evidence() const { return evidence_; }

 private:
  std::vector<BrowserFlowEvidence> evidence_;
};

class TaffyBrowserEffectSourceTest : public content::RenderViewHostTestHarness {
 protected:
  void SetUp() override {
    content::RenderViewHostTestHarness::SetUp();
    source_ = std::make_unique<TaffyBrowserEffectSource>(web_contents());
    source_->RegisterDirectoryClassResolver(
        DownloadDestinationKind::kDefaultDownloadsDirectory,
        base::BindRepeating(
            [](const base::FilePath* directory) { return *directory; },
            base::Unretained(&default_directory_)));
    source_->RegisterDirectoryClass(
        DownloadDestinationKind::kApplicationPrivateDirectory,
        base::FilePath(kApplicationDirectory));
    source_->AddObserver(&recorder_);
  }

  void TearDown() override {
    source_->RemoveObserver(&recorder_);
    // Each item notifies its destruction, which is how the witness drops the
    // observation. Doing it while the witness is alive is the ordering the
    // product has too: the download system outlives no tab it reported to.
    items_.clear();
    source_.reset();
    other_tab_.reset();
    content::RenderViewHostTestHarness::TearDown();
  }

  // A download attached to `tab`, already carrying a target path under the
  // default downloads directory.
  content::FakeDownloadItem* CreateItem(content::WebContents* tab,
                                        const std::string& file_name) {
    auto item = std::make_unique<content::FakeDownloadItem>();
    item->SetId(kDownloadId);
    item->SetMimeType("application/pdf");
    item->SetTargetFilePath(
        base::FilePath(kDownloadsDirectory).AppendASCII(file_name));
    item->SetURL(
        GURL(std::string("https://origin.example/get?t=") + kUrlMarker));
    item->SetState(download::DownloadItem::IN_PROGRESS);
    content::DownloadItemUtils::AttachInfoForTesting(item.get(),
                                                     browser_context(), tab);
    content::FakeDownloadItem* raw = item.get();
    items_.push_back(std::move(item));
    return raw;
  }

  content::FakeDownloadItem* CreateItemInScope() {
    return CreateItem(web_contents(), "quarterly.pdf");
  }

  // Drives the transfer to `state` after `bytes` have arrived.
  void Settle(content::FakeDownloadItem* item,
              download::DownloadItem::DownloadState state,
              int64_t bytes) {
    item->SetReceivedBytes(bytes);
    item->SetState(state);
    item->NotifyDownloadUpdated();
  }

  const DownloadFlowFacts& LastFacts() const {
    CHECK(!recorder_.evidence().empty());
    CHECK(recorder_.evidence().back().download.has_value());
    return *recorder_.evidence().back().download;
  }

  RecordingObserver recorder_;
  base::FilePath default_directory_{kDownloadsDirectory};
  std::vector<std::unique_ptr<content::FakeDownloadItem>> items_;
  std::unique_ptr<content::WebContents> other_tab_;
  std::unique_ptr<TaffyBrowserEffectSource> source_;
};

TEST_F(TaffyBrowserEffectSourceTest, ACompletedDownloadIsWitnessed) {
  content::FakeDownloadItem* item = CreateItemInScope();
  source_->OnDownloadCreated(nullptr, item);
  Settle(item, download::DownloadItem::COMPLETE, 4096);

  ASSERT_EQ(2u, recorder_.evidence().size())
      << "One emission for the transfer starting and one for it ending.";

  const BrowserFlowEvidence& terminal = recorder_.evidence().back();
  EXPECT_TRUE(terminal.flow_started);
  EXPECT_EQ(BrowserFlowKind::kDownload, terminal.kind);
  ASSERT_TRUE(terminal.download.has_value());
  EXPECT_EQ(kDownloadId, terminal.download->download_id);
  EXPECT_EQ(MediaTopLevelType::kApplication, terminal.download->media_type);
  EXPECT_EQ(4096, terminal.download->received_bytes);
  EXPECT_EQ(DownloadDestinationKind::kDefaultDownloadsDirectory,
            terminal.download->directory_class);
  EXPECT_EQ(DownloadState::kComplete, terminal.download->state);
  EXPECT_TRUE(DownloadStateIsTerminal(terminal.download->state));
}

TEST_F(TaffyBrowserEffectSourceTest, AnInterruptedDownloadIsWitnessed) {
  content::FakeDownloadItem* item = CreateItemInScope();
  source_->OnDownloadCreated(nullptr, item);
  Settle(item, download::DownloadItem::INTERRUPTED, 1024);

  ASSERT_EQ(2u, recorder_.evidence().size());
  const BrowserFlowEvidence& terminal = recorder_.evidence().back();
  ASSERT_TRUE(terminal.download.has_value());
  EXPECT_EQ(DownloadState::kInterrupted, terminal.download->state);
  EXPECT_EQ(1024, terminal.download->received_bytes)
      << "A partial transfer reports what actually arrived, not what was "
         "promised.";
  EXPECT_TRUE(DownloadStateIsTerminal(terminal.download->state));
}

TEST_F(TaffyBrowserEffectSourceTest, ACancelledDownloadIsWitnessed) {
  content::FakeDownloadItem* item = CreateItemInScope();
  source_->OnDownloadCreated(nullptr, item);
  Settle(item, download::DownloadItem::CANCELLED, 512);

  ASSERT_EQ(2u, recorder_.evidence().size());
  const BrowserFlowEvidence& terminal = recorder_.evidence().back();
  ASSERT_TRUE(terminal.download.has_value());
  EXPECT_EQ(DownloadState::kCancelled, terminal.download->state);
  EXPECT_TRUE(DownloadStateIsTerminal(terminal.download->state));
}

// The rule this class exists to keep.
//
// It used to be asserted by sweeping every string the evidence carried for a
// canary. That sweep is gone rather than kept as a formality, because the rule
// it enforced is now enforced by the type: since decision 0062 section 3,
// `DownloadFlowFacts` has no member of any string type, so there is nothing
// left to sweep and a sweep that always passes is not a test. **A change that
// adds a string field back must restore that sweep**, and this comment is the
// notice that it was deliberately removed rather than never written.
//
// What is left to check is the boundary the enumeration replaced the string
// at: a header carrying a file name in a parameter answers nothing, rather
// than being repaired into the type in front of the semicolon.
TEST_F(TaffyBrowserEffectSourceTest, TheFileNameNeverReachesTheEvidence) {
  content::FakeDownloadItem* item =
      CreateItem(web_contents(), std::string(kFileNameMarker) + ".pdf");
  // The hostile shape: the name dressed as a media-type parameter, aimed at
  // the field that used to hold a string. The item also carries a URL with a
  // token in its query, because a witness that grew a URL member would take
  // this one.
  item->SetMimeType(std::string("application/octet-stream; name=\"") +
                    kFileNameMarker + ".pdf\"");
  source_->OnDownloadCreated(nullptr, item);
  Settle(item, download::DownloadItem::COMPLETE, 2048);

  ASSERT_EQ(2u, recorder_.evidence().size());
  for (const BrowserFlowEvidence& evidence : recorder_.evidence()) {
    ASSERT_TRUE(evidence.download.has_value());
    EXPECT_EQ(MediaTopLevelType::kUnknown, evidence.download->media_type)
        << "A parameter-bearing header contributed a top-level type. A value "
           "this filter does not recognise is dropped, never repaired — "
           "trimming it back to `application` would be this code deciding "
           "what a hostile server meant.";
  }
}

// The case the media-type field was narrowed for.
//
// A subtype is an RFC 9110 token and so is `Q3-statement.pdf`: the token set
// contains the letter, the digit, the dot and the hyphen, which is a file name
// with no spaces in it. `application/TaffyCanaryQ3-statement.pdf` is what
// Chromium's own parser returns for such a header
// (net::MimeUtil::ParseMimeTypeWithoutParameter requires two tokens and
// nothing more), so this is what a hostile server can actually produce rather
// than a shape only a fake can. It used to be carried verbatim. Decision 0062
// section 3 now keeps the top-level half and discards the subtype, so the name
// has nowhere to ride.
TEST_F(TaffyBrowserEffectSourceTest, AServerChosenSubtypeIsDiscarded) {
  const std::string hostile =
      std::string("application/") + kFileNameMarker + ".pdf";
  EXPECT_EQ(MediaTopLevelType::kApplication, MediaTopLevelTypeOf(hostile))
      << "The header parses, so the top-level type is admissible — and it is "
         "all that is admitted.";

  content::FakeDownloadItem* item = CreateItemInScope();
  item->SetMimeType(hostile);
  source_->OnDownloadCreated(nullptr, item);

  ASSERT_FALSE(recorder_.evidence().empty());
  EXPECT_EQ(MediaTopLevelType::kApplication, LastFacts().media_type)
      << "and the evidence carries the enumerator, which cannot spell a file "
         "name however hard the server tries.";
}

TEST_F(TaffyBrowserEffectSourceTest, OnlyTheTopLevelMediaTypeIsCarried) {
  EXPECT_EQ(MediaTopLevelType::kApplication,
            MediaTopLevelTypeOf("application/pdf"));
  EXPECT_EQ(MediaTopLevelType::kImage, MediaTopLevelTypeOf("image/svg+xml"));
  EXPECT_EQ(MediaTopLevelType::kText, MediaTopLevelTypeOf("text/html"));
  EXPECT_EQ(MediaTopLevelType::kVideo, MediaTopLevelTypeOf("video/mp4"));
  EXPECT_EQ(MediaTopLevelType::kApplication,
            MediaTopLevelTypeOf("application/vnd.ms-excel"));

  // A media type is case-insensitive, so a server that shouts still gets an
  // answer rather than a free kUnknown.
  EXPECT_EQ(MediaTopLevelType::kApplication,
            MediaTopLevelTypeOf("Application/PDF"));

  // A parameter, a space, a quote, a path separator and a bare word are each
  // enough to make it not a media type — and the whole value has to parse
  // before any of it is admitted, so none of these is repaired into its
  // top-level type.
  EXPECT_EQ(MediaTopLevelType::kUnknown,
            MediaTopLevelTypeOf("application/pdf; name=\"report.pdf\""));
  EXPECT_EQ(MediaTopLevelType::kUnknown,
            MediaTopLevelTypeOf("application/pdf report.pdf"));
  EXPECT_EQ(MediaTopLevelType::kUnknown,
            MediaTopLevelTypeOf("\"application/pdf\""));
  EXPECT_EQ(MediaTopLevelType::kUnknown,
            MediaTopLevelTypeOf("application/a/b"));
  EXPECT_EQ(MediaTopLevelType::kUnknown, MediaTopLevelTypeOf("application"));
  EXPECT_EQ(MediaTopLevelType::kUnknown, MediaTopLevelTypeOf("/pdf"));
  EXPECT_EQ(MediaTopLevelType::kUnknown, MediaTopLevelTypeOf("application/"));
  EXPECT_EQ(MediaTopLevelType::kUnknown, MediaTopLevelTypeOf(""));
  EXPECT_EQ(MediaTopLevelType::kUnknown,
            MediaTopLevelTypeOf(std::string(200, 'a') + "/pdf"));

  // A well-formed media type whose top-level half is not a registered name is
  // not one of these, and inventing an enumerator for it would be this code
  // deciding what the registry ought to contain.
  EXPECT_EQ(MediaTopLevelType::kUnknown,
            MediaTopLevelTypeOf("x-taffy/statement"));
}

TEST_F(TaffyBrowserEffectSourceTest, ADownloadInAnotherTabIsNotWitnessed) {
  other_tab_ = CreateTestWebContents();
  content::FakeDownloadItem* item =
      CreateItem(other_tab_.get(), "someone-elses.pdf");
  source_->OnDownloadCreated(nullptr, item);
  Settle(item, download::DownloadItem::COMPLETE, 4096);

  EXPECT_TRUE(recorder_.evidence().empty())
      << "A download the person started in another tab would corroborate an "
         "action this tab's verifier is waiting on.";
}

TEST_F(TaffyBrowserEffectSourceTest, AProgressTickAloneEmitsNothing) {
  content::FakeDownloadItem* item = CreateItemInScope();
  source_->OnDownloadCreated(nullptr, item);
  ASSERT_EQ(1u, recorder_.evidence().size());

  item->SetReceivedBytes(512);
  item->NotifyDownloadUpdated();
  item->SetReceivedBytes(1024);
  item->NotifyDownloadUpdated();

  EXPECT_EQ(1u, recorder_.evidence().size())
      << "A byte count settles no postcondition, so a progress tick is not an "
         "event anyone downstream can act on.";
}

TEST_F(TaffyBrowserEffectSourceTest, ADirectoryNobodyDescribedIsUndecided) {
  auto item = std::make_unique<content::FakeDownloadItem>();
  item->SetId(kDownloadId);
  item->SetMimeType("application/pdf");
  item->SetTargetFilePath(
      base::FilePath(FILE_PATH_LITERAL("/somewhere/else/report.pdf")));
  item->SetState(download::DownloadItem::IN_PROGRESS);
  content::DownloadItemUtils::AttachInfoForTesting(
      item.get(), browser_context(), web_contents());
  content::FakeDownloadItem* raw = item.get();
  items_.push_back(std::move(item));

  source_->OnDownloadCreated(nullptr, raw);

  ASSERT_EQ(1u, recorder_.evidence().size());
  ASSERT_TRUE(recorder_.evidence().front().download.has_value());
  EXPECT_EQ(DownloadDestinationKind::kUndecided,
            recorder_.evidence().front().download->directory_class);
}

TEST_F(TaffyBrowserEffectSourceTest,
       AChangedProfilePreferenceReplacesTheOldDefaultDirectory) {
  content::FakeDownloadItem* original = CreateItemInScope();
  source_->OnDownloadCreated(nullptr, original);
  ASSERT_EQ(DownloadDestinationKind::kDefaultDownloadsDirectory,
            LastFacts().directory_class);

  default_directory_ =
      base::FilePath(FILE_PATH_LITERAL("/taffy-test/new-downloads"));
  content::FakeDownloadItem* moved = CreateItemInScope();
  moved->SetId(kDownloadId + 1u);
  moved->SetTargetFilePath(default_directory_.AppendASCII("moved.pdf"));
  source_->OnDownloadCreated(nullptr, moved);
  ASSERT_EQ(DownloadDestinationKind::kDefaultDownloadsDirectory,
            LastFacts().directory_class);

  content::FakeDownloadItem* stale = CreateItemInScope();
  stale->SetId(kDownloadId + 2u);
  source_->OnDownloadCreated(nullptr, stale);
  EXPECT_EQ(DownloadDestinationKind::kUndecided, LastFacts().directory_class);
}

TEST_F(TaffyBrowserEffectSourceTest, TheMostSpecificRegisteredDirectoryWins) {
  auto item = std::make_unique<content::FakeDownloadItem>();
  item->SetId(kDownloadId);
  item->SetMimeType("application/pdf");
  item->SetTargetFilePath(
      base::FilePath(kApplicationDirectory).AppendASCII("cached.pdf"));
  item->SetState(download::DownloadItem::IN_PROGRESS);
  content::DownloadItemUtils::AttachInfoForTesting(
      item.get(), browser_context(), web_contents());
  content::FakeDownloadItem* raw = item.get();
  items_.push_back(std::move(item));

  source_->OnDownloadCreated(nullptr, raw);

  ASSERT_EQ(1u, recorder_.evidence().size());
  ASSERT_TRUE(recorder_.evidence().front().download.has_value());
  EXPECT_EQ(DownloadDestinationKind::kApplicationPrivateDirectory,
            recorder_.evidence().front().download->directory_class);
}

// Decision 0062 section 5 says a witness whose tab has been destroyed reports
// nothing at all rather than falling back to the profile. That is a claim about
// every callback, not only about the one that adds the observation: a person
// who starts a download and closes the tab leaves a transfer running behind a
// scope that no longer exists, and it is the ordinary case rather than an edge
// one.
TEST_F(TaffyBrowserEffectSourceTest, AWitnessWhoseTabIsGoneReportsNothing) {
  other_tab_ = CreateTestWebContents();
  RecordingObserver recorder;
  auto scoped = std::make_unique<TaffyBrowserEffectSource>(other_tab_.get());
  scoped->AddObserver(&recorder);

  content::FakeDownloadItem* item =
      CreateItem(other_tab_.get(), "left-running.pdf");
  scoped->OnDownloadCreated(nullptr, item);
  ASSERT_EQ(1u, recorder.evidence().size());

  other_tab_.reset();
  Settle(item, download::DownloadItem::COMPLETE, 4096);

  EXPECT_EQ(1u, recorder.evidence().size())
      << "The transfer outlived the tab the witness was built for, and the "
         "witness went on reporting about it.";

  scoped->RemoveObserver(&recorder);
}

// A verifier subscribes before its action is dispatched. Handing it what the
// witness saw earlier would let an unrelated download corroborate an action
// that has not run.
TEST_F(TaffyBrowserEffectSourceTest, ANewObserverIsToldNothingAboutThePast) {
  content::FakeDownloadItem* item = CreateItemInScope();
  source_->OnDownloadCreated(nullptr, item);
  Settle(item, download::DownloadItem::COMPLETE, 4096);
  ASSERT_EQ(2u, recorder_.evidence().size());

  RecordingObserver latecomer;
  source_->AddObserver(&latecomer);
  EXPECT_TRUE(latecomer.evidence().empty());
  source_->RemoveObserver(&latecomer);
}

}  // namespace
}  // namespace taffy
