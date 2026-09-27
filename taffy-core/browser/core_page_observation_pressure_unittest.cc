// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <optional>
#include <utility>

#include "base/memory_coordinator/test_memory_consumer_registry.h"
#include "base/test/bind.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/test_renderer_host.h"
#include "taffy/browser/core_page_observation_broker_test_support.h"
#include "taffy/components/intelligence/content/frame_observation_endpoint.h"
#include "taffy/components/intelligence/content/page_intelligence_service_impl.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

class PressureObservationSink : public PageIntelligenceResultSink {
 public:
  void OnObservationResult(ObservationEnvelope result) override {
    ++count;
    observation = std::move(result);
  }
  void OnProtocolSupport(ProtocolSupportEnvelope) override {}
  void OnSubscriptionResult(SubscriptionEnvelope) override {}
  void OnActionResult(ActionResult) override {}
  void OnDelta(DeltaEnvelope) override {}
  void OnPageInvalidated(InvalidationNotice) override {}
  void OnBackpressure(BackpressureNotice) override {}
  void OnActorLeasePreempted(ActorLeaseId, TabId) override {}

  size_t count = 0u;
  std::optional<ObservationEnvelope> observation;
};

// Exercise the real producer before it can send GetSnapshot, then the real
// broker's result conversion. A hand-built failure envelope would miss the
// fields the service itself dropped in the product's synchronous refusal.
class CorePageObservationPressureTest
    : public content::RenderViewHostTestHarness {
 protected:
  void SetUp() override {
    memory_ = std::make_unique<base::TestMemoryConsumerRegistry>();
    content::RenderViewHostTestHarness::SetUp();
    NavigateAndCommit(GURL("https://observation.example/page"));
    PageIntelligenceBroker::CreateForWebContents(web_contents());
    service_ = std::make_unique<PageIntelligenceServiceImpl>(
        web_contents(), page_broker(), &sink_, nullptr, nullptr, &leases_,
        &capabilities_);
    ASSERT_TRUE(page_broker()->GetOrCreateActionableEndpoint(frame_id()));
    ASSERT_EQ(DegradationStage::kNormal, service_->degradation_stage());
  }

  void TearDown() override {
    service_.reset();
    content::RenderViewHostTestHarness::TearDown();
    memory_.reset();
  }

  PageIntelligenceBroker* page_broker() {
    return PageIntelligenceBroker::FromWebContents(web_contents());
  }

  FrameId frame_id() {
    return page_broker()->GetOrAssignFrameId(
        web_contents()->GetPrimaryMainFrame());
  }

  FrameObservationEndpoint* endpoint() {
    return page_broker()->GetActionableEndpoint(frame_id());
  }

  ObservationRequest Request() {
    ObservationRequest request;
    request.authority_subject =
        ObservationAuthoritySubject::ForTask(TaskId{"task-pressure"});
    request.tab_id = page_broker()->tab_id();
    request.root_frame_id = frame_id();
    request.expected_page_epoch = endpoint()->page_epoch();
    request.scope = ObservationScope::kDocument;
    request.budget = {8u, 1024u, 4096u, 8u, 1u, 1000u};
    return request;
  }

  std::optional<ObservationEnvelope> Refuse(ObservationRequest request,
                                            int limit = 0) {
    memory_->NotifyUpdateMemoryLimit(limit);
    const size_t before = sink_.count;
    sink_.observation.reset();
    ObservationPolicyGrant grant;
    grant.max_scope = request.scope;
    grant.budget = request.budget;
    const RequestId id =
        service_->SubmitObservation(std::move(request), std::move(grant));
    EXPECT_TRUE(id.is_valid());
    EXPECT_EQ(before + 1u, sink_.count);
    if (sink_.observation) {
      EXPECT_EQ(id, sink_.observation->request_id);
      EXPECT_EQ(ObservationResultCode::kResourcePressure,
                sink_.observation->code);
      EXPECT_EQ(0u, sink_.observation->node_count);
      EXPECT_EQ(0u, sink_.observation->total_bytes);
      EXPECT_TRUE(sink_.observation->graph_payload.empty());
      EXPECT_TRUE(sink_.observation->frames.empty());
      EXPECT_TRUE(sink_.observation->transient_observed_links.empty());
      EXPECT_FALSE(sink_.observation->inspector_projection);
      EXPECT_EQ(GraphPayloadEncoding::kNone, sink_.observation->encoding);
    }
    return std::move(sink_.observation);
  }

  mojom::EffectResultPtr Convert(const ObservationRequest& request,
                                 GraphRevision floor,
                                 ObservationEnvelope observation) {
    auto broker = CorePageObservationBrokerTestPeer::Create(browser_context());
    auto effect = mojom::EffectEnvelope::New();
    effect->operation = mojom::OperationEnvelope::New(
        "pressure-operation", 1u, 1u, 1000u, "pressure-idempotency");
    effect->effect_id = "pressure-effect";
    effect->kind = mojom::EffectKind::kPageObservation;
    effect->page_observation = mojom::PageObservationEffect::New();
    auto& requested = *effect->page_observation;
    requested.tab_id = request.tab_id.value;
    requested.frame_id = request.root_frame_id.value;
    requested.page_epoch = request.expected_page_epoch->value;
    requested.expected_graph_revision = floor;
    requested.max_nodes = request.budget.max_nodes;
    requested.max_frames = request.budget.max_frames;
    requested.max_bytes = request.budget.max_total_bytes;
    AuthorizedObservationTarget target;
    target.scope = request.scope;
    target.form_root = request.form_root;
    target.media_root = request.media_root;
    target.kind = request.form_root ? AuthorizedObservationKind::kForm
                  : request.media_root
                      ? AuthorizedObservationKind::kImageDescription
                      : AuthorizedObservationKind::kDocument;
    mojom::EffectResultPtr result;
    CorePageObservationBrokerTestPeer::Complete(
        *broker, std::move(effect),
        base::BindLambdaForTesting(
            [&](mojom::EffectResultPtr value) { result = std::move(value); }),
        std::move(observation), std::move(target));
    EXPECT_FALSE(broker->TakeDirectProjection("pressure-effect"));
    return result;
  }

  std::unique_ptr<base::TestMemoryConsumerRegistry> memory_;
  PressureObservationSink sink_;
  ActorLeaseRegistry leases_;
  CapabilityLedger capabilities_;
  std::unique_ptr<PageIntelligenceServiceImpl> service_;
};

TEST_F(CorePageObservationPressureTest,
       FirstReadRefusalRetainsDocumentIdentityWithoutInventingARevision) {
  const ObservationRequest request = Request();
  ASSERT_EQ(0u, endpoint()->last_reported_revision());
  auto observation = Refuse(request);
  ASSERT_TRUE(observation);
  EXPECT_EQ(request.root_frame_id, observation->root_frame_id);
  EXPECT_EQ(request.expected_page_epoch, observation->page_epoch);
  EXPECT_EQ(request.scope, observation->scope);
  EXPECT_EQ(0u, observation->graph_revision);
  auto result = Convert(request, 0u, std::move(*observation));
  ASSERT_TRUE(result && result->observation);
  EXPECT_EQ(mojom::EffectStatus::kResourceLimit, result->status);
  EXPECT_EQ(mojom::BipObservationStatus::kResourcePressure,
            result->observation->status);
  EXPECT_TRUE(result->observation->graph_payload.empty());
}

TEST_F(CorePageObservationPressureTest,
       RefreshRefusalUsesTheRendererReportedRevisionRatherThanTheFloor) {
  const ObservationRequest request = Request();
  endpoint()->NoteReportedRevision(17u);
  auto observation = Refuse(request);
  ASSERT_TRUE(observation);
  EXPECT_EQ(17u, observation->graph_revision);
  auto result = Convert(request, 11u, std::move(*observation));
  ASSERT_TRUE(result && result->observation);
  EXPECT_EQ(mojom::EffectStatus::kResourceLimit, result->status);
  EXPECT_EQ(17u, result->observation->graph_revision);
}

TEST_F(CorePageObservationPressureTest,
       ExactRootsStayCorrelatedWhenPressureWouldNarrowTheirScope) {
  endpoint()->NoteReportedRevision(17u);
  for (bool form : {true, false}) {
    ObservationRequest request = Request();
    if (form) {
      request.scope = ObservationScope::kSection;
      request.form_root = FormObservationRoot{SemanticNodeId{"form-1"}, 11u};
    } else {
      request.media_root = MediaObservationRoot{SemanticNodeId{"image-1"}, 11u};
    }
    auto observation = Refuse(request, 50);
    ASSERT_TRUE(observation);
    EXPECT_EQ(request.form_root, observation->form_root);
    EXPECT_EQ(request.media_root, observation->media_root);
    auto result = Convert(request, 11u, std::move(*observation));
    ASSERT_TRUE(result && result->observation);
    EXPECT_EQ(mojom::EffectStatus::kResourceLimit, result->status);
    EXPECT_FALSE(result->observation->media);
  }
}

TEST_F(CorePageObservationPressureTest, ARefusalCannotClaimTheRequestedEpoch) {
  ObservationRequest request = Request();
  request.expected_page_epoch = PageEpoch{"different-epoch"};
  auto observation = Refuse(request);
  ASSERT_TRUE(observation);
  EXPECT_NE(request.expected_page_epoch, observation->page_epoch);
  auto result = Convert(request, 0u, std::move(*observation));
  // Decision 0177: a reading that produced nothing reports the code that
  // describes why, rather than being overwritten with `kInvalidResult`. The
  // refusal short-circuits before the override, so `kResourcePressure` maps
  // straight through - the same shape the green sibling above asserts.
  ASSERT_TRUE(result && result->observation);
  EXPECT_EQ(mojom::EffectStatus::kResourceLimit, result->status);
  EXPECT_TRUE(result->observation->graph_payload.empty());
}

TEST_F(CorePageObservationPressureTest, ARetiredEndpointCannotBeRebound) {
  const ObservationRequest request = Request();
  endpoint()->Retire();
  auto observation = Refuse(request);
  ASSERT_TRUE(observation);
  EXPECT_FALSE(observation->page_epoch.is_valid());
  auto result = Convert(request, 0u, std::move(*observation));
  // Decision 0177: a reading that produced nothing reports the code that
  // describes why, rather than being overwritten with `kInvalidResult`. The
  // refusal short-circuits before the override, so `kResourcePressure` maps
  // straight through - the same shape the green sibling above asserts.
  ASSERT_TRUE(result && result->observation);
  EXPECT_EQ(mojom::EffectStatus::kResourceLimit, result->status);
  EXPECT_TRUE(result->observation->graph_payload.empty());
  EXPECT_FALSE(page_broker()->GetActionableEndpoint(request.root_frame_id));
}

TEST_F(CorePageObservationPressureTest, AMissingFrameCannotBorrowTheLivePage) {
  ObservationRequest request = Request();
  request.root_frame_id = FrameId{"missing-frame"};
  auto observation = Refuse(request);
  ASSERT_TRUE(observation);
  EXPECT_FALSE(observation->page_epoch.is_valid());
  auto result = Convert(request, 0u, std::move(*observation));
  // Decision 0177: a reading that produced nothing reports the code that
  // describes why, rather than being overwritten with `kInvalidResult`. The
  // refusal short-circuits before the override, so `kResourcePressure` maps
  // straight through - the same shape the green sibling above asserts.
  ASSERT_TRUE(result && result->observation);
  EXPECT_EQ(mojom::EffectStatus::kResourceLimit, result->status);
  EXPECT_TRUE(result->observation->graph_payload.empty());
}

}  // namespace
}  // namespace taffy
