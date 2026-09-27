// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/profile_tool_handle_store.h"

#include <string>
#include <utility>

#include "base/files/file.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/string_number_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

class ProfileToolHandleStoreTest : public testing::Test {
 public:
  void SetUp() override { ASSERT_TRUE(scratch_.CreateUniqueTempDir()); }

  base::File Open(const std::string& name) {
    return base::File(scratch_.GetPath().AppendASCII(name),
                      base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_READ |
                          base::File::FLAG_WRITE);
  }

 protected:
  base::ScopedTempDir scratch_;
  scoped_refptr<ProfileToolHandleStore> store_ =
      base::MakeRefCounted<ProfileToolHandleStore>();
  ToolHandleBroker broker_;
};

TEST_F(ProfileToolHandleStoreTest, AnIdentifierNobodyAdmittedNamesNothing) {
  // The state of every device today: nothing admits a resource, so every
  // identifier the broker could mint resolves to nothing.
  broker_.SetResolvePort(store_->GetResolvePort());
  const std::string handle =
      broker_.Mint("job-1", ToolHandleBroker::Mode::kRead);
  EXPECT_FALSE(
      broker_.Resolve("job-1", handle, ToolHandleBroker::Mode::kRead)
          .IsValid());
}

TEST_F(ProfileToolHandleStoreTest, AnAdmittedResourceArrivesOpen) {
  broker_.SetResolvePort(store_->GetResolvePort());
  const std::string handle =
      broker_.Mint("job-1", ToolHandleBroker::Mode::kRead);
  ASSERT_TRUE(
      store_->Admit(handle, ToolHandleBroker::Mode::kRead, Open("input.mp4")));
  base::File resolved =
      broker_.Resolve("job-1", handle, ToolHandleBroker::Mode::kRead);
  EXPECT_TRUE(resolved.IsValid());
  // The store holds nothing afterwards: one ask per identifier.
  EXPECT_EQ(store_->size_for_testing(), 0u);
}

TEST_F(ProfileToolHandleStoreTest, AResolvedIdentifierIsSpent) {
  broker_.SetResolvePort(store_->GetResolvePort());
  const std::string handle =
      broker_.Mint("job-1", ToolHandleBroker::Mode::kRead);
  ASSERT_TRUE(
      store_->Admit(handle, ToolHandleBroker::Mode::kRead, Open("input.mp4")));
  EXPECT_TRUE(broker_.Resolve("job-1", handle, ToolHandleBroker::Mode::kRead)
                  .IsValid());
  // The broker still says the identifier was minted for this job, and the
  // resource is still gone. Two halves, and the resource lives in this one.
  EXPECT_TRUE(
      broker_.IsMintedFor("job-1", handle, ToolHandleBroker::Mode::kRead));
  EXPECT_FALSE(broker_.Resolve("job-1", handle, ToolHandleBroker::Mode::kRead)
                   .IsValid());
}

TEST_F(ProfileToolHandleStoreTest, AnotherJobReachesNothing) {
  broker_.SetResolvePort(store_->GetResolvePort());
  const std::string handle =
      broker_.Mint("job-1", ToolHandleBroker::Mode::kRead);
  ASSERT_TRUE(
      store_->Admit(handle, ToolHandleBroker::Mode::kRead, Open("input.mp4")));
  EXPECT_FALSE(broker_.Resolve("job-2", handle, ToolHandleBroker::Mode::kRead)
                   .IsValid());
  // Refused before the port was reached, so the resource is still held.
  EXPECT_EQ(store_->size_for_testing(), 1u);
}

TEST_F(ProfileToolHandleStoreTest, AGenerationChangeRevokesTheIdentifier) {
  broker_.SetResolvePort(store_->GetResolvePort());
  const std::string handle =
      broker_.Mint("job-1", ToolHandleBroker::Mode::kRead);
  ASSERT_TRUE(
      store_->Admit(handle, ToolHandleBroker::Mode::kRead, Open("input.mp4")));
  broker_.RevokeAll();
  EXPECT_FALSE(broker_.Resolve("job-1", handle, ToolHandleBroker::Mode::kRead)
                   .IsValid());
}

TEST_F(ProfileToolHandleStoreTest, TheTwoHalvesMustAgreeOnTheMode) {
  // The broker minted this for writing and the store admitted it for reading.
  // Neither half is entitled to pick, so the identifier stops naming anything.
  const std::string handle =
      broker_.Mint("job-1", ToolHandleBroker::Mode::kWrite);
  ASSERT_TRUE(
      store_->Admit(handle, ToolHandleBroker::Mode::kRead, Open("out.wav")));
  EXPECT_FALSE(
      store_->GetResolvePort().Run(handle, ToolHandleBroker::Mode::kWrite)
          .IsValid());
  EXPECT_EQ(store_->size_for_testing(), 0u);
  EXPECT_FALSE(
      store_->GetResolvePort().Run(handle, ToolHandleBroker::Mode::kRead)
          .IsValid());
}

TEST_F(ProfileToolHandleStoreTest, ADescriptorThatIsNotOpenIsNotAdmitted) {
  EXPECT_FALSE(store_->Admit("handle-1", ToolHandleBroker::Mode::kRead,
                             base::File()));
  EXPECT_EQ(store_->size_for_testing(), 0u);
}

TEST_F(ProfileToolHandleStoreTest, AnIdentifierIsAdmittedOnce) {
  ASSERT_TRUE(store_->Admit("handle-1", ToolHandleBroker::Mode::kRead,
                            Open("first.mp4")));
  // A second admission would decide, silently, which of two resources one
  // identifier stands for.
  EXPECT_FALSE(store_->Admit("handle-1", ToolHandleBroker::Mode::kRead,
                             Open("second.mp4")));
  EXPECT_EQ(store_->size_for_testing(), 1u);
}

TEST_F(ProfileToolHandleStoreTest, AnEmptyIdentifierIsNotAdmitted) {
  EXPECT_FALSE(
      store_->Admit(std::string(), ToolHandleBroker::Mode::kRead,
                    Open("input.mp4")));
  EXPECT_EQ(store_->size_for_testing(), 0u);
}

TEST_F(ProfileToolHandleStoreTest, OutstandingResourcesAreBounded) {
  for (int index = 0; index < 32; ++index) {
    ASSERT_TRUE(store_->Admit("handle-" + base::NumberToString(index),
                              ToolHandleBroker::Mode::kRead,
                              Open("input.mp4")));
  }
  EXPECT_FALSE(store_->Admit("handle-32", ToolHandleBroker::Mode::kRead,
                             Open("input.mp4")));
  EXPECT_EQ(store_->size_for_testing(), 32u);
  store_->ForgetAll();
  EXPECT_EQ(store_->size_for_testing(), 0u);
  EXPECT_TRUE(store_->Admit("handle-32", ToolHandleBroker::Mode::kRead,
                            Open("input.mp4")));
}

TEST_F(ProfileToolHandleStoreTest, ForgottenIdentifiersNameNothing) {
  ASSERT_TRUE(store_->Admit("handle-1", ToolHandleBroker::Mode::kRead,
                            Open("input.mp4")));
  store_->Forget("handle-1");
  EXPECT_FALSE(
      store_->GetResolvePort().Run("handle-1", ToolHandleBroker::Mode::kRead)
          .IsValid());
}

}  // namespace
}  // namespace taffy
