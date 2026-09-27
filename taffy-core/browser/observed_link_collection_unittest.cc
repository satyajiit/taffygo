// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/observed_link_collection.h"

#include <string>
#include <utility>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

constexpr char kDestination[] =
    "https://destination.test/path?q=tea%20cake#result";

ObservationRequest DestinationRequest() {
  ObservationRequest request;
  request.requested_fields.push_back(SemanticField::kDestination);
  return request;
}

mojom::SemanticNodePtr OrdinaryLink(std::string node_id,
                                    std::string destination) {
  auto node = mojom::SemanticNode::New();
  node->node_id = std::move(node_id);
  node->role = mojom::SemanticRole::kLink;
  node->actions.push_back(mojom::ActionType::kActivate);
  node->destination = mojom::Destination::New();
  node->destination->url_metadata = mojom::UrlMetadata::New();
  node->destination->url_metadata->disclosure = mojom::UrlDisclosure::kFullUrl;
  node->destination->url_metadata->url = std::move(destination);
  return node;
}

TEST(ObservedLinkCollectionTest, KeepsExactSafeLinksWithTheirUseFlags) {
  auto snapshot = mojom::PageSnapshot::New();
  snapshot->nodes.push_back(OrdinaryLink("link-1", kDestination));
  snapshot->nodes.push_back(OrdinaryLink("script", "javascript:alert(1)"));
  snapshot->nodes.push_back(
      OrdinaryLink("credentials", "https://name:secret@example.test/"));
  auto new_tab = OrdinaryLink("new-tab", "https://new.test/");
  new_tab->destination->opens_new_tab = true;
  snapshot->nodes.push_back(std::move(new_tab));
  auto download = OrdinaryLink("download", "https://download.test/");
  download->destination->is_download = true;
  snapshot->nodes.push_back(std::move(download));
  snapshot->nodes.push_back(OrdinaryLink(
      "oversized",
      "https://large.test/" +
          std::string(kMaxTransientObservedLinkDestinationBytes, 'x')));

  std::vector<TransientObservedLink> links;
  EXPECT_TRUE(
      CollectTransientObservedLinks(DestinationRequest(), *snapshot, &links));
  ASSERT_EQ(3u, links.size());
  EXPECT_EQ("download", links[0].node_id.value);
  EXPECT_TRUE(links[0].is_download);
  EXPECT_FALSE(links[0].opens_new_tab);
  EXPECT_EQ("link-1", links[1].node_id.value);
  EXPECT_EQ(kDestination, links[1].normalized_destination);
  EXPECT_FALSE(links[1].is_download);
  EXPECT_FALSE(links[1].opens_new_tab);
  EXPECT_EQ("new-tab", links[2].node_id.value);
  EXPECT_FALSE(links[2].is_download);
  EXPECT_TRUE(links[2].opens_new_tab);
}

TEST(ObservedLinkCollectionTest, FlaggedLinksRetainBrowserOnlyDestinations) {
  for (int flags = 1; flags <= 3; ++flags) {
    SCOPED_TRACE(flags);
    auto snapshot = mojom::PageSnapshot::New();
    auto link = OrdinaryLink("file-link", kDestination);
    link->destination->is_download = (flags & 1) != 0;
    link->destination->opens_new_tab = (flags & 2) != 0;
    snapshot->nodes.push_back(std::move(link));
    std::vector<TransientObservedLink> links;
    ASSERT_TRUE(
        CollectTransientObservedLinks(DestinationRequest(), *snapshot, &links));
    ASSERT_EQ(1u, links.size());
    EXPECT_EQ(kDestination, links.front().normalized_destination);
    EXPECT_EQ((flags & 1) != 0, links.front().is_download);
    EXPECT_EQ((flags & 2) != 0, links.front().opens_new_tab);
  }
}

TEST(ObservedLinkCollectionTest, ADuplicateIdentityIsUnresolvableAndNothingElseIs) {
  // Two links wearing one identifier make that identifier ambiguous, and a
  // capability minted for it would name whichever row survived. Neither may be
  // resolvable. The third link is not part of that question and stays: this
  // test used to assert the opposite, and the observation it took down with it
  // was every node on the page (decision 0172).
  auto snapshot = mojom::PageSnapshot::New();
  snapshot->nodes.push_back(OrdinaryLink("same", kDestination));
  snapshot->nodes.push_back(OrdinaryLink("same", "https://different.test/"));
  snapshot->nodes.push_back(OrdinaryLink("other", "https://other.test/"));
  std::vector<TransientObservedLink> links = {
      {.node_id = SemanticNodeId{"older"},
       .normalized_destination = "https://older.test/"}};

  ASSERT_TRUE(
      CollectTransientObservedLinks(DestinationRequest(), *snapshot, &links));

  ASSERT_EQ(1u, links.size());
  EXPECT_EQ("other", links.front().node_id.value);
  EXPECT_EQ("https://other.test/", links.front().normalized_destination);
}

TEST(ObservedLinkCollectionTest, UnrequestedDestinationRetainsNoAuthority) {
  auto snapshot = mojom::PageSnapshot::New();
  snapshot->nodes.push_back(OrdinaryLink("link-1", kDestination));
  std::vector<TransientObservedLink> links = {
      {.node_id = SemanticNodeId{"older"},
       .normalized_destination = "https://older.test/"}};

  EXPECT_TRUE(
      CollectTransientObservedLinks(ObservationRequest(), *snapshot, &links));
  EXPECT_TRUE(links.empty());
}

}  // namespace
}  // namespace taffy
