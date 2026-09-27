// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_task_effect.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr uint64_t kGeneration = 5u;
constexpr uint64_t kRevision = 9u;
constexpr uint64_t kNow = 10'000u;

mojom::TaskEffectBindingPtr ReconcileBinding() {
  auto binding = mojom::TaskEffectBinding::New();
  binding->operation = mojom::OperationEnvelope::New(
      "reconcile-operation", kGeneration, kRevision, 13'000u,
      "reconcile-idempotency");
  binding->effect_id = "reconcile-effect";
  binding->task_id = "task-1";
  binding->kind = mojom::TaskReducerEffectKind::kReconcileAction;
  binding->reconcile = mojom::TaskReconcileEffect::New(
      "action-1", mojom::TaskRecoveryRule::kReconcileFirst, "dispatch-1",
      mojom::TaskActionOperationKind::kDomClick);
  return binding;
}

TEST(CoreTaskEffectReconciliationTest,
     RequiresExactActionAndDispatchIdentities) {
  EXPECT_TRUE(IsStructurallyValidTaskEffectBinding(
      *ReconcileBinding(), kGeneration, kRevision, kNow));

  auto missing_action = ReconcileBinding();
  missing_action->reconcile->action_id.clear();
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(
      *missing_action, kGeneration, kRevision, kNow));

  auto missing_dispatch = ReconcileBinding();
  missing_dispatch->reconcile->dispatch_id.clear();
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(
      *missing_dispatch, kGeneration, kRevision, kNow));
}

}  // namespace
}  // namespace taffy
