// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "base/test/bind.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "taffy/browser/core_page_observation_broker_test_support.h"
#include "taffy/common/public/bip_observation.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {

namespace mojom = core_service::mojom;

namespace {

mojom::EffectEnvelopePtr DirectEffect() {
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = mojom::OperationEnvelope::New("operation-1", 1u, 0u,
                                                    10'000u, "idempotency-1");
  effect->effect_id = "direct-observation-1";
  effect->kind = mojom::EffectKind::kPageObservation;
  effect->retry_class = mojom::RetryClass::kIdempotent;
  effect->page_observation = mojom::PageObservationEffect::New();
  effect->page_observation->tab_id = "tab-1";
  effect->page_observation->frame_id = "frame-1";
  effect->page_observation->page_epoch = "epoch-1";
  effect->page_observation->scope = mojom::ObservationScope::kCurrentDocument;
  effect->page_observation->max_bytes = 4096u;
  effect->page_observation->authority_subject = mojom::AuthoritySubject::New();
  effect->page_observation->authority_subject->kind =
      mojom::AuthoritySubjectKind::kDirectUserIntent;
  effect->page_observation->authority_subject->authority_subject_id =
      "direct-intent-1";
  effect->page_observation->capability_id = "capability-1";
  effect->page_observation->proposal_digest = std::string(64u, 'a');
  effect->page_observation->idempotency_key = "idempotency-1";
  effect->page_observation->max_nodes = 8u;
  effect->page_observation->max_text_bytes = 1024u;
  effect->page_observation->max_frames = 1u;
  effect->page_observation->deadline_ms = 1500u;
  effect->page_observation->expected_graph_revision = 3u;
  return effect;
}

ObservationEnvelope Observation() {
  ObservationEnvelope observation;
  observation.code = ObservationResultCode::kOk;
  observation.schema_version = "0.9";
  observation.tab_id = TabId{"tab-1"};
  observation.root_frame_id = FrameId{"frame-1"};
  observation.page_epoch = PageEpoch{"epoch-1"};
  observation.graph_revision = 3u;
  observation.lifecycle_state = DocumentLifecycleState::kActive;
  observation.origin = Origin{
      .kind = OriginKind::kTuple,
      .serialization = "https://example.test",
  };
  observation.is_potentially_trustworthy = true;
  observation.scope = ObservationScope::kDocument;
  observation.frames.push_back(FrameSummary{
      .is_main_frame = true,
      .lifecycle_state = DocumentLifecycleState::kActive,
      .included = true,
  });
  observation.node_count = 1u;
  observation.total_bytes = 4u;
  observation.encoding = GraphPayloadEncoding::kBipContract;
  observation.graph_payload = {1u, 2u, 3u, 4u};
  InspectorGraphProjection projection;
  projection.nodes.push_back(InspectorNodeProjection{
      .display_id = "node-1",
      .role = InspectorNodeRole::kSection,
      .name = "Example",
      .sensitivity = InspectorSensitivity::kPublic,
      .text_run_count = 1u,
      .text_byte_count = 7u,
  });
  observation.inspector_projection = std::move(projection);
  return observation;
}

AuthorizedObservationTarget ImageTarget() {
  AuthorizedObservationTarget target;
  target.kind = AuthorizedObservationKind::kImageDescription;
  target.scope = ObservationScope::kDocument;
  target.media_root = MediaObservationRoot{
      .node_id = SemanticNodeId{"image-1"},
      .minimum_graph_revision = 3u,
  };
  return target;
}

ObservationEnvelope ImageObservation() {
  ObservationEnvelope observation = Observation();
  observation.media_root = MediaObservationRoot{
      .node_id = SemanticNodeId{"image-1"},
      .minimum_graph_revision = 3u,
  };
  return observation;
}

mojom::MediaObservationResultPtr ImageMedia() {
  auto media = mojom::MediaObservationResult::New();
  media->kind = mojom::MediaObservationKind::kImage;
  media->attachment_handle = "media-handle-1";
  media->attachment_mime_type = "image/png";
  media->width_px = 320u;
  media->height_px = 200u;
  return media;
}

AuthorizedObservationTarget ScreenshotTarget() {
  AuthorizedObservationTarget target;
  target.kind = AuthorizedObservationKind::kPageScreenshot;
  target.scope = ObservationScope::kDocument;
  return target;
}

ObservationEnvelope ScreenshotObservation() {
  ObservationEnvelope observation = Observation();
  observation.capture_time_monotonic_ms = 40u;
  observation.node_count = 0u;
  observation.redaction.sensitive_zone_count = 1u;
  return observation;
}

mojom::MediaObservationResultPtr ScreenshotMedia() {
  auto media = mojom::MediaObservationResult::New();
  media->kind = mojom::MediaObservationKind::kPageScreenshot;
  media->attachment_handle = "page-screenshot-handle";
  media->attachment_mime_type = "image/png";
  media->width_px = 960u;
  media->height_px = 480u;
  media->capture_provenance = mojom::MediaCaptureProvenance::New();
  media->capture_provenance->capture_width_dip = 2000u;
  media->capture_provenance->capture_height_dip = 1000u;
  media->capture_provenance->viewport_width_dip = 2000u;
  media->capture_provenance->viewport_height_dip = 1000u;
  media->capture_provenance->output_scale_ppm = 480000u;
  media->capture_provenance->captured_at_monotonic_ms = 41u;
  media->capture_provenance->redacted_region_count = 1u;
  return media;
}

TEST(CorePageObservationBrokerTest,
     MismatchedEnvelopeNeverReachesTheDirectProjection) {
  content::BrowserTaskEnvironment task_environment;
  auto context = std::make_unique<content::TestBrowserContext>();
  auto broker = CorePageObservationBrokerTestPeer::Create(context.get());
  ObservationEnvelope observation = Observation();
  observation.tab_id = TabId{"different-tab"};

  mojom::EffectResultPtr terminal;
  CorePageObservationBrokerTestPeer::Complete(
      *broker, DirectEffect(),
      base::BindLambdaForTesting(
          [&](mojom::EffectResultPtr result) { terminal = std::move(result); }),
      std::move(observation));

  ASSERT_TRUE(terminal);
  EXPECT_EQ(mojom::EffectStatus::kInvalidResult, terminal->status);
  ASSERT_TRUE(terminal->observation);
  EXPECT_TRUE(terminal->observation->graph_payload.empty());
  EXPECT_EQ(mojom::BipGraphEncoding::kNone,
            terminal->observation->graph_encoding);
  EXPECT_FALSE(broker->TakeDirectProjection("direct-observation-1"));
}

TEST(CorePageObservationBrokerTest,
     MatchingEnvelopeProducesOneProjectionAndOnePayload) {
  content::BrowserTaskEnvironment task_environment;
  auto context = std::make_unique<content::TestBrowserContext>();
  auto broker = CorePageObservationBrokerTestPeer::Create(context.get());

  mojom::EffectResultPtr terminal;
  CorePageObservationBrokerTestPeer::Complete(
      *broker, DirectEffect(),
      base::BindLambdaForTesting(
          [&](mojom::EffectResultPtr result) { terminal = std::move(result); }),
      Observation());

  ASSERT_TRUE(terminal);
  EXPECT_EQ(mojom::EffectStatus::kCompleted, terminal->status);
  ASSERT_TRUE(terminal->observation);
  EXPECT_EQ((std::vector<uint8_t>{1u, 2u, 3u, 4u}),
            terminal->observation->graph_payload);
  EXPECT_EQ(mojom::BipGraphEncoding::kBipContract,
            terminal->observation->graph_encoding);
  EXPECT_TRUE(broker->TakeDirectProjection("direct-observation-1"));
  EXPECT_FALSE(broker->TakeDirectProjection("direct-observation-1"));
}

// A page whose own structured data disagrees with its own text is a fact
// about that page, not a failure to read it. The renderer attaches a snapshot
// for `kConflicted` by contract, and both `ObservationResultBuilder` and
// `ObservedLinkRegistry::Replace` have admitted it as a reading for as long as
// they have stated the rule. This broker was the copy that disagreed: it
// answered `kDenied`, which the bridge settles as `DeniedByPolicy` and the
// recovery table reads as `DoNotRetry`. On a phone that threw away a complete
// 922-node reading of the page an errand had walked to, twice, and the model
// correctly concluded it was not allowed to look and handed the page back
// (decision 0207).
TEST(CorePageObservationBrokerTest, APageThatDisagreesWithItselfIsStillARead) {
  content::BrowserTaskEnvironment task_environment;
  auto context = std::make_unique<content::TestBrowserContext>();
  auto broker = CorePageObservationBrokerTestPeer::Create(context.get());
  ObservationEnvelope observation = Observation();
  observation.code = ObservationResultCode::kConflicted;

  mojom::EffectResultPtr terminal;
  CorePageObservationBrokerTestPeer::Complete(
      *broker, DirectEffect(),
      base::BindLambdaForTesting(
          [&](mojom::EffectResultPtr result) { terminal = std::move(result); }),
      std::move(observation));

  ASSERT_TRUE(terminal);
  EXPECT_EQ(mojom::EffectStatus::kCompleted, terminal->status);
  ASSERT_TRUE(terminal->observation);
  // The caveat still travels: the code is preserved exactly, so a consumer
  // knows the graph carries competing candidates.
  EXPECT_EQ(mojom::BipObservationStatus::kConflicted,
            terminal->observation->status);
  // And the graph it was a caveat on arrives.
  EXPECT_EQ((std::vector<uint8_t>{1u, 2u, 3u, 4u}),
            terminal->observation->graph_payload);
  EXPECT_EQ(mojom::BipGraphEncoding::kBipContract,
            terminal->observation->graph_encoding);
  EXPECT_TRUE(broker->TakeDirectProjection("direct-observation-1"));
}

TEST(CorePageObservationBrokerTest,
     AnUnclaimedDirectProjectionCannotAccumulatePageText) {
  content::BrowserTaskEnvironment task_environment;
  auto context = std::make_unique<content::TestBrowserContext>();
  auto broker = CorePageObservationBrokerTestPeer::Create(context.get());

  CorePageObservationBrokerTestPeer::Complete(
      *broker, DirectEffect(),
      base::BindLambdaForTesting([](mojom::EffectResultPtr) {}), Observation());
  mojom::EffectEnvelopePtr second = DirectEffect();
  second->effect_id = "direct-observation-2";
  CorePageObservationBrokerTestPeer::Complete(
      *broker, std::move(second),
      base::BindLambdaForTesting([](mojom::EffectResultPtr) {}), Observation());

  EXPECT_FALSE(broker->TakeDirectProjection("direct-observation-1"));
  EXPECT_TRUE(broker->TakeDirectProjection("direct-observation-2"));
}

TEST(CorePageObservationBrokerTest, FormRootMustMatchBrowserAuthorityExactly) {
  content::BrowserTaskEnvironment task_environment;
  auto context = std::make_unique<content::TestBrowserContext>();
  auto broker = CorePageObservationBrokerTestPeer::Create(context.get());
  AuthorizedObservationTarget target;
  target.scope = ObservationScope::kSection;
  target.form_root = FormObservationRoot{
      .node_id = SemanticNodeId{"form-41"},
      .minimum_graph_revision = 3u,
  };
  ObservationEnvelope observation = Observation();
  observation.scope = ObservationScope::kSection;
  observation.form_root = FormObservationRoot{
      .node_id = SemanticNodeId{"form-42"},
      .minimum_graph_revision = 3u,
  };

  mojom::EffectResultPtr terminal;
  CorePageObservationBrokerTestPeer::Complete(
      *broker, DirectEffect(),
      base::BindLambdaForTesting(
          [&](mojom::EffectResultPtr result) { terminal = std::move(result); }),
      std::move(observation), std::move(target));

  ASSERT_TRUE(terminal);
  EXPECT_EQ(mojom::EffectStatus::kInvalidResult, terminal->status);
  ASSERT_TRUE(terminal->observation);
  EXPECT_TRUE(terminal->observation->graph_payload.empty());
}

TEST(CorePageObservationBrokerTest, SelectionCannotBeSubstitutedByDocument) {
  content::BrowserTaskEnvironment task_environment;
  auto context = std::make_unique<content::TestBrowserContext>();
  auto broker = CorePageObservationBrokerTestPeer::Create(context.get());
  AuthorizedObservationTarget target;
  target.scope = ObservationScope::kSelection;

  mojom::EffectResultPtr terminal;
  CorePageObservationBrokerTestPeer::Complete(
      *broker, DirectEffect(),
      base::BindLambdaForTesting(
          [&](mojom::EffectResultPtr result) { terminal = std::move(result); }),
      Observation(), std::move(target));

  ASSERT_TRUE(terminal);
  EXPECT_EQ(mojom::EffectStatus::kInvalidResult, terminal->status);
  ASSERT_TRUE(terminal->observation);
  EXPECT_TRUE(terminal->observation->graph_payload.empty());
}

TEST(CorePageObservationBrokerTest,
     ExactMediaResultCarriesOnlyOpaqueAttachmentFacts) {
  content::BrowserTaskEnvironment task_environment;
  auto context = std::make_unique<content::TestBrowserContext>();
  auto broker = CorePageObservationBrokerTestPeer::Create(context.get());

  mojom::EffectResultPtr terminal;
  CorePageObservationBrokerTestPeer::Finish(
      *broker, DirectEffect(),
      base::BindLambdaForTesting(
          [&](mojom::EffectResultPtr result) { terminal = std::move(result); }),
      ImageTarget(), ImageObservation(), ImageMedia());

  ASSERT_TRUE(terminal);
  EXPECT_EQ(mojom::EffectStatus::kCompleted, terminal->status);
  ASSERT_TRUE(terminal->observation);
  ASSERT_TRUE(terminal->observation->media);
  EXPECT_EQ("media-handle-1", terminal->observation->media->attachment_handle);
  EXPECT_EQ("image/png", terminal->observation->media->attachment_mime_type);
  EXPECT_EQ(320u, terminal->observation->media->width_px);
  EXPECT_EQ((std::vector<uint8_t>{1u, 2u, 3u, 4u}),
            terminal->observation->graph_payload);
}

TEST(CorePageObservationBrokerTest,
     MissingRequiredMediaWithdrawsThePartialGraph) {
  content::BrowserTaskEnvironment task_environment;
  auto context = std::make_unique<content::TestBrowserContext>();
  auto broker = CorePageObservationBrokerTestPeer::Create(context.get());

  mojom::EffectResultPtr terminal;
  CorePageObservationBrokerTestPeer::Finish(
      *broker, DirectEffect(),
      base::BindLambdaForTesting(
          [&](mojom::EffectResultPtr result) { terminal = std::move(result); }),
      ImageTarget(), ImageObservation(), nullptr);

  ASSERT_TRUE(terminal);
  EXPECT_EQ(mojom::EffectStatus::kUnavailable, terminal->status);
  ASSERT_TRUE(terminal->observation);
  EXPECT_FALSE(terminal->observation->media);
  EXPECT_TRUE(terminal->observation->graph_payload.empty());
  EXPECT_EQ(mojom::BipGraphEncoding::kNone,
            terminal->observation->graph_encoding);
}

TEST(CorePageObservationBrokerTest,
     ScreenshotFallbackPublishesOnlyMatchingCaptureProvenance) {
  content::BrowserTaskEnvironment task_environment;
  auto context = std::make_unique<content::TestBrowserContext>();
  auto broker = CorePageObservationBrokerTestPeer::Create(context.get());

  mojom::EffectResultPtr terminal;
  CorePageObservationBrokerTestPeer::Finish(
      *broker, DirectEffect(),
      base::BindLambdaForTesting(
          [&](mojom::EffectResultPtr result) { terminal = std::move(result); }),
      ScreenshotTarget(), ScreenshotObservation(), ScreenshotMedia());

  ASSERT_TRUE(terminal && terminal->observation && terminal->observation->media);
  EXPECT_EQ(mojom::EffectStatus::kCompleted, terminal->status);
  ASSERT_TRUE(terminal->observation->media->capture_provenance);
  EXPECT_EQ(1u, terminal->observation->media->capture_provenance
                    ->redacted_region_count);

  mojom::MediaObservationResultPtr invalid = ScreenshotMedia();
  invalid->capture_provenance->redacted_region_count = 0u;
  CorePageObservationBrokerTestPeer::Finish(
      *broker, DirectEffect(),
      base::BindLambdaForTesting(
          [&](mojom::EffectResultPtr result) { terminal = std::move(result); }),
      ScreenshotTarget(), ScreenshotObservation(), std::move(invalid));
  ASSERT_TRUE(terminal && terminal->observation);
  EXPECT_EQ(mojom::EffectStatus::kInvalidResult, terminal->status);
  EXPECT_TRUE(terminal->observation->graph_payload.empty());
  EXPECT_FALSE(terminal->observation->media);
}

TEST(CorePageObservationBrokerTest,
     MediaRescanCountIsPublishedWithoutRetainingRemovedText) {
  content::BrowserTaskEnvironment task_environment;
  auto context = std::make_unique<content::TestBrowserContext>();
  auto broker = CorePageObservationBrokerTestPeer::Create(context.get());
  ObservationEnvelope observation = ImageObservation();
  observation.redaction.suppressed_secret_value_count = 2u;

  mojom::EffectResultPtr terminal;
  CorePageObservationBrokerTestPeer::CompleteMedia(
      *broker, DirectEffect(),
      base::BindLambdaForTesting(
          [&](mojom::EffectResultPtr result) { terminal = std::move(result); }),
      ImageTarget(), std::move(observation), ImageMedia(), 3u);

  ASSERT_TRUE(terminal);
  ASSERT_TRUE(terminal->observation);
  EXPECT_EQ(terminal->observation->suppressed_secret_value_count, 5u);
}

}  // namespace
}  // namespace taffy
