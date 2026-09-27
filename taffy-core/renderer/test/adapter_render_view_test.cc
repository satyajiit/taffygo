// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "taffy/renderer/adapters/adapter.h"
#include "taffy/renderer/semantic_graph_store.h"
#include "taffy/renderer/test/endpoint_test_harness.h"
#include "taffy/renderer/test/fixture_corpus.h"
#include "testing/gtest/include/gtest/gtest.h"

// The adapter behaviour that can only be proved against a parsed document:
// composed-tree projection, virtualized recycling, visibility, selection,
// structured-data corroboration, and the honesty of the capability report.
//
// Every expectation here is one the corpus manifest declares in its
// verifier_postconditions, restated as an assertion. Where a fixture is used,
// its markup is read from the corpus rather than typed in, for the same
// reason the canary tokens are: the corpus is the authority and a copy would
// drift from it silently.

namespace taffy::test {
namespace {

class AdapterTest : public EndpointTestHarness {};

void AddAdapter(mojom::SnapshotRequest* request,
                mojom::AdapterKind kind,
                mojom::AdapterRequirementLevel requirement) {
  auto adapter = mojom::AdapterRequirement::New();
  adapter->adapter = kind;
  adapter->requirement = requirement;
  request->adapters.push_back(std::move(adapter));
}

TEST_F(AdapterTest, UnrequestedUnsupportedAdaptersDoNotDegradeTheSnapshot) {
  LoadAndBind("<html><body><p>Required DOM evidence</p></body></html>");
  mojom::SnapshotRequestPtr request = DocumentRequest();
  request->adapters.clear();
  AddAdapter(request.get(), mojom::AdapterKind::kDom,
             mojom::AdapterRequirementLevel::kRequired);

  mojom::SnapshotResultPtr result = Snapshot(std::move(request));
  ASSERT_TRUE(result && result->snapshot);
  EXPECT_EQ(mojom::ObservationResultCode::kOk, result->code);
  ASSERT_EQ(1u, result->snapshot->adapters.size());
  EXPECT_EQ(mojom::AdapterKind::kDom,
            result->snapshot->adapters.front()->adapter);
}

TEST_F(AdapterTest, MissingOptionalAdaptersAreNamedWithoutDegradingRequiredEvidence) {
  LoadAndBind("<html><body><p>No form or structured data</p></body></html>");
  mojom::SnapshotRequestPtr request = DocumentRequest();
  request->adapters.clear();
  AddAdapter(request.get(), mojom::AdapterKind::kDom,
             mojom::AdapterRequirementLevel::kRequired);
  AddAdapter(request.get(), mojom::AdapterKind::kForms,
             mojom::AdapterRequirementLevel::kOptional);
  AddAdapter(request.get(), mojom::AdapterKind::kMetadata,
             mojom::AdapterRequirementLevel::kOptional);

  mojom::SnapshotResultPtr result = Snapshot(std::move(request));
  ASSERT_TRUE(result && result->snapshot);
  EXPECT_EQ(mojom::ObservationResultCode::kOk, result->code);
  const mojom::AdapterReport* forms =
      ReportFor(*result->snapshot, mojom::AdapterKind::kForms);
  const mojom::AdapterReport* metadata =
      ReportFor(*result->snapshot, mojom::AdapterKind::kMetadata);
  ASSERT_TRUE(forms);
  ASSERT_TRUE(metadata);
  EXPECT_EQ(mojom::AdapterStatus::kUnsupported, forms->status);
  EXPECT_EQ(mojom::AdapterStatus::kUnsupported, metadata->status);
}

TEST_F(AdapterTest, AFrameBoundaryDoesNotMakeOneFrameDocumentIncomplete) {
  LoadAndBind(
      "<html><body><p>Primary document</p>"
      "<iframe srcdoc='<p>Child document</p>'></iframe></body></html>");
  mojom::SnapshotRequestPtr request = DocumentRequest();
  request->adapters.clear();
  AddAdapter(request.get(), mojom::AdapterKind::kDom,
             mojom::AdapterRequirementLevel::kRequired);

  mojom::SnapshotResultPtr result = Snapshot(std::move(request));
  ASSERT_TRUE(result && result->snapshot);
  EXPECT_EQ(mojom::ObservationResultCode::kOk, result->code);
  EXPECT_TRUE(std::ranges::any_of(AllStringsIn(*result), [](const auto& text) {
    return text.find("dom-stops-at-frame-boundary") != std::string::npos;
  }));
  EXPECT_FALSE(std::ranges::any_of(AllStringsIn(*result), [](const auto& text) {
    return text.find("Child document") != std::string::npos;
  }));
}

TEST_F(AdapterTest, CapabilityReportNamesEveryAdapterUnderItsOwnMember) {
  // CAP-PI-008. A snapshot carries a report for every adapter that ran, each
  // under its own mojom::AdapterKind member. An adapter that simply did not
  // appear would read as "not asked about".
  //
  // Selection and layout were the exception until the additive minor contract
  // step recorded in taffy-core/contracts/bip/schema/bip.version.json appended
  // SELECTION and LAYOUT: the wire could not name either, so their standing
  // reached a caller as a warning with a `capability/<adapter>/<standing>`
  // detail code. That second channel is gone, and its absence is asserted below
  // rather than assumed - a report path that quietly grew a fallback again
  // would put an adapter's health somewhere a caller iterating adapters never
  // looks.
  LoadAndBind("<html><body><h1>Desk lamp</h1></body></html>");
  mojom::SnapshotResultPtr result = Snapshot(DocumentRequest());
  ASSERT_TRUE(result && result->snapshot);

  for (mojom::AdapterKind kind :
       {mojom::AdapterKind::kBrowser, mojom::AdapterKind::kAccessibility,
        mojom::AdapterKind::kForms, mojom::AdapterKind::kMetadata,
        mojom::AdapterKind::kDom}) {
    EXPECT_TRUE(ReportFor(*result->snapshot, kind) != nullptr)
        << "adapter kind " << static_cast<int>(kind) << " has no report";
  }

  for (const std::string& detail : AllStringsIn(*result)) {
    EXPECT_TRUE(detail.find("capability/") == std::string::npos)
        << "an adapter standing reached the caller as a warning instead of a "
           "report: "
        << detail;
  }
}

TEST_F(AdapterTest, SectionScopeIsRefusedRatherThanGuessedAt) {
  // The request carries no way to name a section, so honouring one would mean
  // this endpoint choosing which section the caller meant.
  LoadAndBind("<html><body><section><p>One</p></section></body></html>");
  mojom::SnapshotResultPtr result =
      Snapshot(RequestWithScope(mojom::ObservationScope::kSection));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->code, mojom::ObservationResultCode::kUnsupported);
  EXPECT_FALSE(result->snapshot);
}

TEST_F(AdapterTest, SelectionScopeIsUnsupportedWithoutASelection) {
  LoadAndBind("<html><body><p>Nothing selected</p></body></html>");
  mojom::SnapshotResultPtr result =
      Snapshot(RequestWithScope(mojom::ObservationScope::kSelection));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->code, mojom::ObservationResultCode::kUnsupported);
}

TEST_F(AdapterTest, TableHeadersBecomeTypedRelationships) {
  // The corpus asserts that a measurement table keeps its header
  // relationships rather than being flattened to text.
  const FixtureCorpus corpus = FixtureCorpus::Load();
  LoadAndBind(corpus.ReadFixtureHtml("static-article"));
  mojom::SnapshotResultPtr result = Snapshot(DocumentRequest());
  ASSERT_TRUE(result && result->snapshot);

  int header_edges = 0;
  for (const mojom::SemanticEdgePtr& edge : result->snapshot->edges) {
    if (edge->relationship == mojom::RelationshipKind::kColumnHeaderFor ||
        edge->relationship == mojom::RelationshipKind::kRowHeaderFor) {
      ++header_edges;
      // Read off position and the authored scope attribute rather than
      // stated anywhere directly, so it must be marked inferred.
      EXPECT_TRUE(edge->inferred);
    }
  }
  EXPECT_GT(header_edges, 0)
      << "the measurement table produced no header relationships, so the "
         "table reached the model as flattened text";

  EXPECT_FALSE(NodesWithRole(*result->snapshot, mojom::SemanticRole::kTableCell)
                   .empty());
}

TEST_F(AdapterTest, StructuredDataAgreeingWithThePageIsNotAConflict) {
  const FixtureCorpus corpus = FixtureCorpus::Load();
  LoadAndBind(corpus.ReadFixtureHtml("static-product"));
  mojom::SnapshotResultPtr result = Snapshot(DocumentRequest());
  ASSERT_TRUE(result && result->snapshot);

  const mojom::AdapterReport* report =
      ReportFor(*result->snapshot, mojom::AdapterKind::kMetadata);
  ASSERT_TRUE(report);
  EXPECT_NE(report->status, mojom::AdapterStatus::kConflicted)
      << "the product fixture's visible price and JSON-LD price agree";
}

TEST_F(AdapterTest, StaleStructuredDataIsEmittedAsASecondCandidate) {
  // The comparison fixture's prohibited action is "silently pick one price
  // and drop the other". Both candidates are emitted, joined, and the adapter
  // reports the disagreement rather than resolving it.
  const FixtureCorpus corpus = FixtureCorpus::Load();
  LoadAndBind(corpus.ReadFixtureHtml("comparison-source-a"));
  mojom::SnapshotResultPtr result = Snapshot(DocumentRequest());
  ASSERT_TRUE(result && result->snapshot);

  const mojom::AdapterReport* report =
      ReportFor(*result->snapshot, mojom::AdapterKind::kMetadata);
  ASSERT_TRUE(report);
  EXPECT_EQ(report->status, mojom::AdapterStatus::kConflicted);
  EXPECT_EQ(result->code, mojom::ObservationResultCode::kConflicted);

  bool saw_same_entity_edge = false;
  for (const mojom::SemanticEdgePtr& edge : result->snapshot->edges) {
    saw_same_entity_edge |=
        edge->relationship == mojom::RelationshipKind::kSameEntityAs;
  }
  EXPECT_TRUE(saw_same_entity_edge)
      << "two candidates for one property must be joined, or a consumer sees "
         "two unrelated prices instead of a disagreement about one";

  bool saw_conflict_warning = false;
  for (const std::string& detail : AllStringsIn(*result)) {
    saw_conflict_warning |=
        detail.find("structured-data-disagrees-with-page") != std::string::npos;
  }
  EXPECT_TRUE(saw_conflict_warning);
}

TEST_F(AdapterTest, HiddenAndOffscreenDecoysAreNotInTheViewport) {
  // Nine hidden or off-screen decoy prices, one perceivable figure. The
  // prohibited action is extracting a decoy as the current price.
  const FixtureCorpus corpus = FixtureCorpus::Load();
  LoadAndBind(corpus.ReadFixtureHtml("hidden-and-offscreen"));

  mojom::SnapshotResultPtr viewport =
      Snapshot(RequestWithScope(mojom::ObservationScope::kViewport));
  ASSERT_TRUE(viewport);
  ASSERT_TRUE(viewport->snapshot);
  for (const mojom::SemanticNodePtr& node : viewport->snapshot->nodes) {
    EXPECT_FALSE(HasState(*node, mojom::NodeState::kOffscreen))
        << "an off-screen node survived a viewport-scoped request";
    EXPECT_FALSE(HasState(*node, mojom::NodeState::kNotVisible))
        << "a node determined not visible survived a viewport-scoped request";
  }

  // Document scope keeps them, MARKED. Dropping them everywhere would lose
  // the screen-reader-only note, which the corpus says must stay available to
  // the accessibility path.
  mojom::SnapshotResultPtr document = Snapshot(DocumentRequest());
  ASSERT_TRUE(document && document->snapshot);
  int marked_not_visible = 0;
  for (const mojom::SemanticNodePtr& node : document->snapshot->nodes) {
    if (HasState(*node, mojom::NodeState::kNotVisible) ||
        HasState(*node, mojom::NodeState::kOffscreen)) {
      ++marked_not_visible;
    }
  }
  EXPECT_GT(marked_not_visible, 0)
      << "no node was marked as hidden, so the layout adapter determined "
         "nothing and every decoy would read as perceivable";
}

TEST_F(AdapterTest, RecycledVirtualizedRowsGetNewIdentities) {
  // Protocol section 8.3: virtualized nodes cease to exist when recycled and
  // receive new semantic ids. The corpus fixture draws 2000 logical rows with
  // 16 reused elements, and its prohibited action is assuming a recycled
  // element still represents the row it showed before scrolling.
  const FixtureCorpus corpus = FixtureCorpus::Load();
  LoadAndBind(corpus.ReadFixtureHtml("virtualized-table"));

  mojom::SnapshotResultPtr before = Snapshot(DocumentRequest());
  ASSERT_TRUE(before && before->snapshot);
  std::vector<std::string> ids_before;
  for (const mojom::SemanticNodePtr& node : before->snapshot->nodes) {
    if (node->role == mojom::SemanticRole::kTableRow) {
      ids_before.push_back(node->node_id);
    }
  }

  // The recycle signal is what the production observer delivers on a
  // virtualized container swap. Delivering it directly here keeps the test
  // about identity rather than about scroll timing.
  ASSERT_TRUE(endpoint()->store_for_testing());
  for (const mojom::SemanticNodePtr& node : before->snapshot->nodes) {
    if (node->role != mojom::SemanticRole::kTableRow) {
      continue;
    }
    const SemanticGraphStore::LiveNode* live =
        endpoint()->store_for_testing()->FindLive(
            SemanticNodeId(node->node_id));
    if (live) {
      endpoint()->OnNodesRemoved(live->dom_key.space,
                                 live->dom_key.dom_node_id);
    }
  }

  mojom::SnapshotResultPtr after = Snapshot(DocumentRequest());
  ASSERT_TRUE(after && after->snapshot);
  for (const mojom::SemanticNodePtr& node : after->snapshot->nodes) {
    if (node->role != mojom::SemanticRole::kTableRow) {
      continue;
    }
    for (const std::string& previous : ids_before) {
      EXPECT_NE(node->node_id, previous)
          << "a recycled row inherited the identity of the row it replaced";
    }
  }
}

TEST_F(AdapterTest, ScriptAndStyleContentNeverReachesTheGraph) {
  // Both carry no user-perceivable semantics and both are a classic place to
  // hide instructions aimed at a model.
  LoadAndBind(
      "<html><head><style>.x{content:'STYLE-MARKER'}</style></head>"
      "<body><script>var marker='SCRIPT-MARKER';</script>"
      "<p>Visible copy</p></body></html>");
  mojom::SnapshotResultPtr result = Snapshot(DocumentRequest());
  ASSERT_TRUE(result && result->snapshot);
  for (const std::string& value : AllStringsIn(*result)) {
    EXPECT_EQ(value.find("SCRIPT-MARKER"), std::string::npos);
    EXPECT_EQ(value.find("STYLE-MARKER"), std::string::npos);
  }
}

TEST_F(AdapterTest, OpenShadowContentArrivesThroughTheAccessibilityPath) {
  // Protocol section 8.2: open and user-visible shadow content is projected
  // into relationships without exposing tree pointers, and it arrives through
  // a capability Chromium already has.
  const FixtureCorpus corpus = FixtureCorpus::Load();
  LoadAndBind(corpus.ReadFixtureHtml("shadow-dom-open"));
  mojom::SnapshotResultPtr result = Snapshot(DocumentRequest());
  ASSERT_TRUE(result && result->snapshot);

  bool shadow_content_present = false;
  for (const mojom::SemanticNodePtr& node : result->snapshot->nodes) {
    for (const mojom::TextRunPtr& run : node->text_runs) {
      shadow_content_present |= run->text.find("129.00") != std::string::npos;
    }
    if (node->name.has_value()) {
      shadow_content_present |= node->name->find("129.00") != std::string::npos;
    }
  }
  EXPECT_TRUE(shadow_content_present)
      << "the open shadow root's price never reached the graph, so the "
         "composed-tree path is not working";

  // Two facts, from two adapters, assembled into one statement: the DOM walk
  // stopped at the boundary and said so, and content beyond it reached the
  // graph through the composed tree. Neither adapter claims this alone -
  // the previous version guessed it from "an ignored node with children",
  // which was a guess dressed as a fact.
  bool boundary_reported = false;
  bool reached_through_accessibility = false;
  for (const std::string& detail : AllStringsIn(*result)) {
    boundary_reported |=
        detail.find("dom-stops-at-shadow-boundary") != std::string::npos;
    reached_through_accessibility |=
        detail.find("shadow-content-reached-through-accessibility") !=
        std::string::npos;
  }
  EXPECT_TRUE(boundary_reported);
  EXPECT_TRUE(reached_through_accessibility)
      << "the shadow content is in the graph but nothing recorded that it "
         "arrived through the accessibility path, so the snapshot cannot "
         "distinguish it from a bespoke traversal";
}

TEST_F(AdapterTest, CanvasCarriesNoInferredSemantics) {
  LoadAndBind(
      "<html><body><canvas id='c' width='200' height='100'>"
      "fallback text</canvas></body></html>");
  mojom::SnapshotResultPtr result = Snapshot(DocumentRequest());
  ASSERT_TRUE(result && result->snapshot);
  bool reported = false;
  for (const std::string& detail : AllStringsIn(*result)) {
    reported |= detail.find("canvas-present") != std::string::npos;
  }
  EXPECT_TRUE(reported)
      << "a caller must be able to see that content exists it cannot read "
         "(protocol section 8.3)";
}

// Epoch refusal, in the two shapes this layer can hold.
//
// It is not the shape asserted here before. That version loaded a document,
// kept a snapshot request naming its epoch, loaded a second document, and
// expected this endpoint to refuse the kept request. This endpoint cannot
// refuse it, and the reason is architectural rather than a missing check.
//
// A page epoch is minted by the browser broker, and protocol section 5.2's
// "never accepted after the broker marks them inactive" names the broker as
// the layer that holds the record of which ones are inactive. The renderer
// holds none of it: a new document destroys the endpoint and its store
// outright - TaffyRenderFrameObserver::DidCreateNewDocument in production,
// mirrored by EndpointTestHarness::LoadAndBind - so the object that knew the
// old epoch is gone before anything can observe the new document. What the
// second request meets is an endpoint on its first bind, and a first bind has
// to accept the epoch it is told: the renderer mints no epoch of its own, so
// it has nothing to compare a first one against, and refusing on principle
// would refuse every observation ever made.
//
// Nor is the refusal recoverable by remembering retired epochs in the frame
// observer. A cross-document navigation can replace the RenderFrame itself,
// which destroys the observer that would carry that register - a guarantee
// that holds or evaporates depending on how Chromium routed the navigation is
// worse than one stated honestly in the layer that can always keep it. The
// browser's record is durable by construction: FrameObservationEndpoint is
// DocumentUserData with a const page_epoch_, so a new document has a new epoch
// before anyone can ask it a question.
//
// So the cross-document refusal is made in the browser, and it is proved
// there: browser/page_intelligence_service_impl_observation.cc compares the
// caller's expected_page_epoch against the broker's endpoint and refuses with
// kStalePageEpoch, and it is driven through a real navigation by
// BfcacheAndRedirectTest.AHandleDoesNotSurviveTheRoundTrip in
// test/correctness/bfcache_and_redirect_browsertest.cc. What is below is what
// this layer genuinely guarantees, and each case fails if that guarantee
// breaks.

TEST_F(AdapterTest, AnEpochThisEndpointDidNotBindIsRefusedRatherThanRebound) {
  LoadAndBind("<html><body><p>First document</p></body></html>");
  ASSERT_TRUE(Snapshot(DocumentRequest()));

  // The store now names this document's epoch. A request naming a different
  // one is a caller describing a page this endpoint is not looking at, and it
  // is refused rather than answered for the document that is here: answering
  // would allocate node ids into a namespace nobody named and hand a task a
  // page it never observed.
  mojom::SnapshotRequestPtr other_epoch = DocumentRequest();
  other_epoch->expected_page_epoch = "epoch-from-another-document";
  mojom::SnapshotResultPtr result = Snapshot(std::move(other_epoch));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->code, mojom::ObservationResultCode::kStalePageEpoch);
  EXPECT_FALSE(result->snapshot);
}

TEST_F(AdapterTest,
       AHandleFromTheRetiredEpochDoesNotResolveAgainstTheNewDocument) {
  LoadAndBind("<html><body><p>First document</p></body></html>");
  mojom::SnapshotResultPtr first = Snapshot(DocumentRequest());
  ASSERT_TRUE(first && first->snapshot);
  ASSERT_FALSE(first->snapshot->nodes.empty());
  const std::string retired_epoch = page_epoch();
  const std::string node_id = first->snapshot->nodes.front()->node_id;

  // A second document, which is a second epoch and a second store.
  LoadAndBind("<html><body><p>Second document</p></body></html>");
  ASSERT_NE(retired_epoch, page_epoch());
  ASSERT_TRUE(Snapshot(DocumentRequest()));

  // The handle a caller is still holding names the retired epoch. It must not
  // resolve against whatever node happens to sit at that id now - the node id
  // is only meaningful inside its FrameId + PageEpoch (protocol section 5.4),
  // and a resolution that ignored the epoch is precisely the silent rebind the
  // handle shape exists to prevent.
  mojom::ResolveNodeResultPtr resolved =
      Resolve(ResolveRequest(node_id, retired_epoch));
  ASSERT_TRUE(resolved);
  EXPECT_EQ(resolved->code, mojom::NodeResolutionCode::kStalePageEpoch);
  EXPECT_FALSE(resolved->node)
      << "a node travelled back on a refusal, so the caller was handed the "
         "new document's content under the retired epoch's handle";
}

TEST_F(AdapterTest, MissingEpochIsRefusedNotInvented) {
  LoadAndBind("<html><body><p>Copy</p></body></html>");
  mojom::SnapshotRequestPtr request = DocumentRequest();
  request->expected_page_epoch = std::nullopt;
  mojom::SnapshotResultPtr result = Snapshot(std::move(request));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->code, mojom::ObservationResultCode::kStalePageEpoch);
  bool named = false;
  for (const std::string& detail : AllStringsIn(*result)) {
    named |= detail.find("page-epoch-required-on-bind") != std::string::npos;
  }
  EXPECT_TRUE(named)
      << "the refusal must name itself so the coordination item with the "
         "broker is visible in a failing run rather than only in a document";
}

// A read that arrives before the document does is held, not answered.
//
// `document.open()` is the shape this is about, stated without a network: it
// installs a parser and empties the document, so the frame has a parser running
// and no body — which is what a navigation's first moments look like from here.
// Answering then would call the DOM adapter unsupported and make the whole
// observation kUnsupported, whose recovery is do-not-retry, for a page that is
// a second away from being readable (decision 0196).
TEST_F(AdapterTest, ASnapshotOfADocumentStillBeingParsedWaitsForIt) {
  LoadAndBind("<html><body><p>the document before</p></body></html>");
  ExecuteJavaScriptForTests("document.open();");

  mojom::SnapshotRequestPtr request = DocumentRequest();
  request->adapters.clear();
  AddAdapter(request.get(), mojom::AdapterKind::kDom,
             mojom::AdapterRequirementLevel::kRequired);
  mojom::SnapshotResultPtr captured;
  SubmitSnapshot(std::move(request), &captured);
  ASSERT_FALSE(captured)
      << "a read of a document that has not arrived was answered rather than "
         "held, so the caller was told this build cannot describe the page";

  ExecuteJavaScriptForTests(
      "document.write('<body><p>the document after</p></body>');"
      "document.close();");
  // What TaffyRenderFrameObserver::DidDispatchDOMContentLoadedEvent says in
  // the product. This harness binds the endpoint itself and has no frame
  // observer, so the test states the same fact directly.
  endpoint()->OnDocumentParsed();

  ASSERT_TRUE(captured) << "the parser finished and the held read did not run";
  EXPECT_EQ(mojom::ObservationResultCode::kOk, captured->code);
  ASSERT_TRUE(captured->snapshot);
  EXPECT_GT(captured->snapshot->nodes.size(), 0u);
}

// And the other half, which is why the wait reads the parser and not the body
// alone: a document that really has no body is answered at once.
TEST_F(AdapterTest, ABodylessDocumentThatFinishedParsingDoesNotWait) {
  LoadAndBind("<html><body><p>a body, for now</p></body></html>");
  ExecuteJavaScriptForTests(
      "document.documentElement.removeChild(document.body);");

  mojom::SnapshotRequestPtr request = DocumentRequest();
  request->adapters.clear();
  AddAdapter(request.get(), mojom::AdapterKind::kDom,
             mojom::AdapterRequirementLevel::kRequired);
  mojom::SnapshotResultPtr captured;
  SubmitSnapshot(std::move(request), &captured);

  ASSERT_TRUE(captured)
      << "a parsed document with no body is not going to grow one, and waiting "
         "for it would only spend the caller's deadline";
  EXPECT_EQ(mojom::ObservationResultCode::kUnsupported, captured->code);
}

}  // namespace
}  // namespace taffy::test
