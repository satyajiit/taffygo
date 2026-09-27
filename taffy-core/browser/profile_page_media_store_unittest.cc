// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/profile_page_media_store.h"

#include <array>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/scoped_refptr.h"
#include "base/time/time.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

class ProfilePageMediaStoreTest : public testing::Test {
 protected:
  static ProfilePageMediaStore::StoreRequest Request(
      std::string task_id = "task-1",
      uint64_t generation = 7u) {
    return ProfilePageMediaStore::StoreRequest{
        .task_id = std::move(task_id),
        .observation_effect_id = "observation-1",
        .service_generation = generation,
        .tab_id = "tab-1",
        .frame_id = "frame-1",
        .page_epoch = "epoch-1",
        .graph_revision = 3u,
        .node_id = "node-1",
        .width_px = 2u,
        .height_px = 2u,
    };
  }

  static std::vector<uint8_t> Png() {
    return {
        0x89u, 0x50u, 0x4eu, 0x47u, 0x0du, 0x0au, 0x1au, 0x0au, 0x00u,
        0x00u, 0x00u, 0x0du, 'I',   'H',   'D',   'R',   0x00u, 0x00u,
        0x00u, 0x02u, 0x00u, 0x00u, 0x00u, 0x02u, 0x08u, 0x06u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    };
  }

  content::BrowserTaskEnvironment task_environment_;
  bool document_is_live_ = true;
  scoped_refptr<ProfilePageMediaStore> store_ =
      base::MakeRefCounted<ProfilePageMediaStore>(base::BindRepeating(
          [](bool* live, const ProfilePageMediaStore::StoreRequest&) {
            return *live;
          },
          &document_is_live_));
  const base::TimeTicks now_ = base::TimeTicks() + base::Hours(1);
};

TEST_F(ProfilePageMediaStoreTest, PageContentClaimMovesBytesExactlyOnce) {
  const std::vector<uint8_t> expected = Png();
  auto stored = store_->StoreRenderedPng(Request(), Png(), now_);
  ASSERT_TRUE(stored.has_value());
  auto resolved = store_->TakeForModel(
      "task-1", 7u, stored->handle, "image/png",
      mojom::DisclosureClass::kPageContent, now_ + base::Seconds(1));
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(resolved->bytes, expected);
  EXPECT_EQ(resolved->page_epoch, "epoch-1");
  EXPECT_EQ(resolved->graph_revision, 3u);
  EXPECT_EQ(resolved->node_id, std::optional<std::string>("node-1"));
  EXPECT_EQ(store_->entry_count_for_testing(), 0u);
  EXPECT_FALSE(store_->TakeForModel("task-1", 7u, stored->handle, "image/png",
                                    mojom::DisclosureClass::kPageContent,
                                    now_ + base::Seconds(2)));
}

TEST_F(ProfilePageMediaStoreTest, WrongTaskBurnsTheKnownHandle) {
  auto stored = store_->StoreRenderedPng(Request(), Png(), now_);
  ASSERT_TRUE(stored.has_value());
  EXPECT_FALSE(
      store_->TakeForModel("task-other", 7u, stored->handle, "image/png",
                           mojom::DisclosureClass::kPageContent, now_));
  EXPECT_FALSE(store_->TakeForModel("task-1", 7u, stored->handle, "image/png",
                                    mojom::DisclosureClass::kPageContent,
                                    now_));
}

TEST_F(ProfilePageMediaStoreTest, WrongMimeOrDisclosureBurnsTheHandle) {
  auto mime = store_->StoreRenderedPng(Request(), Png(), now_);
  ASSERT_TRUE(mime.has_value());
  EXPECT_FALSE(store_->TakeForModel("task-1", 7u, mime->handle, "image/jpeg",
                                    mojom::DisclosureClass::kPageContent,
                                    now_));

  auto disclosure = store_->StoreRenderedPng(Request(), Png(), now_);
  ASSERT_TRUE(disclosure.has_value());
  EXPECT_FALSE(
      store_->TakeForModel("task-1", 7u, disclosure->handle, "image/png",
                           mojom::DisclosureClass::kUserSelectedContent, now_));
  EXPECT_EQ(store_->entry_count_for_testing(), 0u);
}

TEST_F(ProfilePageMediaStoreTest, GenerationTaskAndDocumentRevocationAreExact) {
  auto first = store_->StoreRenderedPng(Request("task-1", 7u), Png(), now_);
  auto second = store_->StoreRenderedPng(Request("task-2", 7u), Png(), now_);
  auto third = store_->StoreRenderedPng(Request("task-1", 8u), Png(), now_);
  ASSERT_TRUE(first && second && third);
  store_->RevokeTask("task-1", 7u);
  EXPECT_EQ(store_->entry_count_for_testing(), 2u);
  store_->RevokeGeneration(7u);
  EXPECT_EQ(store_->entry_count_for_testing(), 1u);
  store_->RevokeDocument("tab-1", "epoch-1");
  EXPECT_EQ(store_->entry_count_for_testing(), 0u);
}

TEST_F(ProfilePageMediaStoreTest, ExpiredAttachmentIsPrunedAndNeverReturned) {
  auto stored = store_->StoreRenderedPng(Request(), Png(), now_);
  ASSERT_TRUE(stored.has_value());
  EXPECT_FALSE(store_->TakeForModel("task-1", 7u, stored->handle, "image/png",
                                    mojom::DisclosureClass::kPageContent,
                                    now_ + base::Minutes(3)));
  EXPECT_EQ(store_->entry_count_for_testing(), 0u);
  EXPECT_EQ(store_->total_bytes_for_testing(), 0u);
}

TEST_F(ProfilePageMediaStoreTest, StaleDocumentBurnsTheKnownHandle) {
  auto stored = store_->StoreRenderedPng(Request(), Png(), now_);
  ASSERT_TRUE(stored.has_value());
  document_is_live_ = false;
  EXPECT_FALSE(store_->TakeForModel("task-1", 7u, stored->handle, "image/png",
                                    mojom::DisclosureClass::kPageContent,
                                    now_));
  document_is_live_ = true;
  EXPECT_FALSE(store_->TakeForModel("task-1", 7u, stored->handle, "image/png",
                                    mojom::DisclosureClass::kPageContent,
                                    now_));
}

TEST_F(ProfilePageMediaStoreTest, InvalidOrUnboundedInputsNeverEnterCustody) {
  EXPECT_FALSE(store_->StoreRenderedPng(Request(), {1u, 2u, 3u}, now_));
  auto zero = Request();
  zero.width_px = 0u;
  EXPECT_FALSE(store_->StoreRenderedPng(std::move(zero), Png(), now_));
  auto too_wide = Request();
  too_wide.width_px = static_cast<uint32_t>(mojom::kMaxMediaDimensionPx + 1u);
  EXPECT_FALSE(store_->StoreRenderedPng(std::move(too_wide), Png(), now_));

  for (size_t index = 0; index < 8u; ++index) {
    auto request = Request("task-" + std::to_string(index), 7u);
    ASSERT_TRUE(store_->StoreRenderedPng(std::move(request), Png(), now_));
  }
  EXPECT_FALSE(store_->StoreRenderedPng(Request("task-9", 7u), Png(), now_));
  EXPECT_EQ(store_->entry_count_for_testing(), 8u);
}

TEST_F(ProfilePageMediaStoreTest, HeaderDimensionMismatchNeverEntersCustody) {
  auto mismatch = Request();
  mismatch.width_px = 3u;
  EXPECT_FALSE(store_->StoreRenderedPng(std::move(mismatch), Png(), now_));
  EXPECT_EQ(store_->entry_count_for_testing(), 0u);
}

}  // namespace
}  // namespace taffy
