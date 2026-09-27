// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdint.h>

#include <optional>
#include <string>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_task_effect.h"
#include "testing/gtest/include/gtest/gtest.h"

// What these cover: which granted answers to an ASK_POLICY effect the utility
// hands the bridge, and which origin shape each crosses with. A grant the
// projection refuses is dropped, and a dropped terminal ends the core, so a
// shape the browser's narrowing gate admits must cross here too (decision
// 0176): a page's grant carries its tuple origin, and a grant to leave a
// document with no site carries that document's opaque identity.

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr uint8_t kGranted = 0u;  // PolicyEvaluationStatus

mojom::TaskEffectBindingPtr SearchBinding() {
  auto effect = mojom::TaskEffectBinding::New();
  effect->operation =
      mojom::OperationEnvelope::New("operation-1", 1u, 1u, 1000u, "key-1");
  effect->effect_id = "effect-1";
  effect->task_id = "task-1";
  effect->ordinal = 0u;
  effect->kind = mojom::TaskReducerEffectKind::kAskPolicy;
  effect->policy = mojom::TaskPolicyEffect::New();
  effect->policy->action_id = "action-1";
  effect->policy->operation_kind = mojom::TaskActionOperationKind::kSearch;
  return effect;
}

// A search granted from the error document a dead host leaves behind: the
// shape the phone produced on 2026-09-18.
mojom::PolicyEvaluationResultPtr GrantFromADocumentWithNoSite() {
  auto result = mojom::PolicyEvaluationResult::New();
  result->operation_id = "operation-1";
  result->status = mojom::PolicyEvaluationStatus::kGranted;
  result->minted_grant = mojom::MintedCapabilityGrant::New();
  result->minted_grant->capability_id = "capability-1";
  auto& scope = result->minted_grant->scope;
  scope = mojom::PolicyCapabilityScope::New();
  scope->tab_id = "tab-1";
  scope->frame_id = "frame-1";
  scope->page_epoch = "epoch-1";
  scope->origin = mojom::PolicyOrigin::New();
  scope->origin->kind = mojom::PolicyOriginKind::kOpaque;
  scope->origin->opaque_id = "opaque-1";
  scope->required_graph_revision = 0u;
  scope->destination_scope = mojom::PolicyOrigin::New();
  scope->destination_scope->kind = mojom::PolicyOriginKind::kTuple;
  scope->destination_scope->serialization = "https://www.google.com";
  scope->destination_address = "https://www.google.com/search?q=eaadhaar";
  return result;
}

TEST(RustCoreTaskPolicyGrantTest, AGrantToLeaveADocumentWithNoSiteCrosses) {
  const std::optional<core_bridge::BridgeTaskTerminal> terminal =
      core_service_internal::ToBridgeTaskPolicyTerminal(
          *SearchBinding(), *GrantFromADocumentWithNoSite());
  ASSERT_TRUE(terminal);
  EXPECT_EQ(kGranted, terminal->status);
  EXPECT_EQ("capability-1", std::string(terminal->capability_id));
  EXPECT_TRUE(terminal->has_opaque_origin_id);
  EXPECT_EQ("opaque-1", std::string(terminal->opaque_origin_id));
  EXPECT_TRUE(std::string(terminal->normalized_origin).empty());
  EXPECT_TRUE(terminal->has_destination_address);
  EXPECT_EQ("https://www.google.com/search?q=eaadhaar",
            std::string(terminal->destination_address));
}

TEST(RustCoreTaskPolicyGrantTest, AGrantOnAPageCrossesWithItsTupleOrigin) {
  mojom::PolicyEvaluationResultPtr answer = GrantFromADocumentWithNoSite();
  answer->minted_grant->scope->origin->kind = mojom::PolicyOriginKind::kTuple;
  answer->minted_grant->scope->origin->opaque_id.reset();
  answer->minted_grant->scope->origin->serialization = "https://uidai.gov.in";
  const std::optional<core_bridge::BridgeTaskTerminal> terminal =
      core_service_internal::ToBridgeTaskPolicyTerminal(*SearchBinding(),
                                                        *answer);
  ASSERT_TRUE(terminal);
  EXPECT_FALSE(terminal->has_opaque_origin_id);
  EXPECT_EQ("https://uidai.gov.in", std::string(terminal->normalized_origin));
}

// Only leaving is admitted from a document with no site, and only in the
// shape the browser's gate admits. Each of these is refused there, so it is
// refused here rather than recorded as a grant nothing could spend.
TEST(RustCoreTaskPolicyGrantTest, AnOpaqueGrantForAnythingButLeavingIsRefused) {
  mojom::TaskEffectBindingPtr read = SearchBinding();
  read->policy->operation_kind = mojom::TaskActionOperationKind::kDomRead;
  EXPECT_FALSE(core_service_internal::ToBridgeTaskPolicyTerminal(
      *read, *GrantFromADocumentWithNoSite()));

  mojom::PolicyEvaluationResultPtr on_a_node = GrantFromADocumentWithNoSite();
  on_a_node->minted_grant->scope->node_id = "node-1";
  EXPECT_FALSE(core_service_internal::ToBridgeTaskPolicyTerminal(
      *SearchBinding(), *on_a_node));

  mojom::PolicyEvaluationResultPtr read_graph = GrantFromADocumentWithNoSite();
  read_graph->minted_grant->scope->required_graph_revision = 3u;
  EXPECT_FALSE(core_service_internal::ToBridgeTaskPolicyTerminal(
      *SearchBinding(), *read_graph));

  mojom::PolicyEvaluationResultPtr nowhere = GrantFromADocumentWithNoSite();
  nowhere->minted_grant->scope->destination_scope.reset();
  nowhere->minted_grant->scope->destination_address.reset();
  EXPECT_FALSE(core_service_internal::ToBridgeTaskPolicyTerminal(
      *SearchBinding(), *nowhere));

  mojom::PolicyEvaluationResultPtr both = GrantFromADocumentWithNoSite();
  both->minted_grant->scope->origin->serialization = "https://uidai.gov.in";
  EXPECT_FALSE(core_service_internal::ToBridgeTaskPolicyTerminal(
      *SearchBinding(), *both));
}

}  // namespace
}  // namespace taffy
