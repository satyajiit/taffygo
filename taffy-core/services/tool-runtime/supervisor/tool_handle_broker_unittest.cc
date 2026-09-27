// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/tool-runtime/supervisor/tool_handle_broker.h"

#include <string>

#include "base/files/file.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

using Mode = ToolHandleBroker::Mode;

TEST(ToolHandleBrokerTest, AnIdentifierIsOnlyEverTheOneItWasMintedFor) {
  ToolHandleBroker broker;
  EXPECT_FALSE(broker.can_resolve());
  const std::string read = broker.Mint("job-1", Mode::kRead);
  ASSERT_FALSE(read.empty());
  EXPECT_TRUE(broker.IsMintedFor("job-1", read, Mode::kRead));

  // The same identifier for another job, in the other mode, or invented
  // outright, is not this identifier.
  EXPECT_FALSE(broker.IsMintedFor("job-2", read, Mode::kRead));
  EXPECT_FALSE(broker.IsMintedFor("job-1", read, Mode::kWrite));
  EXPECT_FALSE(broker.IsMintedFor("job-1", "0123456789abcdef0123456789abcdef",
                                  Mode::kRead));
  // Two mints do not collide, and neither is derivable from the other.
  EXPECT_NE(read, broker.Mint("job-1", Mode::kRead));
  // A job identity is required, because an identifier bound to nothing would
  // be an identifier bound to every job.
  EXPECT_TRUE(broker.Mint(std::string(), Mode::kRead).empty());
}

TEST(ToolHandleBrokerTest, WithoutAPortThereIsNothingToResolve) {
  ToolHandleBroker broker;
  const std::string read = broker.Mint("job-1", Mode::kRead);
  // Not an approximation of a file, and not a crash: an invalid one, which is
  // what the caller refuses the job on.
  EXPECT_FALSE(broker.Resolve("job-1", read, Mode::kRead).IsValid());
}

TEST(ToolHandleBrokerTest, RevocationIsByJobAndByGeneration) {
  base::ScopedTempDir scratch;
  ASSERT_TRUE(scratch.CreateUniqueTempDir());
  ToolHandleBroker broker;
  broker.SetResolvePort(base::BindRepeating(
      [](const base::FilePath& directory, const std::string& handle_id, Mode) {
        return base::File(directory.AppendASCII(handle_id),
                          base::File::FLAG_CREATE_ALWAYS |
                              base::File::FLAG_READ | base::File::FLAG_WRITE);
      },
      scratch.GetPath()));
  EXPECT_TRUE(broker.can_resolve());

  const std::string first = broker.Mint("job-1", Mode::kRead);
  const std::string second = broker.Mint("job-2", Mode::kWrite);
  EXPECT_TRUE(broker.Resolve("job-1", first, Mode::kRead).IsValid());
  // A port is not an override: an identifier this broker did not mint for this
  // job never reaches the port at all.
  EXPECT_FALSE(broker.Resolve("job-2", first, Mode::kRead).IsValid());

  broker.RevokeJob("job-1");
  EXPECT_FALSE(broker.Resolve("job-1", first, Mode::kRead).IsValid());
  EXPECT_TRUE(broker.Resolve("job-2", second, Mode::kWrite).IsValid());

  broker.RevokeAll();
  EXPECT_EQ(broker.minted_count_for_testing(), 0u);
  EXPECT_FALSE(broker.Resolve("job-2", second, Mode::kWrite).IsValid());
}

}  // namespace
}  // namespace taffy
