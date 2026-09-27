// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/page_video_caption_facts.h"

#include <string>
#include <utility>

#include "taffy/browser/seeded_secret_corpus.h"
#include "taffy/components/intelligence/content/renderer_text_rescan.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace core_mojom = core_service::mojom;

TEST(PageVideoCaptionFactsTest, LoadedCueKeepsNativeTimestampAndCoordinate) {
  auto inspected = mojom::MediaTargetResult::New();
  auto cue = mojom::MediaCaptionCue::New();
  cue->track_index_plus_one = 2u;
  cue->cue_index_plus_one = 7u;
  cue->text = "Battery life starts here";
  cue->timestamp_start_ms = 12'500u;
  cue->timestamp_end_ms = 15'000u;
  inspected->loaded_caption_cues.push_back(std::move(cue));
  auto media = core_mojom::MediaObservationResult::New();
  RescanTally rescan;

  ASSERT_TRUE(PopulateVideoCaptionFacts(*inspected, *media, &rescan));
  ASSERT_EQ(media->facts.size(), 1u);
  EXPECT_TRUE(media->has_meaningful_text);
  EXPECT_EQ(media->facts[0]->kind, core_mojom::MediaFactKind::kTranscript);
  EXPECT_EQ(media->facts[0]->evidence,
            core_mojom::MediaEvidenceKind::kCaptionTrack);
  EXPECT_EQ(media->facts[0]->source_locator, "video:track/2/cue/7");
  EXPECT_EQ(media->facts[0]->timestamp_start_ms, 12'500u);
  EXPECT_EQ(media->facts[0]->timestamp_end_ms, 15'000u);
}

TEST(PageVideoCaptionFactsTest, NoLoadedCueStatesTheVisualOnlyBoundary) {
  auto inspected = mojom::MediaTargetResult::New();
  auto media = core_mojom::MediaObservationResult::New();
  RescanTally rescan;

  ASSERT_TRUE(PopulateVideoCaptionFacts(*inspected, *media, &rescan));
  ASSERT_EQ(media->facts.size(), 1u);
  EXPECT_FALSE(media->has_meaningful_text);
  EXPECT_EQ(media->facts[0]->kind, core_mojom::MediaFactKind::kMetadata);
  EXPECT_EQ(media->facts[0]->source_locator, "video:captions");
}

TEST(PageVideoCaptionFactsTest, MalformedCueRejectsTheWholeResult) {
  auto inspected = mojom::MediaTargetResult::New();
  auto cue = mojom::MediaCaptionCue::New();
  cue->track_index_plus_one = 1u;
  cue->cue_index_plus_one = 1u;
  cue->text = "invalid time";
  cue->timestamp_start_ms = 20u;
  cue->timestamp_end_ms = 10u;
  inspected->loaded_caption_cues.push_back(std::move(cue));
  auto media = core_mojom::MediaObservationResult::New();
  RescanTally rescan;

  EXPECT_FALSE(PopulateVideoCaptionFacts(*inspected, *media, &rescan));
  EXPECT_TRUE(media->facts.empty());
}

TEST(PageVideoCaptionFactsTest, RendererSecretIsRemovedAndCounted) {
  ASSERT_GT(GetSeededSecretCount(), 0u);
  const std::string token(GetSeededSecret(0).token);
  auto inspected = mojom::MediaTargetResult::New();
  auto cue = mojom::MediaCaptionCue::New();
  cue->track_index_plus_one = 1u;
  cue->cue_index_plus_one = 1u;
  cue->text = "caption " + token;
  cue->timestamp_start_ms = 1u;
  cue->timestamp_end_ms = 2u;
  inspected->loaded_caption_cues.push_back(std::move(cue));
  auto media = core_mojom::MediaObservationResult::New();
  RescanTally rescan;

  ASSERT_TRUE(PopulateVideoCaptionFacts(*inspected, *media, &rescan));
  ASSERT_EQ(media->facts.size(), 1u);
  EXPECT_EQ(media->facts[0]->text.find(token), std::string::npos);
  EXPECT_GT(rescan.redacted_span_count, 0u);
  EXPECT_GT(rescan.canary_hit_count, 0u);
}

}  // namespace
}  // namespace taffy
