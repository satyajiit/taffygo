// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_account_effect_terminal.h"

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

TEST(CoreAccountEffectTerminalTest,
     UnconfirmedCanonicalClearMakesEveryTerminalOutcomeUnknown) {
  for (const mojom::EffectStatus requested : {
           mojom::EffectStatus::kCompleted,
           mojom::EffectStatus::kDenied,
           mojom::EffectStatus::kDeadlineExceeded,
           mojom::EffectStatus::kUnavailable,
           mojom::EffectStatus::kInvalidResult,
       }) {
    CanonicalSessionClearDecision unavailable = ResolveCanonicalSessionClear(
        requested, mojom::EffectStatus::kUnavailable, false);
    EXPECT_EQ(mojom::EffectStatus::kOutcomeUnknown,
              unavailable.terminal_status);
    EXPECT_FALSE(unavailable.mark_completed_revoke_deleted);

    CanonicalSessionClearDecision not_cleared = ResolveCanonicalSessionClear(
        requested, mojom::EffectStatus::kCompleted, false);
    EXPECT_EQ(mojom::EffectStatus::kOutcomeUnknown,
              not_cleared.terminal_status);
    EXPECT_FALSE(not_cleared.mark_completed_revoke_deleted);
  }
}

TEST(CoreAccountEffectTerminalTest,
     ConfirmedCanonicalClearPreservesRequestedTerminal) {
  CanonicalSessionClearDecision completed = ResolveCanonicalSessionClear(
      mojom::EffectStatus::kCompleted, mojom::EffectStatus::kCompleted, true);
  EXPECT_EQ(mojom::EffectStatus::kCompleted, completed.terminal_status);
  EXPECT_TRUE(completed.mark_completed_revoke_deleted);

  CanonicalSessionClearDecision denied = ResolveCanonicalSessionClear(
      mojom::EffectStatus::kDenied, mojom::EffectStatus::kCompleted, true);
  EXPECT_EQ(mojom::EffectStatus::kDenied, denied.terminal_status);
  EXPECT_FALSE(denied.mark_completed_revoke_deleted);
}

TEST(CoreAccountEffectTerminalTest,
     OnlyAmbiguousCanonicalSessionMutationsRequireReconciliation) {
  auto result = mojom::EffectResult::New();
  result->status = mojom::EffectStatus::kOutcomeUnknown;
  result->kind = mojom::EffectKind::kNetworkRequest;
  result->network = mojom::NetworkEffectResult::New();
  for (const mojom::AccountNetworkOperation operation : {
           mojom::AccountNetworkOperation::kExchangeAuthorizationCode,
           mojom::AccountNetworkOperation::kExchangeNativeCredential,
           mojom::AccountNetworkOperation::kRefreshSession,
           mojom::AccountNetworkOperation::kRevokeSession,
       }) {
    result->network->operation_kind = operation;
    EXPECT_TRUE(RequiresAccountReconciliation(*result));
  }

  result->network->operation_kind =
      mojom::AccountNetworkOperation::kRequestEmailLink;
  EXPECT_FALSE(RequiresAccountReconciliation(*result));
  // An entitlement fetch reads; it cannot leave the canonical session in an
  // ambiguous state, so an unknown outcome costs no reconciliation.
  result->network->operation_kind =
      mojom::AccountNetworkOperation::kFetchEntitlement;
  EXPECT_FALSE(RequiresAccountReconciliation(*result));
  result->status = mojom::EffectStatus::kUnavailable;
  result->network->operation_kind =
      mojom::AccountNetworkOperation::kRefreshSession;
  EXPECT_FALSE(RequiresAccountReconciliation(*result));
}

} // namespace
} // namespace taffy
