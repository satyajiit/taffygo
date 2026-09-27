// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_state_cache.h"

#include <stdint.h>

#include <vector>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

mojom::CoreStateUpdatePtr State(uint64_t generation,
                                uint64_t sequence,
                                uint8_t marker) {
  auto state = mojom::CoreStateUpdate::New();
  state->service_generation = generation;
  state->sequence = sequence;
  state->core_status_schema_version = 1;
  state->payload = std::vector<uint8_t>{marker};
  return state;
}

TEST(CoreStateCacheTest, RejectsWrongGenerationDuplicateAndOutOfOrderState) {
  CoreStateCache cache;

  EXPECT_FALSE(cache.Accept(nullptr, 4));
  EXPECT_FALSE(cache.Accept(State(3, 1, 1), 4));
  EXPECT_FALSE(cache.Accept(State(4, 0, 1), 4));
  EXPECT_FALSE(cache.Accept(State(4, 2, 2), 4));
  ASSERT_TRUE(cache.Accept(State(4, 1, 1), 4));
  EXPECT_FALSE(cache.Accept(State(4, 1, 3), 4));
  EXPECT_FALSE(cache.Accept(State(4, 0, 4), 4));

  ASSERT_TRUE(cache.latest());
  EXPECT_EQ(1u, cache.latest()->sequence);
  EXPECT_EQ(std::vector<uint8_t>({1}), cache.latest()->payload);
}

TEST(CoreStateCacheTest, RetainsLatestStateForLateObserverAndClearsOnGeneration) {
  CoreStateCache cache;
  ASSERT_TRUE(cache.Accept(State(7, 1, 1), 7));
  EXPECT_FALSE(cache.Accept(State(7, 3, 3), 7));
  ASSERT_TRUE(cache.Accept(State(7, 2, 2), 7));

  const mojom::CoreStateUpdate* replay = cache.latest();
  ASSERT_TRUE(replay);
  EXPECT_EQ(7u, replay->service_generation);
  EXPECT_EQ(2u, replay->sequence);

  cache.Reset();
  EXPECT_FALSE(cache.latest());
  EXPECT_EQ(0u, cache.last_sequence());
  ASSERT_TRUE(cache.Accept(State(8, 1, 8), 8));
  EXPECT_EQ(8u, cache.latest()->service_generation);
}

}  // namespace
}  // namespace taffy
