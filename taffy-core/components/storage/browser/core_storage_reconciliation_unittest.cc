// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/core_storage_broker.h"

#include <optional>
#include <string>
#include <utility>

#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "sql/test/test_helpers.h"
#include "taffy/components/storage/browser/core_storage_schema_test_util.h"
#include "taffy/components/storage/browser/generated/core_service_journal_schema.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

using ReconciliationState = CoreStorageBroker::AccountReconciliationState;

bool CommitIntent(CoreStorageBroker *broker,
                  const mojom::EffectEnvelope &effect) {
  base::RunLoop loop;
  bool committed = false;
  broker->CommitIntent(effect, base::BindLambdaForTesting([&](bool value) {
                         committed = value;
                         loop.Quit();
                       }));
  loop.Run();
  return committed;
}

bool CommitResult(CoreStorageBroker *broker,
                  const mojom::EffectResult &result) {
  base::RunLoop loop;
  bool committed = false;
  broker->CommitResult(result, base::BindLambdaForTesting([&](bool value) {
                         committed = value;
                         loop.Quit();
                       }));
  loop.Run();
  return committed;
}

std::optional<ReconciliationState>
LoadReconciliation(CoreStorageBroker *broker) {
  base::RunLoop loop;
  std::optional<ReconciliationState> state;
  broker->LoadAccountReconciliationState(base::BindLambdaForTesting(
      [&](std::optional<ReconciliationState> loaded) {
        state = std::move(loaded);
        loop.Quit();
      }));
  loop.Run();
  return state;
}

mojom::CoreBootstrapPtr LoadBootstrap(CoreStorageBroker *broker) {
  base::RunLoop loop;
  mojom::CoreBootstrapPtr bootstrap;
  broker->LoadBootstrap(
      1, false, base::BindLambdaForTesting([&](mojom::CoreBootstrapPtr loaded) {
        bootstrap = std::move(loaded);
        loop.Quit();
      }));
  loop.Run();
  return bootstrap;
}

bool FinishReconciliation(CoreStorageBroker *broker) {
  base::RunLoop loop;
  bool finished = false;
  broker->FinishAccountReconciliation(
      base::BindLambdaForTesting([&](bool value) {
        finished = value;
        loop.Quit();
      }));
  loop.Run();
  return finished;
}

mojom::EffectEnvelopePtr ExchangeEffect(std::string effect_id) {
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = mojom::OperationEnvelope::New();
  effect->operation->operation_id = "operation-" + effect_id;
  effect->operation->service_generation = 1;
  effect->operation->deadline_monotonic_ms =
      (base::TimeTicks::Now() + base::Minutes(2))
          .since_origin()
          .InMilliseconds();
  effect->operation->idempotency_key = "key-" + effect_id;
  effect->effect_id = std::move(effect_id);
  effect->kind = mojom::EffectKind::kNetworkRequest;
  effect->retry_class = mojom::RetryClass::kConsequential;
  effect->network_request = mojom::NetworkRequestEffect::New();
  effect->network_request->operation_kind =
      mojom::AccountNetworkOperation::kExchangeNativeCredential;
  effect->network_request->exchange_native_credential =
      mojom::ExchangeNativeCredentialRequest::New();
  effect->network_request->exchange_native_credential->flow_id = "flow";
  effect->network_request->exchange_native_credential->auth_method =
      mojom::AccountAuthMethod::kGoogle;
  effect->network_request->exchange_native_credential->credential_handle =
      "credential";
  effect->network_request->max_response_bytes =
      static_cast<uint32_t>(mojom::kMaxAccountResponseBytes);
  return effect;
}

mojom::EffectEnvelopePtr RefreshEffect(std::string effect_id) {
  mojom::EffectEnvelopePtr effect = ExchangeEffect(std::move(effect_id));
  effect->network_request->operation_kind =
      mojom::AccountNetworkOperation::kRefreshSession;
  effect->network_request->exchange_native_credential.reset();
  effect->network_request->refresh_session =
      mojom::RefreshSessionRequest::New();
  effect->network_request->refresh_session->session_handle = "session";
  effect->network_request->refresh_session->expected_rotation = 1;
  effect->network_request->refresh_session->expected_account_subject =
      "subject";
  effect->network_request->refresh_session->expected_auth_method =
      mojom::AccountAuthMethod::kGoogle;
  return effect;
}

mojom::EffectEnvelopePtr RevokeEffect(std::string effect_id) {
  mojom::EffectEnvelopePtr effect = ExchangeEffect(std::move(effect_id));
  effect->network_request->operation_kind =
      mojom::AccountNetworkOperation::kRevokeSession;
  effect->network_request->exchange_native_credential.reset();
  effect->network_request->revoke_session = mojom::RevokeSessionRequest::New();
  effect->network_request->revoke_session->session_handle = "session";
  return effect;
}

mojom::EffectResultPtr TerminalResult(const mojom::EffectEnvelope &effect,
                                      mojom::EffectStatus status) {
  auto result = mojom::EffectResult::New();
  result->operation = effect.operation.Clone();
  result->effect_id = effect.effect_id;
  result->status = status;
  result->kind = effect.kind;
  result->network = mojom::NetworkEffectResult::New();
  result->network->operation_kind = effect.network_request->operation_kind;
  return result;
}

mojom::EffectResultPtr RevokeResult(const mojom::EffectEnvelope &effect) {
  mojom::EffectResultPtr result =
      TerminalResult(effect, mojom::EffectStatus::kCompleted);
  result->network->revoked_session = mojom::RevokedSessionResult::New();
  result->network->revoked_session->session_handle = "session";
  result->network->revoked_session->deleted = true;
  return result;
}

mojom::EffectResultPtr SessionResult(const mojom::EffectEnvelope &effect,
                                     uint64_t rotation) {
  mojom::EffectResultPtr result =
      TerminalResult(effect, mojom::EffectStatus::kCompleted);
  auto receipt = mojom::AccountSessionReceipt::New();
  receipt->session_handle = "session";
  receipt->account_subject = "subject";
  receipt->expires_at_monotonic_ms = (base::TimeTicks::Now() + base::Minutes(2))
                                         .since_origin()
                                         .InMilliseconds();
  receipt->rotation = rotation;
  receipt->auth_method = mojom::AccountAuthMethod::kGoogle;
  if (effect.network_request->operation_kind ==
      mojom::AccountNetworkOperation::kRefreshSession) {
    result->network->refreshed_session = std::move(receipt);
  } else {
    result->network->native_credential_session = std::move(receipt);
  }
  return result;
}

void SeedSession(CoreStorageBroker *broker) {
  mojom::EffectEnvelopePtr exchange = ExchangeEffect("seed");
  ASSERT_TRUE(CommitIntent(broker, *exchange));
  ASSERT_TRUE(CommitResult(broker, *SessionResult(*exchange, 0)));
}

class CoreStorageReconciliationTest : public testing::Test {
protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
};

TEST_F(CoreStorageReconciliationTest,
       OutcomeUnknownBlocksRestoreUntilIdempotentFinish) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker broker(directory.GetPath().AppendASCII("core.sqlite3"),
                           false);
  SeedSession(&broker);
  mojom::EffectEnvelopePtr effect = RefreshEffect("unknown");
  ASSERT_TRUE(CommitIntent(&broker, *effect));

  std::optional<ReconciliationState> pending = LoadReconciliation(&broker);
  ASSERT_TRUE(pending);
  EXPECT_TRUE(pending->has_pending_session_mutation);
  EXPECT_TRUE(pending->committed_session);
  mojom::CoreBootstrapPtr bootstrap = LoadBootstrap(&broker);
  ASSERT_TRUE(bootstrap);
  EXPECT_FALSE(bootstrap->account_session);

  mojom::EffectResultPtr unknown =
      TerminalResult(*effect, mojom::EffectStatus::kOutcomeUnknown);
  ASSERT_TRUE(CommitResult(&broker, *unknown));
  pending = LoadReconciliation(&broker);
  ASSERT_TRUE(pending);
  EXPECT_TRUE(pending->has_pending_session_mutation);
  mojom::EffectEnvelopePtr fenced = ExchangeEffect("fenced-until-reconciled");
  EXPECT_FALSE(CommitIntent(&broker, *fenced));
  EXPECT_TRUE(FinishReconciliation(&broker));
  EXPECT_TRUE(FinishReconciliation(&broker));
  pending = LoadReconciliation(&broker);
  ASSERT_TRUE(pending);
  EXPECT_FALSE(pending->has_pending_session_mutation);
  EXPECT_FALSE(pending->committed_session);
  EXPECT_TRUE(CommitIntent(&broker, *fenced));
}

TEST_F(CoreStorageReconciliationTest,
       GenerationLossKeepsBarrierAndRejectsLateCompletion) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker broker(directory.GetPath().AppendASCII("core.sqlite3"),
                           false);
  SeedSession(&broker);
  mojom::EffectEnvelopePtr effect = RefreshEffect("lost-generation");
  ASSERT_TRUE(CommitIntent(&broker, *effect));
  broker.MarkEffectsLost(1, {effect->effect_id});
  task_environment_.RunUntilIdle();

  std::optional<ReconciliationState> state = LoadReconciliation(&broker);
  ASSERT_TRUE(state);
  EXPECT_TRUE(state->has_pending_session_mutation);
  EXPECT_FALSE(CommitResult(&broker, *SessionResult(*effect, 1)));
  state = LoadReconciliation(&broker);
  ASSERT_TRUE(state);
  EXPECT_TRUE(state->has_pending_session_mutation);
  EXPECT_TRUE(FinishReconciliation(&broker));
}

TEST_F(CoreStorageReconciliationTest,
       PendingSessionMutationImmediatelyFencesAnotherIntent) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker broker(directory.GetPath().AppendASCII("core.sqlite3"),
                           false);
  mojom::EffectEnvelopePtr first = ExchangeEffect("first");
  mojom::EffectEnvelopePtr second = ExchangeEffect("second");
  ASSERT_TRUE(CommitIntent(&broker, *first));
  EXPECT_FALSE(CommitIntent(&broker, *second));

  mojom::EffectResultPtr failed =
      TerminalResult(*first, mojom::EffectStatus::kUnavailable);
  ASSERT_TRUE(CommitResult(&broker, *failed));
  std::optional<ReconciliationState> state = LoadReconciliation(&broker);
  ASSERT_TRUE(state);
  EXPECT_FALSE(state->has_pending_session_mutation);
  ASSERT_TRUE(CommitIntent(&broker, *second));
  failed = TerminalResult(*second, mojom::EffectStatus::kUnavailable);
  ASSERT_TRUE(CommitResult(&broker, *failed));
  state = LoadReconciliation(&broker);
  ASSERT_TRUE(state);
  EXPECT_FALSE(state->has_pending_session_mutation);
}

TEST_F(CoreStorageReconciliationTest,
       CompletedExchangeWritesSessionAndClearsMarkerAtomically) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker broker(directory.GetPath().AppendASCII("core.sqlite3"),
                           false);
  SeedSession(&broker);

  std::optional<ReconciliationState> state = LoadReconciliation(&broker);
  ASSERT_TRUE(state);
  EXPECT_FALSE(state->has_pending_session_mutation);
  ASSERT_TRUE(state->committed_session);
  EXPECT_EQ("session", state->committed_session->session_handle);
  EXPECT_EQ("subject", state->committed_session->account_subject);
  EXPECT_EQ(0u, state->committed_session->rotation);
  EXPECT_EQ(mojom::AccountAuthMethod::kGoogle,
            state->committed_session->auth_method);

  mojom::EffectEnvelopePtr replacement = ExchangeEffect("replacement");
  ASSERT_TRUE(CommitIntent(&broker, *replacement));
  mojom::EffectResultPtr replacement_result = SessionResult(*replacement, 0);
  replacement_result->network->native_credential_session->session_handle =
      "other-session";
  replacement_result->network->native_credential_session->account_subject =
      "other-subject";
  EXPECT_FALSE(CommitResult(&broker, *replacement_result));
  state = LoadReconciliation(&broker);
  ASSERT_TRUE(state);
  EXPECT_TRUE(state->has_pending_session_mutation);
  ASSERT_TRUE(state->committed_session);
  EXPECT_EQ("session", state->committed_session->session_handle);
}

TEST_F(CoreStorageReconciliationTest,
       RefreshPreservesHandleSubjectMethodAndSequentialRotation) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker broker(directory.GetPath().AppendASCII("core.sqlite3"),
                           false);
  SeedSession(&broker);
  mojom::EffectEnvelopePtr refresh = RefreshEffect("refresh");
  ASSERT_TRUE(CommitIntent(&broker, *refresh));
  mojom::EffectResultPtr result = SessionResult(*refresh, 1);

  result->network->refreshed_session->session_handle = "replacement";
  EXPECT_FALSE(CommitResult(&broker, *result));
  result->network->refreshed_session->session_handle = "session";
  result->network->refreshed_session->account_subject = "replacement";
  EXPECT_FALSE(CommitResult(&broker, *result));
  result->network->refreshed_session->account_subject = "subject";
  result->network->refreshed_session->auth_method =
      mojom::AccountAuthMethod::kGithub;
  EXPECT_FALSE(CommitResult(&broker, *result));
  result->network->refreshed_session->auth_method =
      mojom::AccountAuthMethod::kGoogle;
  result->network->refreshed_session->rotation = 2;
  EXPECT_FALSE(CommitResult(&broker, *result));

  std::optional<ReconciliationState> state = LoadReconciliation(&broker);
  ASSERT_TRUE(state);
  EXPECT_TRUE(state->has_pending_session_mutation);
  ASSERT_TRUE(state->committed_session);
  EXPECT_EQ(0u, state->committed_session->rotation);
  result->network->refreshed_session->rotation = 1;
  ASSERT_TRUE(CommitResult(&broker, *result));
  state = LoadReconciliation(&broker);
  ASSERT_TRUE(state);
  EXPECT_FALSE(state->has_pending_session_mutation);
  ASSERT_TRUE(state->committed_session);
  EXPECT_EQ(1u, state->committed_session->rotation);
}

TEST_F(CoreStorageReconciliationTest,
       DeterministicRevokeDeletesOnlyTheMatchingCommittedHandle) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker broker(directory.GetPath().AppendASCII("core.sqlite3"),
                           false);
  SeedSession(&broker);
  mojom::EffectEnvelopePtr revoke = RevokeEffect("revoke");
  ASSERT_TRUE(CommitIntent(&broker, *revoke));
  mojom::EffectResultPtr result = RevokeResult(*revoke);

  result->network->revoked_session->session_handle = "replacement";
  EXPECT_FALSE(CommitResult(&broker, *result));
  std::optional<ReconciliationState> state = LoadReconciliation(&broker);
  ASSERT_TRUE(state);
  EXPECT_TRUE(state->has_pending_session_mutation);
  EXPECT_TRUE(state->committed_session);
  result->network->revoked_session->session_handle = "session";
  result->network->revoked_session->deleted = false;
  EXPECT_FALSE(CommitResult(&broker, *result));
  state = LoadReconciliation(&broker);
  ASSERT_TRUE(state);
  EXPECT_TRUE(state->has_pending_session_mutation);
  EXPECT_TRUE(state->committed_session);
  result->status = mojom::EffectStatus::kDenied;
  ASSERT_TRUE(CommitResult(&broker, *result));
  state = LoadReconciliation(&broker);
  ASSERT_TRUE(state);
  EXPECT_FALSE(state->has_pending_session_mutation);
  EXPECT_FALSE(state->committed_session);
}

TEST_F(CoreStorageReconciliationTest,
       UnknownRevokePreservesSessionAndRecoveryMarker) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker broker(directory.GetPath().AppendASCII("core.sqlite3"),
                           false);
  SeedSession(&broker);
  mojom::EffectEnvelopePtr revoke = RevokeEffect("unknown-revoke");
  ASSERT_TRUE(CommitIntent(&broker, *revoke));
  mojom::EffectResultPtr result = RevokeResult(*revoke);
  result->status = mojom::EffectStatus::kOutcomeUnknown;

  EXPECT_FALSE(CommitResult(&broker, *result));
  result->network->revoked_session->deleted = false;
  ASSERT_TRUE(CommitResult(&broker, *result));
  std::optional<ReconciliationState> state = LoadReconciliation(&broker);
  ASSERT_TRUE(state);
  EXPECT_TRUE(state->has_pending_session_mutation);
  EXPECT_TRUE(state->committed_session);
}

TEST_F(CoreStorageReconciliationTest, MigratesVersionSevenPendingNetworkWork) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath path = directory.GetPath().AppendASCII("core.sqlite3");

  // A real version-seven database, built from the schema the generator records
  // that version as physically having.
  //
  // It used to be built the other way round -- create a head database, drop the
  // one table version seven lacked, and rewrite the ledger row to say seven --
  // and that is a head database wearing a version-seven label. Every column
  // seven never had was still there, so the fixture agreed with any migration
  // that only added tables and disagreed with one that added a column. It cost
  // nothing until the version-seven path was corrected to carry the two columns
  // version nine introduced, which a genuine version-seven database does not
  // have and this one did: the migration failed on `duplicate column name`,
  // reporting a defect in the fix rather than in the fixture.
  {
    sql::Database database(sql::test::kTestTag);
    ASSERT_TRUE(database.Open(path));
    ASSERT_TRUE(storage_test::CreateHistoricalSchema(&database, 7u));
    ASSERT_TRUE(database.Execute(
        "INSERT INTO core_effect_journal VALUES('legacy',1,'operation',0,"
        "'key',3,1,NULL)"));
    database.Close();
  }

  CoreStorageBroker broker(path, false);
  std::optional<ReconciliationState> state = LoadReconciliation(&broker);
  ASSERT_TRUE(state);
  // The account-session effect this version left unterminated is carried into
  // the table version eight introduced for it, which is the whole point of the
  // migration rather than a side effect of it.
  EXPECT_TRUE(state->has_pending_session_mutation);
  mojom::CoreBootstrapPtr bootstrap = LoadBootstrap(&broker);
  ASSERT_TRUE(bootstrap);
  EXPECT_EQ(storage_schema::kVersion, bootstrap->core_journal_schema_version);
  EXPECT_EQ(storage_schema::kChecksum, bootstrap->core_journal_schema_checksum);
}

} // namespace
} // namespace taffy
