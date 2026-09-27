// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "taffy/renderer/semantic_graph_store.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

using IdentitySpace = SemanticGraphStore::IdentitySpace;

SemanticGraphStore MakeStore() {
  return SemanticGraphStore(FrameId("frame_main"), PageEpoch("epoch_a1"));
}

SemanticGraphStore::LiveNode Described(const SemanticGraphStore& store,
                                       const SemanticNodeId& id,
                                       int64_t source,
                                       SemanticRole role) {
  SemanticGraphStore::LiveNode node;
  node.node_id = id;
  node.dom_key = store.MakeKey(IdentitySpace::kDom, source);
  node.role = role;
  return node;
}

int64_t NextFrom(uint64_t& seed, int64_t bound) {
  seed = seed * 1664525u + 1013904223u;
  return static_cast<int64_t>((seed >> 16) % static_cast<uint64_t>(bound));
}

// Generated mutation sequences prove that recycling never reuses an identity,
// including interleavings that a short example would miss.
TEST(SemanticGraphStoreTest, NeverReusesAnIdOverAGeneratedMutationSequence) {
  constexpr int kDomNodePool = 12;
  constexpr int kOperations = 4000;

  SemanticGraphStore store = MakeStore();
  std::set<SemanticNodeId> ever_issued;
  std::set<SemanticNodeId> ever_retired;
  std::map<int64_t, SemanticNodeId> current;
  uint64_t seed = 0x5eed1234u;
  GraphRevision previous_revision = store.current_revision();

  for (int step = 0; step < kOperations; ++step) {
    const int64_t dom_node_id = NextFrom(seed, kDomNodePool);
    switch (NextFrom(seed, 5)) {
      case 0:
      case 1: {
        const SemanticNodeId id = store.AllocateOrLookup(
            store.MakeKey(IdentitySpace::kDom, dom_node_id));
        EXPECT_FALSE(ever_retired.contains(id));
        auto known = current.find(dom_node_id);
        if (known != current.end()) {
          EXPECT_EQ(known->second, id);
        } else {
          EXPECT_TRUE(ever_issued.insert(id).second);
          current.emplace(dom_node_id, id);
        }
        SemanticGraphStore::LiveNode node =
            Described(store, id, dom_node_id, SemanticRole::kListItem);
        node.states = {NodeState::kVisible};
        store.UpsertLiveNode(std::move(node));
        break;
      }
      case 2:
        for (const SemanticNodeId& retired :
             store.RetireByDomNode(IdentitySpace::kDom, dom_node_id)) {
          EXPECT_TRUE(ever_retired.insert(retired).second);
        }
        current.erase(dom_node_id);
        break;
      case 3: {
        auto known = current.find(dom_node_id);
        if (known != current.end()) {
          store.Retire(known->second);
          ever_retired.insert(known->second);
          current.erase(known);
        }
        break;
      }
      case 4: {
        auto known = current.find(dom_node_id);
        const std::optional<SemanticNodeId> previous_id =
            known == current.end()
                ? std::nullopt
                : std::optional<SemanticNodeId>(known->second);
        for (const SemanticNodeId& retired : store.AdvanceIdentityGeneration(
                 IdentitySpace::kDom, dom_node_id)) {
          EXPECT_TRUE(ever_retired.insert(retired).second);
        }
        if (previous_id) {
          current.erase(dom_node_id);
          const SemanticNodeId reobserved = store.AllocateOrLookup(
              store.MakeKey(IdentitySpace::kDom, dom_node_id));
          EXPECT_NE(reobserved, *previous_id);
          EXPECT_TRUE(ever_issued.insert(reobserved).second);
          current.emplace(dom_node_id, reobserved);
        }
        break;
      }
      default:
        FAIL() << "unreachable operation";
    }
    EXPECT_GE(store.current_revision().value(), previous_revision.value());
    previous_revision = store.current_revision();
  }

  for (const SemanticNodeId& retired : ever_retired) {
    EXPECT_EQ(
        store.Resolve(retired, PageEpoch("epoch_a1"), std::nullopt).status,
        SemanticGraphStore::ResolveStatus::kNodeGone);
  }
  for (const auto& entry : current) {
    EXPECT_FALSE(ever_retired.contains(entry.second));
  }
  EXPECT_GE(ever_issued.size(), current.size());
}

TEST(SemanticGraphStoreTest, IdentityBookkeepingStaysBounded) {
  SemanticGraphStore store = MakeStore();
  const size_t ceiling = SemanticGraphStore::MaxTrackedIdentities();
  for (int64_t source = 0; source < static_cast<int64_t>(ceiling * 3);
       ++source) {
    store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, source));
    store.RetireByDomNode(IdentitySpace::kDom, source);
    ASSERT_LE(store.tracked_identity_count(), ceiling) << "step " << source;
  }
  EXPECT_GT(store.identity_reset_count(), 0u);
}

TEST(SemanticGraphStoreTest, IdentityIsNotConfusedAcrossTheBound) {
  constexpr int64_t kPool = 8;
  SemanticGraphStore store = MakeStore();
  std::set<SemanticNodeId> ever_issued;
  std::vector<SemanticNodeId> issued_before_reset;

  for (size_t step = 0; store.identity_reset_count() == 0u; ++step) {
    ASSERT_LT(step, SemanticGraphStore::MaxTrackedIdentities() * 4);
    const int64_t source = static_cast<int64_t>(step % kPool);
    const SemanticNodeId id =
        store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, source));
    ASSERT_TRUE(ever_issued.insert(id).second);
    issued_before_reset.push_back(id);
    store.RetireByDomNode(IdentitySpace::kDom, source);
  }

  for (int64_t source = 0; source < kPool; ++source) {
    const SemanticNodeId id =
        store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, source));
    EXPECT_TRUE(ever_issued.insert(id).second);
  }
  for (const SemanticNodeId& id : issued_before_reset) {
    ASSERT_EQ(store.Resolve(id, PageEpoch("epoch_a1"), std::nullopt).status,
              SemanticGraphStore::ResolveStatus::kNodeGone);
  }
}

TEST(SemanticGraphStoreTest, ReverseIndexStaysConsistentUnderChurn) {
  constexpr int64_t kPool = 6;
  SemanticGraphStore store = MakeStore();
  std::map<int64_t, SemanticNodeId> expected;
  uint64_t seed = 0x1dea5eedu;

  for (int step = 0; step < 3000; ++step) {
    const int64_t source = NextFrom(seed, kPool);
    const int64_t roll = NextFrom(seed, 4);
    if (roll == 0) {
      store.RetireByDomNode(IdentitySpace::kDom, source);
      expected.erase(source);
    } else {
      const SemanticNodeId id =
          store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, source));
      expected.insert_or_assign(source, id);
      if (roll == 1) {
        store.UpsertLiveNode(
            Described(store, id, source, SemanticRole::kListItem));
      }
    }

    for (int64_t candidate = 0; candidate < kPool; ++candidate) {
      const std::optional<SemanticNodeId> found =
          store.Lookup(store.MakeKey(IdentitySpace::kDom, candidate));
      const auto known = expected.find(candidate);
      ASSERT_EQ(found.has_value(), known != expected.end())
          << "step " << step << ", source node " << candidate;
      ASSERT_TRUE(!found || *found == known->second)
          << "step " << step << ", source node " << candidate;
    }
  }
}

}  // namespace
}  // namespace taffy
