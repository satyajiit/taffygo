// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/time/time.h"
#include "components/download/public/common/download_item.h"
#include "content/public/browser/download_item_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/fake_download_item.h"
#include "content/public/test/mock_download_manager.h"
#include "content/public/test/test_renderer_host.h"
#include "taffy/browser/taffy_browser_effect_source.h"
#include "taffy/common/public/taffy_download_facts.h"
#include "taffy/components/intelligence/content/browser_effect_source.h"
#include "taffy/components/intelligence/content/postcondition_evidence.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "third_party/blink/public/common/input/web_keyboard_event.h"

// What the download witness will and will not credit to an action.
//
// The sibling suite (taffy_browser_effect_source_unittest.cc) covers what the
// witness may *say*. This one covers who it may say it *about*, which is a
// different rule with a different failure: getting it wrong does not leak a
// file name, it writes an audit record claiming the assistant did something a
// person did.
//
// Scope alone cannot tell those two apart. A witness is per tab and a verifier
// runs for a few seconds, so "a download appeared in this tab while the
// verifier was waiting" is a coincidence a person produces by browsing. The
// case that matters most here is
// `ADownloadThePersonStartedIsNotAttributed`; everything else pins an edge of
// the same rule.
//
// One ordering is deliberately not asserted here, because the witness gets it
// wrong and no assertion in this file would be honest about it. A person
// clicks a download link, the input event closes the window, the assistant's
// next action reopens one, and only then does the response head arrive and the
// `DownloadItem` appear — so the person's transfer is first seen inside the
// new window and is bound to it. Driving those four steps in this order
// produces an `attributed_to` naming the unrelated dispatch. Decision 0062
// section 6 records why nothing at this layer separates the two causes and
// assigns it to [Open (OD-056)]; a test asserting the correct answer belongs
// with the change that makes it true, and a test asserting the current one
// would read as though this were the intended rule.
//
// The witness is driven through its observer entry points directly, for the
// reason the sibling suite states. The two mock managers exist only for the
// case about changing managers, where all that is asserted is what this class
// stops holding.

namespace taffy {
namespace {

constexpr uint32_t kDownloadId = 71u;

const base::FilePath::CharType kDownloadsDirectory[] =
    FILE_PATH_LITERAL("/taffy-test/downloads");

class RecordingObserver : public BrowserEffectObserver {
 public:
  void OnBrowserFlowStarted(const BrowserFlowEvidence& evidence) override {
    evidence_.push_back(evidence);
  }

  const std::vector<BrowserFlowEvidence>& evidence() const { return evidence_; }

 private:
  std::vector<BrowserFlowEvidence> evidence_;
};

// What Chromium's input pipeline delivers when a person presses a key in this
// tab. The witness reads nothing out of it — what matters is that the pipeline
// delivered one at all — so any interaction-class event says the same thing.
// It is spelled out rather than default-constructed so that a reader can see
// it is an input event and not a placeholder.
blink::WebKeyboardEvent PersonPressedAKey() {
  return blink::WebKeyboardEvent(blink::WebInputEvent::Type::kRawKeyDown,
                                 blink::WebInputEvent::kNoModifiers,
                                 base::TimeTicks::Now());
}

class TaffyBrowserEffectSourceAttributionTest
    : public content::RenderViewHostTestHarness {
 protected:
  void SetUp() override {
    content::RenderViewHostTestHarness::SetUp();
    source_ = std::make_unique<TaffyBrowserEffectSource>(web_contents());
    source_->RegisterDirectoryClass(
        DownloadDestinationKind::kDefaultDownloadsDirectory,
        base::FilePath(kDownloadsDirectory));
    source_->AddObserver(&recorder_);
  }

  void TearDown() override {
    source_->RemoveObserver(&recorder_);
    items_.clear();
    source_.reset();
    content::RenderViewHostTestHarness::TearDown();
  }

  content::FakeDownloadItem* CreateItemInScope() {
    auto item = std::make_unique<content::FakeDownloadItem>();
    item->SetId(kDownloadId);
    item->SetMimeType("application/pdf");
    item->SetTargetFilePath(
        base::FilePath(kDownloadsDirectory).AppendASCII("quarterly.pdf"));
    item->SetState(download::DownloadItem::IN_PROGRESS);
    content::DownloadItemUtils::AttachInfoForTesting(
        item.get(), browser_context(), web_contents());
    content::FakeDownloadItem* raw = item.get();
    items_.push_back(std::move(item));
    return raw;
  }

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
  std::vector<std::unique_ptr<content::FakeDownloadItem>> items_;
  // Declared before the witness, so a witness still observing one of them
  // would be a failed assertion rather than a use-after-free.
  testing::NiceMock<content::MockDownloadManager> first_manager_;
  testing::NiceMock<content::MockDownloadManager> second_manager_;
  std::unique_ptr<TaffyBrowserEffectSource> source_;
};

TEST_F(TaffyBrowserEffectSourceAttributionTest,
       ADownloadInsideTheDispatchWindowIsBound) {
  const DispatchWatermark dispatch = source_->NoteDispatch();

  content::FakeDownloadItem* item = CreateItemInScope();
  source_->OnDownloadCreated(nullptr, item);
  Settle(item, download::DownloadItem::COMPLETE, 4096);

  ASSERT_EQ(2u, recorder_.evidence().size());
  for (const BrowserFlowEvidence& evidence : recorder_.evidence()) {
    ASSERT_TRUE(evidence.download.has_value());
    ASSERT_TRUE(evidence.download->attributed_to.has_value())
        << "Every emission about one transfer carries the same binding; a "
           "verifier that only saw the terminal one would otherwise have to "
           "guess.";
    EXPECT_EQ(dispatch, *evidence.download->attributed_to);
  }
}

// The case the whole watermark exists for, and the one a per-tab, per-time
// scope answers wrongly: the assistant clicks something, and the person clicks
// a download link in the same tab a moment later.
TEST_F(TaffyBrowserEffectSourceAttributionTest,
       ADownloadThePersonStartedIsNotAttributed) {
  source_->NoteDispatch();

  // Chromium's input pipeline delivered a real input event to this tab. An
  // assistant action never does — decision 0059 sends every write through the
  // accessibility path, which reaches the renderer by mojo and never through
  // the browser's input router — so this is the browser stating that a human
  // acted.
  source_->DidGetUserInteraction(PersonPressedAKey());

  content::FakeDownloadItem* item = CreateItemInScope();
  source_->OnDownloadCreated(nullptr, item);
  Settle(item, download::DownloadItem::COMPLETE, 4096);

  ASSERT_EQ(2u, recorder_.evidence().size())
      << "The transfer is still witnessed. What changes is what it is "
         "credited to.";
  for (const BrowserFlowEvidence& evidence : recorder_.evidence()) {
    ASSERT_TRUE(evidence.download.has_value());
    EXPECT_FALSE(evidence.download->attributed_to.has_value())
        << "A download the person started in this tab was credited to the "
           "assistant's dispatch. That is a false entry in an audit record, "
           "which is worse than no entry at all.";
  }
}

// The browser-flow analogue of "strictly newer than the revision at dispatch".
TEST_F(TaffyBrowserEffectSourceAttributionTest,
       ADownloadThatBeganBeforeTheDispatchIsNotAttributed) {
  content::FakeDownloadItem* item = CreateItemInScope();
  source_->OnDownloadCreated(nullptr, item);
  ASSERT_EQ(1u, recorder_.evidence().size());
  EXPECT_FALSE(LastFacts().attributed_to.has_value());

  // The action goes out while the transfer is still running, and the transfer
  // then finishes inside the window. It still is not the action's effect.
  source_->NoteDispatch();
  Settle(item, download::DownloadItem::COMPLETE, 4096);

  ASSERT_EQ(2u, recorder_.evidence().size());
  EXPECT_FALSE(LastFacts().attributed_to.has_value())
      << "A transfer that was already running does not become an action's "
         "effect by continuing to run into its window.";
}

TEST_F(TaffyBrowserEffectSourceAttributionTest,
       ALaterDispatchDoesNotInheritTheEvidence) {
  const DispatchWatermark first = source_->NoteDispatch();
  content::FakeDownloadItem* item = CreateItemInScope();
  source_->OnDownloadCreated(nullptr, item);

  const DispatchWatermark second = source_->NoteDispatch();
  EXPECT_FALSE(first == second)
      << "A reused watermark would let a finished attempt's evidence settle a "
         "later one.";

  Settle(item, download::DownloadItem::COMPLETE, 4096);

  ASSERT_EQ(2u, recorder_.evidence().size());
  ASSERT_TRUE(LastFacts().attributed_to.has_value());
  EXPECT_EQ(first, *LastFacts().attributed_to)
      << "The transfer belongs to the dispatch it started under, not to "
         "whichever one happens to be outstanding when it ends.";
}

TEST_F(TaffyBrowserEffectSourceAttributionTest,
       ADispatchReopensTheWindowThePersonClosed) {
  source_->NoteDispatch();
  source_->DidGetUserInteraction(PersonPressedAKey());
  const DispatchWatermark after = source_->NoteDispatch();

  content::FakeDownloadItem* item = CreateItemInScope();
  source_->OnDownloadCreated(nullptr, item);

  ASSERT_EQ(1u, recorder_.evidence().size());
  ASSERT_TRUE(LastFacts().attributed_to.has_value())
      << "A person's earlier click must not disqualify every later action for "
         "the life of the tab.";
  EXPECT_EQ(after, *LastFacts().attributed_to);
}

TEST_F(TaffyBrowserEffectSourceAttributionTest,
       FinishingTheCurrentDispatchClosesItsWindow) {
  const DispatchWatermark dispatch = source_->NoteDispatch();
  ASSERT_TRUE(source_->HasOpenDispatch());

  source_->CloseDispatch(dispatch);
  EXPECT_FALSE(source_->HasOpenDispatch());

  content::FakeDownloadItem* item = CreateItemInScope();
  source_->OnDownloadCreated(nullptr, item);
  ASSERT_EQ(1u, recorder_.evidence().size());
  EXPECT_FALSE(LastFacts().attributed_to.has_value());
}

TEST_F(TaffyBrowserEffectSourceAttributionTest,
       FinishingASupersededDispatchKeepsTheCurrentWindow) {
  const DispatchWatermark first = source_->NoteDispatch();
  const DispatchWatermark current = source_->NoteDispatch();

  source_->CloseDispatch(first);
  ASSERT_TRUE(source_->HasOpenDispatch());

  content::FakeDownloadItem* item = CreateItemInScope();
  source_->OnDownloadCreated(nullptr, item);
  ASSERT_EQ(1u, recorder_.evidence().size());
  ASSERT_TRUE(LastFacts().attributed_to.has_value());
  EXPECT_EQ(current, *LastFacts().attributed_to);
}

TEST_F(TaffyBrowserEffectSourceAttributionTest,
       WithNoDispatchNothingIsAttributed) {
  content::FakeDownloadItem* item = CreateItemInScope();
  source_->OnDownloadCreated(nullptr, item);

  ASSERT_EQ(1u, recorder_.evidence().size());
  EXPECT_FALSE(LastFacts().attributed_to.has_value())
      << "Manual browsing is the ordinary case, and nothing about it belongs "
         "to an action that was never dispatched.";
}

// Moving the manager observation is not the whole of moving managers. The
// per-item observations belong to the old manager's items, and the remembered
// facts are keyed by an identifier that manager allocated — download
// identifiers are unique within a manager and not across two.
TEST_F(TaffyBrowserEffectSourceAttributionTest,
       ChangingTheManagerForgetsTheOldOne) {
  source_->Observe(&first_manager_);

  content::FakeDownloadItem* item = CreateItemInScope();
  source_->OnDownloadCreated(nullptr, item);
  ASSERT_EQ(1u, recorder_.evidence().size());

  source_->Observe(&second_manager_);

  // The per-item observation went with the manager that owned the item, so a
  // transfer in the profile this witness no longer serves reports nothing.
  Settle(item, download::DownloadItem::COMPLETE, 4096);
  EXPECT_EQ(1u, recorder_.evidence().size())
      << "The witness stayed subscribed to an item belonging to a download "
         "system it had stopped watching.";

  // And the remembered facts went too. Without that, an identifier the new
  // manager reuses is compared against a stranger's facts, and an emission the
  // verifier is waiting for is silently suppressed as "nothing changed" — the
  // failure that looks like a deadline rather than like a bug.
  source_->OnDownloadCreated(nullptr, item);
  ASSERT_EQ(2u, recorder_.evidence().size())
      << "A first sight under the new manager was suppressed by facts "
         "remembered from the old one.";
  EXPECT_EQ(DownloadState::kComplete, LastFacts().state);
}

// The attribution window is about this tab's person and this tab's actions,
// not about which profile's download system is being watched.
TEST_F(TaffyBrowserEffectSourceAttributionTest,
       ChangingTheManagerKeepsTheOpenWindow) {
  source_->Observe(&first_manager_);
  const DispatchWatermark dispatch = source_->NoteDispatch();
  source_->Observe(&second_manager_);

  content::FakeDownloadItem* item = CreateItemInScope();
  source_->OnDownloadCreated(nullptr, item);

  ASSERT_EQ(1u, recorder_.evidence().size());
  ASSERT_TRUE(LastFacts().attributed_to.has_value());
  EXPECT_EQ(dispatch, *LastFacts().attributed_to);
}

}  // namespace
}  // namespace taffy
