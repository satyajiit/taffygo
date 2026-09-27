// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/semantic_graph_store.h"

#include <utility>

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

TEST(SemanticGraphStoreSecurityTest,
     ContentTrustPersistsAndCanOnlyBecomeLessTrusted) {
  SemanticGraphStore store = MakeStore();
  const SemanticNodeId id =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 6));

  SemanticGraphStore::LiveNode node =
      Described(store, id, 6, SemanticRole::kParagraph);
  node.content_trust = RendererContentTrust::kFirstPartyDocument;
  store.UpsertLiveNode(node);
  const GraphRevision first_party_revision = store.current_revision();
  auto resolved =
      store.Resolve(id, PageEpoch("epoch_a1"), first_party_revision);
  ASSERT_TRUE(resolved.node.has_value());
  EXPECT_EQ(resolved.node->content_trust,
            RendererContentTrust::kFirstPartyDocument);

  node.content_trust = RendererContentTrust::kUserGeneratedContent;
  store.UpsertLiveNode(node);
  EXPECT_GT(store.current_revision(), first_party_revision);
  EXPECT_EQ(store.Resolve(id, PageEpoch("epoch_a1"), first_party_revision)
                .status,
            SemanticGraphStore::ResolveStatus::kStaleGraph);
  const GraphRevision less_trusted_revision = store.current_revision();
  resolved = store.Resolve(id, PageEpoch("epoch_a1"), less_trusted_revision);
  ASSERT_TRUE(resolved.node.has_value());
  EXPECT_EQ(resolved.node->content_trust,
            RendererContentTrust::kUserGeneratedContent);

  node.content_trust = RendererContentTrust::kFirstPartyDocument;
  store.UpsertLiveNode(node);
  EXPECT_EQ(store.current_revision(), less_trusted_revision);
  resolved = store.Resolve(id, PageEpoch("epoch_a1"), less_trusted_revision);
  ASSERT_TRUE(resolved.node.has_value());
  EXPECT_EQ(resolved.node->content_trust,
            RendererContentTrust::kUserGeneratedContent);
}

TEST(SemanticGraphStoreSecurityTest,
     VisualRedactionRefusesChallengeAndUnlocatableSecrets) {
  SemanticGraphStore store = MakeStore();
  const SemanticNodeId secret_id =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kAccessibility, 20));
  SemanticGraphStore::LiveNode secret =
      Described(store, secret_id, 20, SemanticRole::kTextField);
  secret.dom_key = store.MakeKey(IdentitySpace::kAccessibility, 20);
  secret.sensitivity = Sensitivity::kCredential;
  store.UpsertLiveNode(secret);
  EXPECT_FALSE(store.VisualRedactionDomNodeIds(8).has_value());

  SemanticGraphStore located_store = MakeStore();
  const SemanticNodeId located_id = located_store.AllocateOrLookup(
      located_store.MakeKey(IdentitySpace::kDom, 20));
  SemanticGraphStore::LiveNode located =
      Described(located_store, located_id, 20, SemanticRole::kTextField);
  located.sensitivity = Sensitivity::kCredential;
  located_store.UpsertLiveNode(located);
  const auto redactions = located_store.VisualRedactionDomNodeIds(8);
  ASSERT_TRUE(redactions.has_value());
  ASSERT_EQ(redactions->size(), 1u);
  EXPECT_EQ(redactions->front(), 20);

  located.form_semantics_authoritative = true;
  located.challenge_kind = ChallengeKind::kInteractive;
  located_store.UpsertLiveNode(located);
  EXPECT_FALSE(located_store.VisualRedactionDomNodeIds(8).has_value());
}

TEST(SemanticGraphStoreSecurityTest, VisualRedactionTargetsAreBounded) {
  SemanticGraphStore store = MakeStore();
  for (int source : {21, 22}) {
    const SemanticNodeId id =
        store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, source));
    SemanticGraphStore::LiveNode secret =
        Described(store, id, source, SemanticRole::kTextField);
    secret.sensitivity = Sensitivity::kOneTimeCode;
    store.UpsertLiveNode(std::move(secret));
  }
  EXPECT_FALSE(store.VisualRedactionDomNodeIds(1).has_value());
  EXPECT_TRUE(store.VisualRedactionDomNodeIds(2).has_value());
}

}  // namespace
}  // namespace taffy
