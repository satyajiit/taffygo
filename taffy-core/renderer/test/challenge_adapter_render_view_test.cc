// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <vector>

#include "taffy/renderer/semantic_graph_store.h"
#include "content/public/test/render_view_test.h"
#include "third_party/blink/public/web/web_frame_widget.h"
#include "taffy/renderer/test/endpoint_test_harness.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::test {
namespace {

class ChallengeAdapterTest : public EndpointTestHarness {};

TEST_F(ChallengeAdapterTest, ImageChallengeIsRefusedBeforeAnyPixelCapture) {
  LoadAndBind(
      "<html><body><form>"
      "<img width='96' height='48' "
      "src='data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///ywAAAAAAQABAAACAUwAOw=='>"
      "<input type='text'><button type='button' aria-label='Refresh'></button>"
      "</form></body></html>");
  mojom::SnapshotResultPtr snapshot = Snapshot(DocumentRequest());
  ASSERT_TRUE(snapshot && snapshot->snapshot);
  ASSERT_TRUE(endpoint()->store_for_testing());

  const mojom::SemanticNode* image = nullptr;
  for (const mojom::SemanticNode* candidate :
       NodesWithRole(*snapshot->snapshot, mojom::SemanticRole::kImage)) {
    const SemanticGraphStore::LiveNode* live =
        endpoint()->store_for_testing()->FindLive(
            SemanticNodeId(candidate->node_id));
    if (live &&
        live->dom_key.space == SemanticGraphStore::IdentitySpace::kDom) {
      image = candidate;
      EXPECT_TRUE(endpoint()->store_for_testing()->IsChallengePresentationTarget(
          live->dom_key.dom_node_id, ChallengeKind::kImage));
      break;
    }
  }
  ASSERT_TRUE(image) << "the DOM adapter did not expose the challenge image";

  mojom::MediaTargetResultPtr inspected = InspectMedia(
      image->node_id, snapshot->snapshot->graph_revision,
      mojom::MediaTargetKind::kImage);
  ASSERT_TRUE(inspected);
  EXPECT_EQ(inspected->code, mojom::MediaTargetResultCode::kWrongType);
  EXPECT_FALSE(inspected->bounds);
  EXPECT_TRUE(inspected->loaded_caption_cues.empty());

  mojom::MediaTargetResultPtr page = InspectMedia(
      "", snapshot->snapshot->graph_revision,
      mojom::MediaTargetKind::kPageScreenshot);
  ASSERT_TRUE(page);
  EXPECT_EQ(page->code, mojom::MediaTargetResultCode::kUnsafeContent);
  EXPECT_FALSE(page->bounds);
  EXPECT_TRUE(page->redaction_bounds.empty());
}

// The form-control text fields of a snapshot, in the order the form adapter
// emitted them, which is the form's own order.
std::vector<const mojom::SemanticNode*> FormTextFields(
    const mojom::PageSnapshot& snapshot) {
  std::vector<const mojom::SemanticNode*> fields;
  for (const mojom::SemanticNode* node : EndpointTestHarness::NodesWithRole(
           snapshot, mojom::SemanticRole::kTextField)) {
    if (std::ranges::find(node->sources, mojom::SourceKind::kFormControl) !=
        node->sources.end()) {
      fields.push_back(node);
    }
  }
  return fields;
}

mojom::ChallengeKind ChallengeOf(const mojom::SemanticNode& node) {
  return node.challenge_kind.value_or(mojom::ChallengeKind::kNone);
}

// The myAadhaar download form asks for a number and a CAPTCHA in one form, and
// the picture's only label is its own alt text. Read form-wide, two fields
// mean neither is a challenge, and the sheet on a phone drew "Enter Captcha"
// with no picture (decision 0193).
TEST_F(ChallengeAdapterTest, ACaptchaBesideAnotherFieldIsReadFromItsOwnBlock) {
  LoadAndBind(
      "<html><body><form>"
      "<div><label for='uid'>Enter Aadhaar Number</label>"
      "<input id='uid' type='text'></div>"
      "<div><div><label for='cap'>Enter Captcha</label>"
      "<input id='cap' type='text'></div>"
      "<div><img width='96' height='48' "
      "alt='CAPTCHA image: enter the characters shown' "
      "src='data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///ywAAAAAAQABAAACAUwAOw=='>"
      "</div></div>"
      "<button type='button'>Send OTP</button>"
      "</form></body></html>");
  mojom::SnapshotResultPtr snapshot = Snapshot(DocumentRequest());
  ASSERT_TRUE(snapshot && snapshot->snapshot);
  ASSERT_TRUE(endpoint()->store_for_testing());

  const std::vector<const mojom::SemanticNode*> fields =
      FormTextFields(*snapshot->snapshot);
  ASSERT_EQ(2u, fields.size());
  EXPECT_EQ(mojom::ChallengeKind::kNone, ChallengeOf(*fields[0]));
  EXPECT_EQ(mojom::ChallengeKind::kImage, ChallengeOf(*fields[1]));

  // The picture the sheet will show is the one in the CAPTCHA's block.
  bool image_is_the_target = false;
  for (const mojom::SemanticNode* candidate : NodesWithRole(
           *snapshot->snapshot, mojom::SemanticRole::kImage)) {
    const SemanticGraphStore::LiveNode* live =
        endpoint()->store_for_testing()->FindLive(
            SemanticNodeId(candidate->node_id));
    if (live &&
        live->dom_key.space == SemanticGraphStore::IdentitySpace::kDom) {
      image_is_the_target |=
          endpoint()->store_for_testing()->IsChallengePresentationTarget(
              live->dom_key.dom_node_id, ChallengeKind::kImage);
    }
  }
  EXPECT_TRUE(image_is_the_target);
}

// The graph gives a challenge's answer and a one-time code no label, so the
// model never reads a CAPTCHA's question. The sheet that asks the person for
// either names the row from the live node, as the page names it. On a phone
// the myAadhaar CAPTCHA was left off every sheet as `no-label` (decision
// 0249).
TEST_F(ChallengeAdapterTest, TheSheetNamesAFieldTheGraphLeavesUnnamed) {
  LoadAndBind(
      "<html><body><form>"
      "<div><label for='uid'>Enter Aadhaar Number</label>"
      "<input id='uid' type='text'></div>"
      "<div><div><label for='cap'>Enter Captcha</label>"
      "<input id='cap' type='text'></div>"
      "<div><img width='96' height='48' "
      "alt='CAPTCHA image: enter the characters shown' "
      "src='data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///ywAAAAAAQABAAACAUwAOw=='>"
      "</div></div>"
      "</form><form>"
      "<label for='otp'>Enter OTP</label>"
      "<input id='otp' type='text' autocomplete='one-time-code'>"
      "</form></body></html>");
  mojom::SnapshotResultPtr snapshot = Snapshot(DocumentRequest());
  ASSERT_TRUE(snapshot && snapshot->snapshot);
  ASSERT_TRUE(endpoint()->store_for_testing());

  int challenge_answers = 0;
  int one_time_codes = 0;
  for (const mojom::SemanticNodePtr& node : snapshot->snapshot->nodes) {
    const SemanticGraphStore::LiveNode* live =
        endpoint()->store_for_testing()->FindLive(
            SemanticNodeId(node->node_id));
    if (!live) {
      continue;
    }
    if (live->sensitivity == Sensitivity::kChallengeResponse) {
      ++challenge_answers;
      EXPECT_EQ("Enter Captcha", live->display_label);
      EXPECT_FALSE(node->name.has_value());
    } else if (live->sensitivity == Sensitivity::kOneTimeCode) {
      ++one_time_codes;
      EXPECT_EQ("Enter OTP", live->display_label);
      EXPECT_FALSE(node->name.has_value());
    }
  }
  EXPECT_EQ(1, challenge_answers);
  EXPECT_EQ(1, one_time_codes);
}

// A picture beside one field is a challenge only when something says so, and
// a block that holds two fields is not one field's challenge.
TEST_F(ChallengeAdapterTest, APictureIsNotAChallengeWithoutEvidenceOrAlone) {
  LoadAndBind(
      "<html><body><form>"
      "<div><input type='text'>"
      "<img width='96' height='48' alt='Product photo' "
      "src='data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///ywAAAAAAQABAAACAUwAOw=='>"
      "</div>"
      "<div><input type='text'><input type='text'>"
      "<img width='96' height='48' alt='CAPTCHA' "
      "src='data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///ywAAAAAAQABAAACAUwAOw=='>"
      "</div>"
      "</form></body></html>");
  mojom::SnapshotResultPtr snapshot = Snapshot(DocumentRequest());
  ASSERT_TRUE(snapshot && snapshot->snapshot);

  const std::vector<const mojom::SemanticNode*> fields =
      FormTextFields(*snapshot->snapshot);
  ASSERT_EQ(3u, fields.size());
  for (const mojom::SemanticNode* field : fields) {
    EXPECT_EQ(mojom::ChallengeKind::kNone, ChallengeOf(*field));
  }
}

TEST_F(ChallengeAdapterTest,
       PageScreenshotReturnsFreshSecretGeometryAndRefusesAStaleNode) {
  LoadAndBind(
      "<html><body><form><label for='secret'>Password</label>"
      "<input id='secret' type='password' value='never-export-this'>"
      "</form></body></html>");
  mojom::SnapshotResultPtr snapshot = Snapshot(DocumentRequest());
  ASSERT_TRUE(snapshot && snapshot->snapshot);

  mojom::MediaTargetResultPtr inspected = InspectMedia(
      "", snapshot->snapshot->graph_revision,
      mojom::MediaTargetKind::kPageScreenshot);
  ASSERT_TRUE(inspected);
  EXPECT_EQ(inspected->code, mojom::MediaTargetResultCode::kOk);
  ASSERT_TRUE(inspected->bounds);
  ASSERT_EQ(inspected->redaction_bounds.size(), 1u);
  EXPECT_GT(inspected->redaction_bounds.front()->width, 0);
  EXPECT_GT(inspected->redaction_bounds.front()->height, 0);

  // A secret that is still in the document and can no longer be measured. The
  // store keeps the node — a style change is an attribute mutation, not a
  // removal — so the redaction list still names it and VisibleBoundsInWidget
  // answers empty, which is the contract's first example of unsafe content: a
  // prohibited secret lacking fresh geometry. A capture admitted here would
  // carry a region nothing could have redacted.
  //
  // This is not the mechanism this case was written with. It removed the
  // input, and a removal is exactly the mutation DomMutationSignalSource
  // forwards to OnNodesRemoved, which retires the id and drops it from the
  // store — so there was no stale node left to refuse, the redaction list came
  // back empty, and the renderer truthfully answered kOk about a page that no
  // longer held a secret. The test read as a security assertion and was
  // measuring a path that could not run. The removal case is asserted below
  // for what it actually is.
  ExecuteJavaScriptForTests(
      "document.getElementById('secret').style.display = 'none';");
  // Layout has to be driven by hand here. Nothing in a render-view test
  // produces a compositor frame, so `VisibleBoundsInWidget` otherwise answers
  // from whatever layout was last computed and reports the rectangle the field
  // occupied before it was hidden — which is why the first attempt at this
  // case saw the geometry it was expecting to be refused. In the product the
  // capture path has committed a frame before it asks, so this is the test
  // supplying what the environment does not, not a behaviour being simulated.
  GetWebFrameWidget()->UpdateAllLifecyclePhases(
      blink::DocumentUpdateReason::kTest);
  inspected = InspectMedia("", snapshot->snapshot->graph_revision,
                           mojom::MediaTargetKind::kPageScreenshot);
  ASSERT_TRUE(inspected);
  EXPECT_EQ(inspected->code, mojom::MediaTargetResultCode::kUnsafeContent);
  // A refusal carries no geometry. The viewport bounds were once written
  // before the redaction loop ran, so every refusal inside it returned the
  // measurements it was refusing to authorize.
  EXPECT_FALSE(inspected->bounds);
  EXPECT_TRUE(inspected->redaction_bounds.empty());
}

// The other half of the pair above, stated rather than assumed: a removed
// secret leaves the store, so the page really has nothing prohibited in it and
// the renderer says so. Refusing here would be the renderer inventing a hazard
// the document no longer contains.
//
// What keeps that answer safe is not this call. The browser inspects once
// before the capture and once after it, and PageMediaObservationRun refuses a
// screenshot whose second observed_graph_revision differs from its first, or
// whose redaction bounds moved. So the capture taken while the secret was on
// screen is rejected by the revision that this removal advanced — by the
// caller that holds both answers, which is the only place the comparison can
// be made.
TEST_F(ChallengeAdapterTest, PageScreenshotReportsARemovedSecretAsGone) {
  LoadAndBind(
      "<html><body><form><label for='secret'>Password</label>"
      "<input id='secret' type='password' value='never-export-this'>"
      "</form></body></html>");
  mojom::SnapshotResultPtr snapshot = Snapshot(DocumentRequest());
  ASSERT_TRUE(snapshot && snapshot->snapshot);

  mojom::MediaTargetResultPtr before = InspectMedia(
      "", snapshot->snapshot->graph_revision,
      mojom::MediaTargetKind::kPageScreenshot);
  ASSERT_TRUE(before);
  ASSERT_EQ(before->code, mojom::MediaTargetResultCode::kOk);
  ASSERT_EQ(before->redaction_bounds.size(), 1u);

  ExecuteJavaScriptForTests("document.getElementById('secret').remove();");
  mojom::MediaTargetResultPtr after = InspectMedia(
      "", snapshot->snapshot->graph_revision,
      mojom::MediaTargetKind::kPageScreenshot);
  ASSERT_TRUE(after);
  EXPECT_EQ(after->code, mojom::MediaTargetResultCode::kOk);
  EXPECT_TRUE(after->redaction_bounds.empty());
  EXPECT_GT(after->observed_graph_revision, before->observed_graph_revision)
      << "The removal did not advance the graph revision. That number is the "
         "only thing telling the browser its capture and its geometry came "
         "from two different pages, so without it the post-capture check has "
         "nothing to compare.";
}

}  // namespace
}  // namespace taffy::test
