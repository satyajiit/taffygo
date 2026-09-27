// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
#include <vector>

#include "content/public/test/browser_task_environment.h"
#include "taffy/browser/field_value_request_coordinator_test_support.h"
#include "testing/gtest/include/gtest/gtest.h"

// A request the browser gives up on still has to reach the task as an answer
// of nothing, and it can only do that while the manager still holds the
// request open: `ValidateCommand` admits a count only for a request id in the
// emitted set, and the close sink is what takes it out. On a phone the other
// order refused every such answer as `[taffy_command_refused]
// at=submit/revision kind=26`, and the errand sat on "Waiting for you" with no
// sheet on the screen (verification report, section 2.48).

namespace taffy {
namespace {

using namespace field_value_request_coordinator_test;

std::vector<std::string> ReportedThenClosed() {
  return {std::string("reported ") + kRequestId,
          std::string("closed ") + kRequestId};
}

TEST_F(FieldValueRequestCoordinatorTest,
       ARequestWhoseTargetWentAwayIsAnsweredBeforeItCloses) {
  node_gone_ = true;
  OpenRequest();

  ASSERT_EQ(1u, reported_.size());
  EXPECT_EQ(0u, reported_[0].second);
  EXPECT_EQ(ReportedThenClosed(), events_);
  EXPECT_EQ(0u, coordinator_->open_request_count());
}

TEST_F(FieldValueRequestCoordinatorTest,
       ARequestWhosePageMovedWhileTypingIsAnsweredBeforeItCloses) {
  OpenRequest();
  task_environment_->RunUntilIdle();
  ASSERT_EQ(1u, client_.opened.size());
  // The site moved the tab to a new document while the sheet was open.
  page_epoch_ = "page-2";

  EXPECT_EQ(browser::field_values::mojom::FieldValueSupplyVerdict::
                kUnknownRequest,
            Supply({"1234 5678 9012"}).verdict);
  ASSERT_EQ(1u, reported_.size());
  EXPECT_EQ(0u, reported_[0].second);
  EXPECT_EQ(ReportedThenClosed(), events_);
}

TEST_F(FieldValueRequestCoordinatorTest,
       ADismissedRequestIsAnsweredBeforeItCloses) {
  OpenRequest();
  coordinator_->Dismiss(kRequestId);

  EXPECT_EQ(ReportedThenClosed(), events_);
}

}  // namespace
}  // namespace taffy
