// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/profile_model_register.h"

#include <stdint.h>

#include <array>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/scoped_refptr.h"
#include "base/test/task_environment.h"
#include "crypto/hash.h"
#include "taffy/browser/assets/asset_store.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace service_mojom = core_service::mojom;
namespace runtime_mojom = tool_runtime::mojom;

class ProfileModelRegisterTest : public testing::Test {
 public:
  void SetUp() override { ASSERT_TRUE(store_.CreateUniqueTempDir()); }

 protected:
  void Install(const std::string& asset_id,
               const std::string& revision,
               const std::string& bytes) {
    AssetStore store(store_.GetPath());
    base::File file = store.OpenStaging(asset_id, revision, 0u);
    ASSERT_TRUE(file.IsValid());
    ASSERT_TRUE(file.WriteAtCurrentPosAndCheck(base::as_byte_span(bytes)));
    file.Close();
    ASSERT_TRUE(store.Commit(asset_id, revision));
  }

  service_mojom::ModelArtifactRegistrationPtr Registration(
      const std::string& bytes,
      bool adapter = false,
      service_mojom::AssetKind asset_kind =
          service_mojom::AssetKind::kModelWeights,
      service_mojom::ToolModelArtifactKind format =
          service_mojom::ToolModelArtifactKind::kGguf) {
    const std::array<uint8_t, 32> digest =
        crypto::hash::Sha256(base::as_byte_span(bytes));
    return service_mojom::ModelArtifactRegistration::New(
        adapter ? "model.adapter" : "model.small", "2026-08-01", asset_kind,
        format, adapter, bytes.size(),
        std::vector<uint8_t>(digest.begin(), digest.end()));
  }

  std::vector<service_mojom::ModelArtifactRegistrationPtr> Snapshot(
      service_mojom::ModelArtifactRegistrationPtr registration) {
    std::vector<service_mojom::ModelArtifactRegistrationPtr> snapshot;
    snapshot.push_back(std::move(registration));
    return snapshot;
  }

  scoped_refptr<ProfileModelRegister> Register() {
    return base::MakeRefCounted<ProfileModelRegister>(store_.GetPath());
  }

  void Validate(ProfileModelRegister* model_register,
                const std::vector<service_mojom::ModelArtifactRegistrationPtr>&
                    snapshot) {
    auto prepared = model_register->PrepareSnapshot(snapshot);
    ASSERT_TRUE(prepared);
    model_register->CommitPreparedSnapshot(std::move(*prepared));
    task_environment_.RunUntilIdle();
  }

  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir store_;
};

TEST_F(ProfileModelRegisterTest, EmptyCompleteSnapshotRegistersNothing) {
  scoped_refptr<ProfileModelRegister> model_register = Register();
  std::vector<service_mojom::ModelArtifactRegistrationPtr> snapshot;
  Validate(model_register.get(), snapshot);
  EXPECT_FALSE(model_register->validation_pending_for_testing());
  EXPECT_EQ(model_register->registered_count_for_testing(), 0u);
}

TEST_F(ProfileModelRegisterTest, ExactCatalogFactsOpenAReadOnlyDescriptor) {
  const std::string bytes = "verified-model";
  Install("model.small", "2026-08-01", bytes);
  auto snapshot = Snapshot(Registration(bytes));
  scoped_refptr<ProfileModelRegister> model_register = Register();
  Validate(model_register.get(), snapshot);
  ASSERT_EQ(model_register->registered_count_for_testing(), 1u);

  tool_job_resources::ModelArtifactSource resolved;
  ASSERT_EQ(
      tool_job_resources::ModelArtifactResolution::kResolved,
      model_register->Resolve("model.small", "2026-08-01", false, &resolved));
  EXPECT_EQ(resolved.byte_length, bytes.size());
  EXPECT_EQ(resolved.kind, runtime_mojom::ToolModelArtifactKind::kGguf);
  EXPECT_EQ(resolved.digest, snapshot[0]->digest);
  std::vector<uint8_t> read(bytes.size());
  ASSERT_TRUE(resolved.file.ReadAndCheck(0, read));
  EXPECT_EQ(read, std::vector<uint8_t>(bytes.begin(), bytes.end()));
  EXPECT_FALSE(resolved.file.WriteAndCheck(0, base::as_byte_span(bytes)));
}

TEST_F(ProfileModelRegisterTest, ValidationNeverBlocksTheCallingSequence) {
  const std::string bytes = "verified-model";
  Install("model.small", "2026-08-01", bytes);
  auto snapshot = Snapshot(Registration(bytes));
  scoped_refptr<ProfileModelRegister> model_register = Register();
  auto prepared = model_register->PrepareSnapshot(snapshot);
  ASSERT_TRUE(prepared);
  model_register->CommitPreparedSnapshot(std::move(*prepared));
  EXPECT_TRUE(model_register->validation_pending_for_testing());
  tool_job_resources::ModelArtifactSource resolved;
  EXPECT_EQ(
      tool_job_resources::ModelArtifactResolution::kMissing,
      model_register->Resolve("model.small", "2026-08-01", false, &resolved));
  task_environment_.RunUntilIdle();
  EXPECT_FALSE(model_register->validation_pending_for_testing());
  EXPECT_EQ(model_register->registered_count_for_testing(), 1u);
}

TEST_F(ProfileModelRegisterTest, PreparedSnapshotIsDecodedExactlyOnce) {
  auto snapshot = Snapshot(Registration("verified-model"));
  scoped_refptr<ProfileModelRegister> model_register = Register();

  EXPECT_EQ(model_register->decode_invocation_count_for_testing(), 0u);
  auto prepared = model_register->PrepareSnapshot(snapshot);
  ASSERT_TRUE(prepared);
  EXPECT_EQ(model_register->decode_invocation_count_for_testing(), 1u);

  model_register->CommitPreparedSnapshot(std::move(*prepared));
  EXPECT_EQ(model_register->decode_invocation_count_for_testing(), 1u);
}

TEST_F(ProfileModelRegisterTest,
       EquivalentSnapshotDoesNotWithdrawOrRehashDescriptors) {
  const std::string weights = "verified-model";
  const std::string adapter = "verified-adapter";
  Install("model.small", "2026-08-01", weights);
  Install("model.adapter", "2026-08-01", adapter);
  std::vector<service_mojom::ModelArtifactRegistrationPtr> snapshot;
  snapshot.push_back(Registration(weights));
  snapshot.push_back(Registration(adapter, true));
  scoped_refptr<ProfileModelRegister> model_register = Register();
  Validate(model_register.get(), snapshot);
  ASSERT_EQ(model_register->registered_count_for_testing(), 2u);

  AssetStore store(store_.GetPath());
  EXPECT_EQ(store.Remove("model.small", "2026-08-01"), weights.size());
  EXPECT_EQ(store.Remove("model.adapter", "2026-08-01"), adapter.size());
  std::reverse(snapshot.begin(), snapshot.end());
  auto prepared = model_register->PrepareSnapshot(snapshot);
  ASSERT_TRUE(prepared);
  model_register->CommitPreparedSnapshot(std::move(*prepared));
  task_environment_.RunUntilIdle();
  EXPECT_FALSE(model_register->validation_pending_for_testing());
  EXPECT_EQ(model_register->registered_count_for_testing(), 2u);

  tool_job_resources::ModelArtifactSource resolved;
  EXPECT_EQ(
      tool_job_resources::ModelArtifactResolution::kResolved,
      model_register->Resolve("model.small", "2026-08-01", false, &resolved));
}

TEST_F(ProfileModelRegisterTest, MissingBytesNeverBecomeRegistered) {
  auto snapshot = Snapshot(Registration("verified-model"));
  scoped_refptr<ProfileModelRegister> model_register = Register();
  Validate(model_register.get(), snapshot);
  EXPECT_EQ(model_register->registered_count_for_testing(), 0u);
}

TEST_F(ProfileModelRegisterTest, WrongLengthNeverBecomesRegistered) {
  Install("model.small", "2026-08-01", "short");
  auto registration = Registration("short");
  ++registration->byte_length;
  auto snapshot = Snapshot(std::move(registration));
  scoped_refptr<ProfileModelRegister> model_register = Register();
  Validate(model_register.get(), snapshot);
  EXPECT_EQ(model_register->registered_count_for_testing(), 0u);
}

TEST_F(ProfileModelRegisterTest, WrongDigestNeverBecomesRegistered) {
  Install("model.small", "2026-08-01", "verified-model");
  auto registration = Registration("verified-model");
  registration->digest.assign(32u, 7u);
  auto snapshot = Snapshot(std::move(registration));
  scoped_refptr<ProfileModelRegister> model_register = Register();
  Validate(model_register.get(), snapshot);
  EXPECT_EQ(model_register->registered_count_for_testing(), 0u);
}

TEST_F(ProfileModelRegisterTest, UnsafeOrNonModelRowsRejectTheWholeSnapshot) {
  scoped_refptr<ProfileModelRegister> model_register = Register();
  auto unsafe = Registration("bytes");
  unsafe->asset_id = "../model";
  auto unsafe_snapshot = Snapshot(std::move(unsafe));
  EXPECT_FALSE(model_register->PrepareSnapshot(unsafe_snapshot));

  auto wrong_kind = Registration("bytes");
  wrong_kind->asset_kind = service_mojom::AssetKind::kPythonPackages;
  auto wrong_kind_snapshot = Snapshot(std::move(wrong_kind));
  EXPECT_FALSE(model_register->PrepareSnapshot(wrong_kind_snapshot));
}

TEST_F(ProfileModelRegisterTest, TokenizerCannotClaimTheAdapterRole) {
  scoped_refptr<ProfileModelRegister> model_register = Register();
  auto registration =
      Registration("bytes", true, service_mojom::AssetKind::kModelTokenizer);
  auto snapshot = Snapshot(std::move(registration));
  EXPECT_FALSE(model_register->PrepareSnapshot(snapshot));
}

TEST_F(ProfileModelRegisterTest, DuplicateIdentityRevisionIsMalformed) {
  scoped_refptr<ProfileModelRegister> model_register = Register();
  std::vector<service_mojom::ModelArtifactRegistrationPtr> snapshot;
  snapshot.push_back(Registration("bytes"));
  snapshot.push_back(Registration("bytes"));
  EXPECT_FALSE(model_register->PrepareSnapshot(snapshot));
}

TEST_F(ProfileModelRegisterTest,
       MalformedPreparationPreservesPreviouslyVerifiedSnapshot) {
  const std::string bytes = "verified-model";
  Install("model.small", "2026-08-01", bytes);
  auto snapshot = Snapshot(Registration(bytes));
  scoped_refptr<ProfileModelRegister> model_register = Register();
  Validate(model_register.get(), snapshot);
  ASSERT_EQ(model_register->registered_count_for_testing(), 1u);

  std::vector<service_mojom::ModelArtifactRegistrationPtr> malformed_snapshot;
  malformed_snapshot.push_back(Registration("candidate"));
  auto malformed = Registration("replacement", true);
  malformed->digest.pop_back();
  malformed_snapshot.push_back(std::move(malformed));
  EXPECT_FALSE(model_register->PrepareSnapshot(malformed_snapshot));
  EXPECT_EQ(model_register->registered_count_for_testing(), 1u);

  tool_job_resources::ModelArtifactSource resolved;
  EXPECT_EQ(
      tool_job_resources::ModelArtifactResolution::kResolved,
      model_register->Resolve("model.small", "2026-08-01", false, &resolved));
}

TEST_F(ProfileModelRegisterTest, WrongRevisionOrRoleCannotResolve) {
  const std::string bytes = "verified-model";
  Install("model.small", "2026-08-01", bytes);
  auto snapshot = Snapshot(Registration(bytes));
  scoped_refptr<ProfileModelRegister> model_register = Register();
  Validate(model_register.get(), snapshot);

  tool_job_resources::ModelArtifactSource resolved;
  EXPECT_EQ(
      tool_job_resources::ModelArtifactResolution::kMissing,
      model_register->Resolve("model.small", "2026-09-01", false, &resolved));
  EXPECT_EQ(
      tool_job_resources::ModelArtifactResolution::kMissing,
      model_register->Resolve("model.small", "2026-08-01", true, &resolved));
}

TEST_F(ProfileModelRegisterTest, ReplacementWithdrawsOldFactsImmediately) {
  const std::string bytes = "verified-model";
  Install("model.small", "2026-08-01", bytes);
  auto snapshot = Snapshot(Registration(bytes));
  scoped_refptr<ProfileModelRegister> model_register = Register();
  auto prepared = model_register->PrepareSnapshot(snapshot);
  ASSERT_TRUE(prepared);
  model_register->CommitPreparedSnapshot(std::move(*prepared));

  std::vector<service_mojom::ModelArtifactRegistrationPtr> empty;
  prepared = model_register->PrepareSnapshot(empty);
  ASSERT_TRUE(prepared);
  model_register->CommitPreparedSnapshot(std::move(*prepared));
  task_environment_.RunUntilIdle();
  EXPECT_FALSE(model_register->validation_pending_for_testing());
  EXPECT_EQ(model_register->registered_count_for_testing(), 0u);
}

TEST_F(ProfileModelRegisterTest, ResolutionPinsTheVerifiedFileNotAReusedPath) {
  const std::string original = "verified-model";
  Install("model.small", "2026-08-01", original);
  auto snapshot = Snapshot(Registration(original));
  scoped_refptr<ProfileModelRegister> model_register = Register();
  Validate(model_register.get(), snapshot);

  Install("model.small", "2026-08-01", "replacement");
  tool_job_resources::ModelArtifactSource resolved;
  ASSERT_EQ(
      tool_job_resources::ModelArtifactResolution::kResolved,
      model_register->Resolve("model.small", "2026-08-01", false, &resolved));
  std::vector<uint8_t> read(original.size());
  ASSERT_TRUE(resolved.file.ReadAndCheck(0, read));
  EXPECT_EQ(read, std::vector<uint8_t>(original.begin(), original.end()));
}

}  // namespace
}  // namespace taffy
