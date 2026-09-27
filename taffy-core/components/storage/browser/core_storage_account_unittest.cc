// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/core_storage_broker.h"

#include <utility>

#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "sql/test/test_helpers.h"
#include "taffy/components/storage/browser/account_session_expiry.h"
#include "taffy/components/storage/browser/core_storage_schema_test_util.h"
#include "taffy/components/storage/browser/generated/core_service_journal_schema.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

mojom::CoreBootstrapPtr Load(CoreStorageBroker *broker) {
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

mojom::EffectEnvelopePtr AccountExchangeEffect() {
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = mojom::OperationEnvelope::New();
  effect->operation->operation_id = "account-exchange";
  effect->operation->service_generation = 1;
  effect->operation->deadline_monotonic_ms =
      (base::TimeTicks::Now() + base::Minutes(2))
          .since_origin()
          .InMilliseconds();
  effect->operation->idempotency_key = "account-exchange-key";
  effect->effect_id = "account-exchange-effect";
  effect->kind = mojom::EffectKind::kNetworkRequest;
  effect->retry_class = mojom::RetryClass::kConsequential;
  effect->network_request = mojom::NetworkRequestEffect::New();
  effect->network_request->operation_kind =
      mojom::AccountNetworkOperation::kExchangeNativeCredential;
  effect->network_request->exchange_native_credential =
      mojom::ExchangeNativeCredentialRequest::New();
  effect->network_request->exchange_native_credential->flow_id = "flow-1";
  effect->network_request->exchange_native_credential->auth_method =
      mojom::AccountAuthMethod::kGoogle;
  effect->network_request->exchange_native_credential->credential_handle =
      "credential-handle";
  effect->network_request->max_response_bytes =
      static_cast<uint32_t>(mojom::kMaxAccountResponseBytes);
  return effect;
}

class CoreStorageAccountTest : public testing::Test {
protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
};

TEST(AccountSessionExpiryTest, RestampsOnlyRepresentableFutureUtcValues) {
  const base::Time now_utc = base::Time::UnixEpoch() + base::Days(20'000);
  const base::TimeTicks now_monotonic = base::TimeTicks() + base::Days(1);
  const uint64_t expected =
      (now_monotonic + base::Seconds(90)).since_origin().InMilliseconds();
  EXPECT_EQ(expected, RestampAccountSessionExpiry(now_utc + base::Seconds(90),
                                                  now_utc, now_monotonic));
  EXPECT_FALSE(RestampAccountSessionExpiry(now_utc, now_utc, now_monotonic));
  EXPECT_FALSE(
      RestampAccountSessionExpiry(base::Time::Max(), now_utc, now_monotonic));
  EXPECT_FALSE(RestampAccountSessionExpiry(now_utc + base::Seconds(90),
                                           base::Time(), now_monotonic));
  EXPECT_FALSE(RestampAccountSessionExpiry(now_utc + base::Seconds(90), now_utc,
                                           base::TimeTicks::Max()));
}

TEST(AccountSessionExpiryTest, PersistsOnlyRepresentableFutureMonotonicValues) {
  const base::Time now_utc = base::Time::UnixEpoch() + base::Days(20'000);
  const base::TimeTicks now_monotonic = base::TimeTicks() + base::Days(1);
  const uint64_t future =
      (now_monotonic + base::Seconds(90)).since_origin().InMilliseconds();
  EXPECT_EQ(now_utc + base::Seconds(90),
            PersistAccountSessionExpiry(future, now_utc, now_monotonic));
  EXPECT_FALSE(PersistAccountSessionExpiry(
      now_monotonic.since_origin().InMilliseconds(), now_utc, now_monotonic));
  EXPECT_FALSE(
      PersistAccountSessionExpiry(future, base::Time(), now_monotonic));
}

TEST_F(CoreStorageAccountTest, PersistsTypedAccountMethodWithSessionHandle) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker broker(directory.GetPath().AppendASCII("core.sqlite3"),
                           false);
  mojom::EffectEnvelopePtr effect = AccountExchangeEffect();
  ASSERT_TRUE(CommitIntent(&broker, *effect));

  auto result = mojom::EffectResult::New();
  result->operation = effect->operation.Clone();
  result->effect_id = effect->effect_id;
  result->status = mojom::EffectStatus::kCompleted;
  result->kind = mojom::EffectKind::kNetworkRequest;
  result->network = mojom::NetworkEffectResult::New();
  result->network->operation_kind =
      mojom::AccountNetworkOperation::kExchangeNativeCredential;
  result->network->native_credential_session =
      mojom::AccountSessionReceipt::New();
  result->network->native_credential_session->session_handle = "session-handle";
  result->network->native_credential_session->account_subject = "subject-1";
  result->network->native_credential_session->expires_at_monotonic_ms =
      (base::TimeTicks::Now() + base::Minutes(2))
          .since_origin()
          .InMilliseconds();
  result->network->native_credential_session->auth_method =
      mojom::AccountAuthMethod::kGoogle;
  ASSERT_TRUE(CommitResult(&broker, *result));

  mojom::CoreBootstrapPtr bootstrap = Load(&broker);
  ASSERT_TRUE(bootstrap);
  ASSERT_TRUE(bootstrap->account_session);
  EXPECT_EQ("session-handle", bootstrap->account_session->session_handle);
  EXPECT_EQ(mojom::AccountAuthMethod::kGoogle,
            bootstrap->account_session->auth_method);
}

TEST_F(CoreStorageAccountTest, PersistsAndRestoresTheAccountIdentity) {
  // The journal row is the identity's one durable home, so a restart reads it
  // from here or the screen has nothing to say. The vault holds tokens only.
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker broker(directory.GetPath().AppendASCII("core.sqlite3"),
                           false);
  mojom::EffectEnvelopePtr effect = AccountExchangeEffect();
  ASSERT_TRUE(CommitIntent(&broker, *effect));

  auto result = mojom::EffectResult::New();
  result->operation = effect->operation.Clone();
  result->effect_id = effect->effect_id;
  result->status = mojom::EffectStatus::kCompleted;
  result->kind = mojom::EffectKind::kNetworkRequest;
  result->network = mojom::NetworkEffectResult::New();
  result->network->operation_kind =
      mojom::AccountNetworkOperation::kExchangeNativeCredential;
  result->network->native_credential_session =
      mojom::AccountSessionReceipt::New();
  result->network->native_credential_session->session_handle = "session-handle";
  result->network->native_credential_session->account_subject = "subject-1";
  result->network->native_credential_session->expires_at_monotonic_ms =
      (base::TimeTicks::Now() + base::Minutes(2))
          .since_origin()
          .InMilliseconds();
  result->network->native_credential_session->auth_method =
      mojom::AccountAuthMethod::kGoogle;
  result->network->native_credential_session->email = "reader@example.test";
  result->network->native_credential_session->display_name = "A Reader";
  ASSERT_TRUE(CommitResult(&broker, *result));

  mojom::CoreBootstrapPtr bootstrap = Load(&broker);
  ASSERT_TRUE(bootstrap);
  ASSERT_TRUE(bootstrap->account_session);
  ASSERT_TRUE(bootstrap->account_session->email);
  EXPECT_EQ("reader@example.test", *bootstrap->account_session->email);
  ASSERT_TRUE(bootstrap->account_session->display_name);
  EXPECT_EQ("A Reader", *bootstrap->account_session->display_name);
}

TEST_F(CoreStorageAccountTest, AnAccountTheProviderNamedNothingForRestoresBare) {
  // Absent must survive the round trip as absent. A row that read back an
  // empty string would be a person called nothing, which the projection cannot
  // tell from a person the provider did name.
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker broker(directory.GetPath().AppendASCII("core.sqlite3"),
                           false);
  mojom::EffectEnvelopePtr effect = AccountExchangeEffect();
  ASSERT_TRUE(CommitIntent(&broker, *effect));

  auto result = mojom::EffectResult::New();
  result->operation = effect->operation.Clone();
  result->effect_id = effect->effect_id;
  result->status = mojom::EffectStatus::kCompleted;
  result->kind = mojom::EffectKind::kNetworkRequest;
  result->network = mojom::NetworkEffectResult::New();
  result->network->operation_kind =
      mojom::AccountNetworkOperation::kExchangeNativeCredential;
  result->network->native_credential_session =
      mojom::AccountSessionReceipt::New();
  result->network->native_credential_session->session_handle = "session-handle";
  result->network->native_credential_session->account_subject = "subject-1";
  result->network->native_credential_session->expires_at_monotonic_ms =
      (base::TimeTicks::Now() + base::Minutes(2))
          .since_origin()
          .InMilliseconds();
  result->network->native_credential_session->auth_method =
      mojom::AccountAuthMethod::kGoogle;
  ASSERT_TRUE(CommitResult(&broker, *result));

  mojom::CoreBootstrapPtr bootstrap = Load(&broker);
  ASSERT_TRUE(bootstrap);
  ASSERT_TRUE(bootstrap->account_session);
  EXPECT_FALSE(bootstrap->account_session->email);
  EXPECT_FALSE(bootstrap->account_session->display_name);
}

TEST_F(CoreStorageAccountTest, DuplicatePendingIntentIdFailsClosed) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker broker(directory.GetPath().AppendASCII("core.sqlite3"),
                           false);
  mojom::EffectEnvelopePtr effect = AccountExchangeEffect();

  ASSERT_TRUE(CommitIntent(&broker, *effect));
  EXPECT_FALSE(CommitIntent(&broker, *effect));
}

TEST_F(CoreStorageAccountTest, DuplicateTerminalIntentIdFailsClosed) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker broker(directory.GetPath().AppendASCII("core.sqlite3"),
                           false);
  mojom::EffectEnvelopePtr effect = AccountExchangeEffect();
  ASSERT_TRUE(CommitIntent(&broker, *effect));

  auto result = mojom::EffectResult::New();
  result->operation = effect->operation.Clone();
  result->effect_id = effect->effect_id;
  result->status = mojom::EffectStatus::kUnavailable;
  result->kind = effect->kind;
  result->network = mojom::NetworkEffectResult::New();
  result->network->operation_kind =
      mojom::AccountNetworkOperation::kExchangeNativeCredential;
  ASSERT_TRUE(CommitResult(&broker, *result));

  EXPECT_FALSE(CommitIntent(&broker, *effect));
}

TEST_F(CoreStorageAccountTest, TerminalWithoutItsNetworkResultIsRefused) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  CoreStorageBroker broker(directory.GetPath().AppendASCII("core.sqlite3"),
                           false);
  mojom::EffectEnvelopePtr effect = AccountExchangeEffect();
  ASSERT_TRUE(CommitIntent(&broker, *effect));

  // A terminal that names no network operation cannot say whether the session
  // it was dispatched for was created, so it may not retire the pending
  // barrier. The barrier survives, and the intent stays claimed.
  auto result = mojom::EffectResult::New();
  result->operation = effect->operation.Clone();
  result->effect_id = effect->effect_id;
  result->status = mojom::EffectStatus::kUnavailable;
  result->kind = effect->kind;
  EXPECT_FALSE(CommitResult(&broker, *result));
  EXPECT_FALSE(CommitIntent(&broker, *effect));
}

TEST_F(CoreStorageAccountTest, RestampsDurableUtcAccountExpiryAfterRestart) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath path = directory.GetPath().AppendASCII("core.sqlite3");
  {
    CoreStorageBroker broker(path, false);
    ASSERT_TRUE(Load(&broker));
  }
  task_environment_.RunUntilIdle();

  sql::Database database(sql::test::kTestTag);
  ASSERT_TRUE(database.Open(path));
  {
    sql::Statement insert(database.GetUniqueStatement(
        "INSERT INTO core_account_session(singleton,session_handle,"
        "account_subject,expires_at_utc_ms,rotation,auth_method) "
        "VALUES(1,?,?,?,4,2)"));
    insert.BindString(0, "session-handle");
    insert.BindString(1, "subject-handle");
    insert.BindInt64(
        2,
        (base::Time::Now() + base::Seconds(90)).InMillisecondsSinceUnixEpoch());
    ASSERT_TRUE(insert.Run());
  }
  database.Close();

  const uint64_t expected_expiry = (base::TimeTicks::Now() + base::Seconds(90))
                                       .since_origin()
                                       .InMilliseconds();
  CoreStorageBroker broker(path, false);
  mojom::CoreBootstrapPtr bootstrap = Load(&broker);
  ASSERT_TRUE(bootstrap);
  ASSERT_TRUE(bootstrap->account_session);
  EXPECT_EQ("session-handle", bootstrap->account_session->session_handle);
  EXPECT_EQ("subject-handle", bootstrap->account_session->account_subject);
  EXPECT_EQ(expected_expiry,
            bootstrap->account_session->expires_at_monotonic_ms);
  EXPECT_EQ(4u, bootstrap->account_session->rotation);
  EXPECT_EQ(mojom::AccountAuthMethod::kGithub,
            bootstrap->account_session->auth_method);
}

TEST_F(CoreStorageAccountTest, ExpiredAccountSessionIsRevokedNotRestored) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath path = directory.GetPath().AppendASCII("core.sqlite3");
  {
    CoreStorageBroker broker(path, false);
    ASSERT_TRUE(Load(&broker));
  }
  task_environment_.RunUntilIdle();

  sql::Database database(sql::test::kTestTag);
  ASSERT_TRUE(database.Open(path));
  {
    sql::Statement insert(database.GetUniqueStatement(
        "INSERT INTO core_account_session(singleton,session_handle,"
        "account_subject,expires_at_utc_ms,rotation,auth_method) "
        "VALUES(1,?,?,?,0,1)"));
    insert.BindString(0, "expired-session");
    insert.BindString(1, "expired-subject");
    insert.BindInt64(2, (base::Time::Now() - base::Seconds(1))
                            .InMillisecondsSinceUnixEpoch());
    ASSERT_TRUE(insert.Run());
  }
  database.Close();

  CoreStorageBroker broker(path, false);
  mojom::CoreBootstrapPtr bootstrap = Load(&broker);
  ASSERT_TRUE(bootstrap);
  EXPECT_FALSE(bootstrap->account_session);
  task_environment_.RunUntilIdle();
}

TEST_F(CoreStorageAccountTest, MigratesVersionFourByDroppingMonotonicSession) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath path = directory.GetPath().AppendASCII("core.sqlite3");
  sql::Database database(sql::test::kTestTag);
  ASSERT_TRUE(database.Open(path));
  ASSERT_TRUE(storage_test::CreateHistoricalSchema(&database, 4u));
  ASSERT_TRUE(database.Execute(
      "INSERT INTO core_account_session VALUES(1,'old-session','old-subject',"
      "90000,2)"));
  database.Close();

  CoreStorageBroker broker(path, false);
  mojom::CoreBootstrapPtr bootstrap = Load(&broker);
  ASSERT_TRUE(bootstrap);
  EXPECT_FALSE(bootstrap->account_session);
  EXPECT_EQ(storage_schema::kVersion, bootstrap->core_journal_schema_version);
  EXPECT_EQ(storage_schema::kChecksum, bootstrap->core_journal_schema_checksum);
}

TEST_F(CoreStorageAccountTest, MigratesVersionFiveByDroppingMethodlessSession) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath path = directory.GetPath().AppendASCII("core.sqlite3");
  sql::Database database(sql::test::kTestTag);
  ASSERT_TRUE(database.Open(path));
  ASSERT_TRUE(storage_test::CreateHistoricalSchema(&database, 5u));
  ASSERT_TRUE(database.Execute(
      "INSERT INTO core_account_session VALUES(1,'methodless-session',"
      "'methodless-subject',90000,2)"));
  database.Close();

  CoreStorageBroker broker(path, false);
  mojom::CoreBootstrapPtr bootstrap = Load(&broker);
  ASSERT_TRUE(bootstrap);
  EXPECT_FALSE(bootstrap->account_session);
  EXPECT_EQ(storage_schema::kVersion, bootstrap->core_journal_schema_version);
  EXPECT_EQ(storage_schema::kChecksum, bootstrap->core_journal_schema_checksum);
}

} // namespace
} // namespace taffy
