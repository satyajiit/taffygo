// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <optional>
#include <string>

#include "taffy/common/public/bip_action.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/common/public/bip_result.h"
#include "taffy/test/support/taffy_observation_test_base.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/shell/browser/shell.h"
#include "taffy/components/intelligence/content/page_intelligence_broker.h"
#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"
#include "taffy/test/support/bip_graph_payload_reader.h"
#include "testing/gtest/include/gtest/gtest.h"

// Single-page application navigation: the case where the document does not
// change and everything the runtime believed about it does.
//
// A history-API route change is the shape of navigation that breaks page
// intelligence quietly. There is no commit of a new document, so nothing
// obviously ends; the DOM is replaced underneath, so every node handle names
// something that is gone; and the URL moves, so an observation taken before and
// reported after describes a page the user is no longer on. The protocol's
// answer is that a same-document route change advances the graph revision and
// invalidates node handles without retiring the page epoch, and this suite is
// that sentence made falsifiable.

namespace taffy::test {
namespace {

using SpaNavigationTest = TaffyObservationTestBase;

// The baseline every other case in this file rests on: the router fixture is
// observable at all, and the observation names the epoch the broker bound.
IN_PROC_BROWSER_TEST_F(SpaNavigationTest, TheRouterFixtureObservesCleanly) {
  const ObservationEnvelope envelope = ObserveFixture("spa-router");
  ASSERT_TRUE(envelope.code == ObservationResultCode::kOk ||
              envelope.code == ObservationResultCode::kIncomplete)
      << "The router fixture did not produce a usable observation; code "
      << static_cast<int>(envelope.code);
  EXPECT_TRUE(envelope.page_epoch.is_valid());
  EXPECT_GT(envelope.graph_revision, 0u);
  EXPECT_EQ(origins().OriginOf("primary").Serialize(),
            envelope.committed_url_metadata.origin.serialization);
}

// A same-document route change advances the revision. Without that, a handle
// taken before the route change satisfies its revision floor afterwards, and
// an action lands on whatever element inherited the identifier.
IN_PROC_BROWSER_TEST_F(SpaNavigationTest, ARouteChangeAdvancesTheRevision) {
  const ObservationEnvelope before = ObserveFixture("spa-router");
  const GraphRevision revision_before = before.graph_revision;

  ASSERT_TRUE(content::ExecJs(
      web_contents(), "document.querySelector('[data-route=\"/spa/index.html#/specs\"]').click()"));
  ASSERT_TRUE(content::WaitForLoadStop(web_contents()));

  const ObservationEnvelope after = client().Observe(
      builder().Observation(MainFrameId(), ObservationScope::kDocument));
  EXPECT_EQ(before.page_epoch, after.page_epoch)
      << "A same-document route change retired the page epoch. It should not: "
         "the document is the same one, and retiring it would force a "
         "resnapshot the protocol does not require.";
  EXPECT_GT(after.graph_revision, revision_before)
      << "The revision did not move across a route change. Every handle from "
         "before the change would still satisfy its revision floor, and an "
         "action would land on whatever inherited the identifier.";
}

// The URL follows the browser, not the page. A route change is a real
// navigation the browser committed, so the observation's URL metadata moves —
// and the origin does not, because a history-API change cannot cross one.
IN_PROC_BROWSER_TEST_F(SpaNavigationTest, TheObservedUrlFollowsTheBrowser) {
  const ObservationEnvelope before = ObserveFixture("spa-router");
  const Origin origin_before = before.committed_url_metadata.origin;

  ASSERT_TRUE(content::ExecJs(web_contents(),
                              "history.pushState({}, '', '/spa/invented-route')"));

  const ObservationEnvelope after = client().Observe(
      builder().Observation(MainFrameId(), ObservationScope::kDocument));
  EXPECT_EQ(origin_before, after.committed_url_metadata.origin)
      << "A history push changed the observed origin. Nothing a page does with "
         "the history API can move it, so this value did not come from the "
         "browser's own record.";
}

// A handle taken before a route change is refused afterwards. The refusal is
// specific: the epoch is still alive, so the answer names the graph rather than
// the document, and the core service's next legal move is a fresh observation of the
// same epoch rather than a rebind.
IN_PROC_BROWSER_TEST_F(SpaNavigationTest, AHandleFromBeforeTheRouteChangeIsRefused) {
  const ObservationEnvelope before = ObserveFixture("spa-router");
  const GraphRevision stale_revision = before.graph_revision;

  ASSERT_TRUE(content::ExecJs(
      web_contents(), "document.querySelector('[data-route=\"/spa/index.html#/specs\"]').click()"));
  ASSERT_TRUE(content::WaitForLoadStop(web_contents()));

  NodeHandle handle;
  handle.tab_id = broker()->tab_id();
  handle.frame_id = MainFrameId();
  handle.page_epoch = before.page_epoch;
  handle.graph_revision = stale_revision;
  handle.node_id = SemanticNodeId{ids().NeverIssued("node")};
  handle.expected_origin = MainFrameOrigin();

  Postcondition navigated;
  navigated.kind = PostconditionKind::kCommittedNavigation;
  const ActionResult result = client().Act(
      builder().Activate(handle, stale_revision, {navigated}));

  EXPECT_FALSE(IsActionSuccess(result.result_code));
  EXPECT_TRUE(RequiresFreshObservation(result.result_code))
      << "The refusal did not tell the core service that a fresh observation is the "
         "only legal next step. Result code "
      << static_cast<int>(result.result_code);
  EXPECT_FALSE(result.dispatched)
      << "A refused action must not have reached a renderer at all; that is "
         "what makes it safe to treat as nothing having happened.";
}

// Going back through a same-document history entry is another route change and
// gets the same treatment. Written separately because the browser reaches it
// through a different path, and a broker that handled a forward push and missed
// a back traversal would look correct in the test above.
IN_PROC_BROWSER_TEST_F(SpaNavigationTest, HistoryBackThroughARouteAlsoAdvances) {
  const ObservationEnvelope initial = ObserveFixture("spa-router");

  ASSERT_TRUE(content::ExecJs(
      web_contents(), "document.querySelector('[data-route=\"/spa/index.html#/specs\"]').click()"));
  ASSERT_TRUE(content::WaitForLoadStop(web_contents()));
  const ObservationEnvelope after_forward = client().Observe(
      builder().Observation(MainFrameId(), ObservationScope::kDocument));

  ASSERT_TRUE(content::HistoryGoBack(web_contents()));
  const ObservationEnvelope after_back = client().Observe(
      builder().Observation(MainFrameId(), ObservationScope::kDocument));

  EXPECT_EQ(initial.page_epoch, after_back.page_epoch);
  EXPECT_GT(after_back.graph_revision, after_forward.graph_revision)
      << "A back traversal through a same-document entry did not advance the "
         "revision, so handles from the forward route survive into a page that "
         "no longer shows what they named.";
}

// A cross-document navigation out of the application retires the epoch. The
// contrast with every case above is the point: same document advances the
// revision, different document retires the epoch, and confusing the two is how
// a handle survives a navigation it should not.
IN_PROC_BROWSER_TEST_F(SpaNavigationTest, LeavingTheApplicationRetiresTheEpoch) {
  const ObservationEnvelope before = ObserveFixture("spa-router");

  ASSERT_TRUE(NavigateToFixture("static-article"));
  ASSERT_TRUE(client().WaitForInvalidationCount(1));

  bool saw_cross_document = false;
  for (const InvalidationNotice& notice : client().invalidations()) {
    if (notice.reason == InvalidationCode::kCrossDocumentCommit) {
      saw_cross_document = true;
      EXPECT_TRUE(notice.retires_page_epoch);
      EXPECT_EQ(before.page_epoch, notice.page_epoch);
    }
  }
  EXPECT_TRUE(saw_cross_document)
      << "Leaving the application did not produce a cross-document "
         "invalidation. Every handle in the old document is dead and nothing "
         "said so.";
}

// A press the page answers with a route change of its own. The page moves
// before the renderer's reply to the press can arrive, so the route change is
// the press's own effect and not a reason to end it: the press is kept, and
// its reply and postcondition decide it (decision 0242). Ending it at the
// route change is how an ordinary press on a single-page site reached the
// task as cancelled by the person, who had not touched anything.
//
// The suite runs over the corpus's TLS mode, because the production
// classifier refuses an ordinary control on an insecure document.
class SpaPressTest : public TaffyObservationTestBase {
 public:
  SpaPressTest() : TaffyObservationTestBase(FixtureOriginMap::Scheme::kHttps) {}
};

IN_PROC_BROWSER_TEST_F(SpaPressTest, APressThatChangesTheRouteIsNotEndedByIt) {
  const ObservationEnvelope before = ObserveFixture("spa-router");
  const std::optional<GraphPayload> graph =
      ReadGraphPayload(before.graph_payload);
  ASSERT_TRUE(graph.has_value())
      << "The router fixture's graph did not decode.";
  const uint16_t activate = static_cast<uint16_t>(mojom::ActionType::kActivate);
  const auto route = std::ranges::find_if(
      graph->nodes, [activate](const GraphPayloadNode& node) {
        return node.name == "Specifications" &&
               std::ranges::find(node.actions, activate) != node.actions.end();
      });
  ASSERT_NE(route, graph->nodes.end())
      << "No pressable route control named Specifications was observed.";

  NodeHandle handle;
  handle.tab_id = broker()->tab_id();
  handle.frame_id = FrameId{route->frame_id};
  handle.page_epoch = before.page_epoch;
  handle.graph_revision = before.graph_revision;
  handle.node_id = SemanticNodeId{route->node_id};
  handle.expected_origin = MainFrameOrigin();

  // The postcondition an ordinary press carries on the product path.
  Postcondition advanced;
  advanced.kind = PostconditionKind::kDocumentAdvanced;
  advanced.timeout_ms = 3000u;
  AuthorizedActionEnvelope envelope =
      builder().Activate(handle, before.graph_revision, {advanced});
  ASSERT_TRUE(RegisterActionGrant(&envelope));

  const ActionResult result = DispatchTaskActionAndWait(std::move(envelope));
  EXPECT_EQ(ActionResultCode::kVerified, result.result_code)
      << "The press that moved the route was ended by its own route change. "
         "Result code "
      << static_cast<int>(result.result_code);
  EXPECT_TRUE(result.dispatched);
  EXPECT_EQ("/specs", web_contents()->GetLastCommittedURL().ref())
      << "The press did not reach the page's control.";
}

}  // namespace
}  // namespace taffy::test
