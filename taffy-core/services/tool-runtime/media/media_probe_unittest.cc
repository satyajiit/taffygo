// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/tool-runtime/media/media_probe.h"

#include <utility>
#include <vector>

#include "base/files/file.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "taffy/services/tool-runtime/media/test/synthetic_media.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::media_tool {
namespace {

class MediaProbeTest : public ::testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  ProbeResult Probe(base::File file, uint64_t budget) {
    base::test::TestFuture<ProbeResult> future;
    ProbeMedia(std::move(file), budget, future.GetCallback());
    return future.Take();
  }

  base::File SilentWave(uint32_t duration_ms) {
    return test::WriteReadOnlyFile(temp_dir_.GetPath(),
                                   test::BuildSilentWave(duration_ms));
  }

  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir temp_dir_;
};

TEST_F(MediaProbeTest, ReadsDurationAndStreamCountsFromAContainer) {
  const ProbeResult result = Probe(SilentWave(2000u), 1u << 20);
  ASSERT_TRUE(result.has_value()) << static_cast<int>(result.error());
  EXPECT_EQ(1u, result->audio_streams);
  EXPECT_EQ(0u, result->video_streams);
  EXPECT_EQ(0u, result->width);
  EXPECT_EQ(0u, result->height);
  // The container declares 2,000 ms of 8 kHz mono. A demuxer is allowed to
  // round, so the assertion is on the neighbourhood rather than the value: a
  // duration read from the wrong field is not within 100 ms of the right one.
  EXPECT_NEAR(2000.0, static_cast<double>(result->duration_ms), 100.0);
}

TEST_F(MediaProbeTest, RefusesAFileLargerThanTheJobsOwnBudget) {
  const std::vector<uint8_t> bytes = test::BuildSilentWave(1000u);
  base::File file = test::WriteReadOnlyFile(temp_dir_.GetPath(), bytes);
  ASSERT_TRUE(file.IsValid());
  const ProbeResult result = Probe(std::move(file), bytes.size() - 1u);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(ProbeFailure::kInputTooLarge, result.error());
}

TEST_F(MediaProbeTest, RefusesBytesThatAreNotAContainer) {
  const std::vector<uint8_t> bytes(4096u, 0x41u);
  base::File file = test::WriteReadOnlyFile(temp_dir_.GetPath(), bytes);
  ASSERT_TRUE(file.IsValid());
  const ProbeResult result = Probe(std::move(file), 1u << 20);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(ProbeFailure::kUnparseableContainer, result.error());
}

TEST_F(MediaProbeTest, RefusesAnInvalidDescriptorRatherThanReadingZeroes) {
  const ProbeResult result = Probe(base::File(), 1u << 20);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(ProbeFailure::kUnreadableFile, result.error());
}

TEST_F(MediaProbeTest, RefusesAnEmptyFile) {
  base::File file = test::WriteReadOnlyFile(temp_dir_.GetPath(), {});
  ASSERT_TRUE(file.IsValid());
  const ProbeResult result = Probe(std::move(file), 1u << 20);
  ASSERT_FALSE(result.has_value());
  // Empty is not "a container with nothing in it": the demuxer never gets far
  // enough to say what it is, and the reading must not come back as success.
  EXPECT_NE(ProbeFailure::kNoStreams, result.error());
}

TEST_F(MediaProbeTest, EachRunIsIndependentOfEveryOther) {
  // Four probes on one sequence, interleaved by the task environment. The run
  // owns itself, so a reading that leaked between two of them would show up
  // here as the wrong duration rather than as a crash.
  base::test::TestFuture<ProbeResult> a;
  base::test::TestFuture<ProbeResult> b;
  ProbeMedia(SilentWave(500u), 1u << 20, a.GetCallback());
  ProbeMedia(SilentWave(3000u), 1u << 20, b.GetCallback());
  const ProbeResult first = a.Take();
  const ProbeResult second = b.Take();
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_NEAR(500.0, static_cast<double>(first->duration_ms), 100.0);
  EXPECT_NEAR(3000.0, static_cast<double>(second->duration_ms), 100.0);
}

}  // namespace
}  // namespace taffy::media_tool
