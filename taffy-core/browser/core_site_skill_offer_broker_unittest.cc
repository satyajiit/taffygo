// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_site_skill_offer_broker.h"

#include <string>
#include <utility>

#include "base/test/bind.h"
#include "content/public/browser/web_contents.h"
#include "taffy/browser/task_source_selection_registry_actions_test_support.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;
namespace service = core_service::mojom;
using task_source_selection_test::TaskSourceSelectionRegistryActionsTest;

TEST_F(TaskSourceSelectionRegistryActionsTest,
       SavedFlowOfferKeepsExactPageBindingWithBoundedErrandDiscovery) {
  SetGraphRevision(web_contents(), 7u);
  auto* host = TaffyPageIntelligenceHost::FromWebContents(web_contents());
  ASSERT_TRUE(host);
  const auto live = host->BuildDirectObservationContext();
  ASSERT_TRUE(live);
  CorePageObservationIdentity identity{
      .tab_id = live->tab_id,
      .frame_id = live->frame_id,
      .page_epoch = live->page_epoch,
      .origin = live->origin,
      .graph_revision = live->graph_revision,
  };
  CoreSiteSkillOfferBroker broker(false);
  auto operation =
      service::OperationEnvelope::New("match", 1u, 0u, 60'000u, "match-key");
  std::string offer_id;
  auto callback = broker.TrackMatch(
      1u, "match", web_contents()->GetWeakPtr(), identity, operation.Clone(),
      api::PageInspectorSnapshotView::New(),
      base::BindLambdaForTesting(
          [&](api::PageInspectorSnapshotResultPtr result) {
            ASSERT_TRUE(result && result->snapshot);
            ASSERT_EQ(api::SiteSkillOfferAvailability::kAvailable,
                      result->snapshot->site_skill_offer_availability);
            ASSERT_EQ(1u, result->snapshot->site_skill_offers.size());
            offer_id = result->snapshot->site_skill_offers.front()->offer_id;
          }));
  auto result = service::SiteSkillMatchResult::New();
  result->operation = std::move(operation);
  result->status = service::SiteSkillMatchStatus::kAvailable;
  result->tab_id = identity.tab_id;
  result->frame_id = identity.frame_id;
  result->page_epoch = identity.page_epoch;
  result->graph_revision = identity.graph_revision;
  result->origin = identity.origin;
  result->offers.push_back(
      service::SiteSkillMatchOffer::New("flow.saved@1", "flow.saved", 1u, 4u));
  std::move(callback).Run(std::move(result));
  ASSERT_FALSE(offer_id.empty());

  auto consent = service::TaskConsentPreview::New();
  consent->sources.push_back(service::TaskConsentSource::New(
      "00112233445566778899aabbccddeeff", identity.tab_id, identity.origin,
      std::nullopt));
  // Existing authored research offers retain their source-closed shape.
  EXPECT_EQ("flow.saved@1",
            broker.ResolveForStart(
                offer_id, api::TaskTemplateId::kBuildSourceTable, *consent));
  EXPECT_FALSE(broker.ResolveForStart(offer_id, api::TaskTemplateId::kWebErrand,
                                      *consent));
  consent->provider_route = service::TaskProviderRoute::kNoModelRequired;
  EXPECT_EQ("flow.saved@1", broker.ResolveForStart(
      offer_id, api::TaskTemplateId::kWebErrand, *consent));
  EXPECT_FALSE(broker.ResolveForStart(
      "another-offer", api::TaskTemplateId::kWebErrand, *consent));
  consent->source_discovery_enabled = true;
  consent->new_source_cap = 4u;
  EXPECT_FALSE(broker.ResolveForStart(
      offer_id, api::TaskTemplateId::kWebErrand, *consent));
  consent->provider_route = service::TaskProviderRoute::kDirectUserKey;
  EXPECT_EQ("flow.saved@1",
            broker.ResolveForStart(offer_id, api::TaskTemplateId::kWebErrand,
                                   *consent));
  EXPECT_FALSE(broker.ResolveForStart(
      offer_id, api::TaskTemplateId::kBuildSourceTable, *consent));
  consent->new_source_cap = 9u;
  EXPECT_FALSE(broker.ResolveForStart(offer_id, api::TaskTemplateId::kWebErrand,
                                      *consent));
  consent->new_source_cap = 4u;
  consent->sources.front()->tab_id = "tab-other";
  EXPECT_FALSE(broker.ResolveForStart(offer_id, api::TaskTemplateId::kWebErrand,
                                      *consent));
  consent->sources.front()->tab_id = identity.tab_id;
  SetGraphRevision(web_contents(), 8u);
  EXPECT_FALSE(broker.ResolveForStart(offer_id, api::TaskTemplateId::kWebErrand,
                                      *consent));
  broker.ResolveUnavailable();
  EXPECT_FALSE(broker.ResolveForStart(offer_id, api::TaskTemplateId::kWebErrand,
                                      *consent));
}

}  // namespace
}  // namespace taffy
