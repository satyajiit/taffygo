// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/task_download_ownership_registry.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

TEST(TaskDownloadOwnershipRegistryTest, RequiresTheExactFourPartOwner) {
  TaskDownloadOwnershipRegistry registry;
  registry.Record(TaskId{"task-1"}, "session-1", TabId{"tab-1"}, "guid-1");

  EXPECT_TRUE(registry.Owns(TaskId{"task-1"}, "session-1", TabId{"tab-1"},
                            "guid-1"));
  EXPECT_FALSE(registry.Owns(TaskId{"task-2"}, "session-1", TabId{"tab-1"},
                             "guid-1"));
  EXPECT_FALSE(registry.Owns(TaskId{"task-1"}, "session-2", TabId{"tab-1"},
                             "guid-1"));
  EXPECT_FALSE(registry.Owns(TaskId{"task-1"}, "session-1", TabId{"tab-2"},
                             "guid-1"));
  EXPECT_FALSE(registry.Owns(TaskId{"task-1"}, "session-1", TabId{"tab-1"},
                             "manual-guid"));
  EXPECT_TRUE(registry.WasStartedBy(TaskId{"task-1"}, "session-1", "guid-1"));
  EXPECT_FALSE(registry.WasStartedBy(TaskId{"task-2"}, "session-1", "guid-1"));
  EXPECT_FALSE(registry.WasStartedBy(TaskId{"task-1"}, "session-2", "guid-1"));
  EXPECT_FALSE(
      registry.WasStartedBy(TaskId{"task-1"}, "session-1", "manual-guid"));
}

TEST(TaskDownloadOwnershipRegistryTest, RetentionIsBoundedAndFailsClosed) {
  TaskDownloadOwnershipRegistry registry;
  for (size_t index = 0;
       index <= TaskDownloadOwnershipRegistry::kMaxEntries; ++index) {
    registry.Record(TaskId{"task-1"}, "session-1", TabId{"tab-1"},
                    "guid-" + std::to_string(index));
  }

  EXPECT_EQ(TaskDownloadOwnershipRegistry::kMaxEntries,
            registry.size_for_testing());
  EXPECT_FALSE(registry.Owns(TaskId{"task-1"}, "session-1", TabId{"tab-1"},
                             "guid-0"));
  EXPECT_TRUE(registry.Owns(TaskId{"task-1"}, "session-1", TabId{"tab-1"},
                            "guid-128"));
  EXPECT_FALSE(registry.WasStartedBy(TaskId{"task-1"}, "session-1", "guid-0"));
  EXPECT_TRUE(registry.WasStartedBy(TaskId{"task-1"}, "session-1", "guid-128"));
}

TEST(TaskDownloadOwnershipRegistryTest, ARepeatedGuidCannotChangeOwners) {
  TaskDownloadOwnershipRegistry registry;
  registry.Record(TaskId{"task-1"}, "session-1", TabId{"tab-1"}, "guid-1");
  registry.Record(TaskId{"task-2"}, "session-1", TabId{"tab-2"}, "guid-1");

  EXPECT_TRUE(registry.Owns(TaskId{"task-1"}, "session-1", TabId{"tab-1"},
                            "guid-1"));
  EXPECT_FALSE(registry.Owns(TaskId{"task-2"}, "session-1", TabId{"tab-2"},
                             "guid-1"));
  EXPECT_EQ(1u, registry.size_for_testing());
}

}  // namespace
}  // namespace taffy
