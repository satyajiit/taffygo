// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/bip_graph_payload.h"

#include <stdint.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "taffy/browser/seeded_secret_corpus.h"
#include "taffy/components/intelligence/content/graph_payload_encoder.h"
#include "taffy/components/intelligence/content/renderer_text_rescan.h"
#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"
#include "taffy/test/support/bip_graph_payload_reader.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

// The browser's identity for the document under test, deliberately unequal to
// the "frame-1" the snapshots below carry. The renderer mints its own frame
// identity in its own namespace and stamps it on every node; the browser
// overwrites it while framing the payload. Keeping the two values distinct
// here is what lets a test tell a rewrite from a passthrough.
FrameId BrowserFrameId() {
  return FrameId{"browser-frame-9"};
}

// The snapshot the golden graph payload is built from: a sign-in button whose
// name may cross, and a password field whose name may not.
mojom::PageSnapshotPtr GoldenSnapshot() {
  auto snapshot = mojom::PageSnapshot::New();
  snapshot->schema_version = "0.6";

  auto button = mojom::SemanticNode::New();
  button->node_id = "n1";
  button->frame_id = "frame-1";
  button->role = mojom::SemanticRole::kButton;
  button->name = "Sign in";
  button->sensitivity = mojom::Sensitivity::kNotSensitive;
  auto label = mojom::TextRun::New();
  label->text = "Sign in";
  label->source_kind = mojom::SourceKind::kDom;
  label->sensitivity = mojom::Sensitivity::kNotSensitive;
  label->truncated = false;
  button->text_runs.push_back(std::move(label));

  auto password = mojom::SemanticNode::New();
  password->node_id = "n2";
  password->frame_id = "frame-1";
  password->role = mojom::SemanticRole::kTextField;
  password->name = "Password";
  password->sensitivity = mojom::Sensitivity::kCredential;
  auto value = mojom::ValueDescriptor::New();
  value->kind = mojom::ValueKind::kSecretWithheld;
  value->present = true;
  value->redacted = true;
  password->value_descriptor = std::move(value);

  auto edge = mojom::SemanticEdge::New();
  edge->from_frame_id = "frame-1";
  edge->from_node_id = "n1";
  edge->to_frame_id = "frame-1";
  edge->to_node_id = "n2";
  edge->relationship = mojom::RelationshipKind::kLabels;
  edge->inferred = false;

  snapshot->nodes.push_back(std::move(button));
  snapshot->nodes.push_back(std::move(password));
  snapshot->edges.push_back(std::move(edge));
  return snapshot;
}

TEST(BipGraphPayloadTest, TheGraphEncoderReportsBipContract) {
  GraphPayloadEncoder::Encoded encoded = BipGraphPayloadEncoder().Encode(
      *GoldenSnapshot(), BrowserFrameId(), kMaxBipGraphPayloadBytes,
      kMaxBipGraphPayloadBytes);
  EXPECT_EQ(encoded.encoding, GraphPayloadEncoding::kBipContract);
  EXPECT_FALSE(encoded.bytes.empty());
  EXPECT_FALSE(encoded.rejected);
}

TEST(BipGraphPayloadTest, TheMetadataOnlyEncoderStillReportsNone) {
  // The distinction the encoding field exists for: no encoder installed is not
  // the same statement as an empty graph.
  GraphPayloadEncoder::Encoded encoded =
      MetadataOnlyGraphPayloadEncoder().Encode(
          *GoldenSnapshot(), BrowserFrameId(), kMaxBipGraphPayloadBytes,
          kMaxBipGraphPayloadBytes);
  EXPECT_EQ(encoded.encoding, GraphPayloadEncoding::kNone);
  EXPECT_TRUE(encoded.bytes.empty());
  EXPECT_FALSE(encoded.rejected);
}

TEST(BipGraphPayloadTest, TrustAndSignalsSurviveAnIndependentReader) {
  auto snapshot = GoldenSnapshot();
  ASSERT_EQ(2u, snapshot->nodes.size());
  snapshot->nodes[0]->content_trust =
      mojom::ContentTrust::kUserGeneratedContent;
  snapshot->nodes[0]->content_signals = {
      mojom::ContentSignal::kHiddenByStyle,
      mojom::ContentSignal::kImperativeInstructionShape,
  };
  ASSERT_EQ(1u, snapshot->nodes[0]->text_runs.size());
  snapshot->nodes[0]->text_runs[0]->content_trust =
      mojom::ContentTrust::kThirdPartyEmbedded;
  snapshot->nodes[0]->text_runs[0]->content_signals = {
      mojom::ContentSignal::kZeroWidthCharacters,
      mojom::ContentSignal::kLanguageMismatch,
  };
  auto unlabeled = mojom::TextRun::New();
  unlabeled->text = "Unlabelled page text";
  unlabeled->source_kind = mojom::SourceKind::kDom;
  unlabeled->sensitivity = mojom::Sensitivity::kNotSensitive;
  snapshot->nodes[0]->text_runs.push_back(std::move(unlabeled));

  const std::vector<uint8_t> bytes =
      EncodeGraphPayload(*snapshot, BrowserFrameId());
  ASSERT_FALSE(bytes.empty());
  const std::optional<std::vector<test::GraphPayloadNode>> decoded =
      test::ReadGraphPayloadNodes(bytes);
  ASSERT_TRUE(decoded.has_value());
  ASSERT_EQ(2u, decoded->size());

  const test::GraphPayloadNode& node = decoded->at(0);
  EXPECT_EQ(static_cast<uint8_t>(mojom::ContentTrust::kUserGeneratedContent),
            node.content_trust);
  EXPECT_EQ((std::vector<uint8_t>{
                static_cast<uint8_t>(mojom::ContentSignal::kHiddenByStyle),
                static_cast<uint8_t>(
                    mojom::ContentSignal::kImperativeInstructionShape),
            }),
            node.content_signals);
  ASSERT_EQ(2u, node.text_runs.size());
  EXPECT_EQ(static_cast<uint8_t>(mojom::ContentTrust::kThirdPartyEmbedded),
            node.text_runs[0].content_trust);
  EXPECT_EQ(
      (std::vector<uint8_t>{
          static_cast<uint8_t>(mojom::ContentSignal::kZeroWidthCharacters),
          static_cast<uint8_t>(mojom::ContentSignal::kLanguageMismatch),
      }),
      node.text_runs[0].content_signals);

  // Optional trust is not a permissive default. Every absent node/run label
  // is materialized as UNKNOWN_UNTRUSTED.
  EXPECT_EQ(static_cast<uint8_t>(mojom::ContentTrust::kUnknownUntrusted),
            node.text_runs[1].content_trust);
  EXPECT_EQ(static_cast<uint8_t>(mojom::ContentTrust::kUnknownUntrusted),
            decoded->at(1).content_trust);
}

TEST(BipGraphPayloadTest, RendererCannotMintPrivilegedAuthorship) {
  constexpr mojom::ContentTrust kPrivileged[] = {
      mojom::ContentTrust::kUserAuthored,
      mojom::ContentTrust::kTaffyAuthored,
      mojom::ContentTrust::kModelAuthored,
  };
  for (mojom::ContentTrust trust : kPrivileged) {
    auto snapshot = GoldenSnapshot();
    snapshot->nodes[0]->content_trust = trust;
    GraphPayloadEncoder::Encoded encoded = BipGraphPayloadEncoder().Encode(
        *snapshot, BrowserFrameId(), kMaxBipGraphPayloadBytes,
        kMaxBipGraphPayloadBytes);
    EXPECT_TRUE(encoded.rejected);
    EXPECT_EQ(encoded.encoding, GraphPayloadEncoding::kNone);
    EXPECT_TRUE(encoded.bytes.empty());

    // A withheld run is still untrusted input. It cannot hide an authority
    // forgery behind the fact that its text will not be emitted.
    snapshot = GoldenSnapshot();
    snapshot->nodes[0]->sensitivity = mojom::Sensitivity::kCredential;
    snapshot->nodes[0]->text_runs[0]->content_trust = trust;
    encoded = BipGraphPayloadEncoder().Encode(*snapshot, BrowserFrameId(),
                                              kMaxBipGraphPayloadBytes,
                                              kMaxBipGraphPayloadBytes);
    EXPECT_TRUE(encoded.rejected);
    EXPECT_TRUE(encoded.bytes.empty());
  }
}

TEST(BipGraphPayloadTest, SignalsMustBeABoundedCanonicalSet) {
  auto rejects = [](std::vector<mojom::ContentSignal> signals) {
    auto snapshot = GoldenSnapshot();
    snapshot->nodes[0]->content_signals = std::move(signals);
    return BipGraphPayloadEncoder()
        .Encode(*snapshot, BrowserFrameId(), kMaxBipGraphPayloadBytes,
                kMaxBipGraphPayloadBytes)
        .rejected;
  };

  EXPECT_TRUE(rejects({mojom::ContentSignal::kHiddenByStyle,
                       mojom::ContentSignal::kHiddenByStyle}));
  EXPECT_TRUE(rejects({mojom::ContentSignal::kLanguageMismatch,
                       mojom::ContentSignal::kZeroWidthCharacters}));
  EXPECT_TRUE(rejects({static_cast<mojom::ContentSignal>(255)}));
  EXPECT_TRUE(rejects({
      mojom::ContentSignal::kHiddenByStyle,
      mojom::ContentSignal::kZeroWidthCharacters,
      mojom::ContentSignal::kBidiControlCharacters,
      mojom::ContentSignal::kEncodedBlob,
      mojom::ContentSignal::kImperativeInstructionShape,
      mojom::ContentSignal::kLanguageMismatch,
      mojom::ContentSignal::kCrossOriginFrameAuthored,
      static_cast<mojom::ContentSignal>(7),
      static_cast<mojom::ContentSignal>(8),
  }));
}

TEST(BipGraphPayloadTest, ClampedByteBudgetIsAnEncodingCeiling) {
  const std::vector<uint8_t> complete =
      EncodeGraphPayload(*GoldenSnapshot(), BrowserFrameId());
  ASSERT_GT(complete.size(), 1u);

  GraphPayloadEncoder::Encoded exact = BipGraphPayloadEncoder().Encode(
      *GoldenSnapshot(), BrowserFrameId(), complete.size(),
      kMaxBipGraphPayloadBytes);
  EXPECT_FALSE(exact.rejected);
  EXPECT_EQ(complete, exact.bytes);

  GraphPayloadEncoder::Encoded too_small = BipGraphPayloadEncoder().Encode(
      *GoldenSnapshot(), BrowserFrameId(), complete.size() - 1u,
      kMaxBipGraphPayloadBytes);
  EXPECT_TRUE(too_small.rejected);
  EXPECT_EQ(too_small.encoding, GraphPayloadEncoding::kNone);
  EXPECT_TRUE(too_small.bytes.empty());
}

TEST(BipGraphPayloadTest, ClampedTextBudgetChargesAllOriginalNodeText) {
  auto snapshot = GoldenSnapshot();
  snapshot->nodes[0]->description = "A browser-visible description";
  size_t original_text_bytes = 0;
  for (const auto& node : snapshot->nodes) {
    if (node->name) {
      original_text_bytes += node->name->size();
    }
    if (node->description) {
      original_text_bytes += node->description->size();
    }
    for (const auto& run : node->text_runs) {
      original_text_bytes += run->text.size();
    }
  }
  ASSERT_GT(original_text_bytes, 0u);

  GraphPayloadEncoder::Encoded exact = BipGraphPayloadEncoder().Encode(
      *snapshot, BrowserFrameId(), kMaxBipGraphPayloadBytes,
      original_text_bytes);
  EXPECT_FALSE(exact.rejected);
  EXPECT_FALSE(exact.bytes.empty());

  GraphPayloadEncoder::Encoded too_small = BipGraphPayloadEncoder().Encode(
      *snapshot, BrowserFrameId(), kMaxBipGraphPayloadBytes,
      original_text_bytes - 1u);
  EXPECT_TRUE(too_small.rejected);
  EXPECT_TRUE(too_small.bytes.empty());
}

TEST(BipGraphPayloadTest, WithheldDeltaTextStillConsumesTheMessageBudget) {
  auto delta = mojom::PageDelta::New();
  delta->schema_version = "0.6";
  delta->subscription_id = "sub-1";
  delta->frame_id = "frame-1";
  delta->page_epoch = "epoch-1";

  auto added = mojom::SemanticNode::New();
  added->node_id = "n3";
  added->frame_id = "frame-1";
  added->role = mojom::SemanticRole::kParagraph;
  added->sensitivity = mojom::Sensitivity::kCredential;
  auto withheld = mojom::TextRun::New();
  withheld->text = std::string(512u, 'x');
  withheld->source_kind = mojom::SourceKind::kDom;
  withheld->sensitivity = mojom::Sensitivity::kCredential;
  added->text_runs.push_back(std::move(withheld));
  delta->added_nodes.push_back(std::move(added));

  const std::vector<uint8_t> complete =
      EncodeDeltaPayload(*delta, BrowserFrameId());
  ASSERT_FALSE(complete.empty());
  ASSERT_LT(complete.size(), delta->added_nodes[0]->text_runs[0]->text.size());
  EXPECT_TRUE(EncodeDeltaPayload(*delta, BrowserFrameId(), nullptr,
                                 complete.size())
                  .empty());
}

TEST(BipGraphPayloadTest, ACredentialNodeCrossesWithoutItsName) {
  // The security property of this framing, asserted from the sending side. The
  // node's name is in the snapshot and must not be in the bytes.
  std::vector<uint8_t> bytes =
      EncodeGraphPayload(*GoldenSnapshot(), BrowserFrameId());
  ASSERT_FALSE(bytes.empty());

  const std::string secret = "Password";
  auto found =
      std::search(bytes.begin(), bytes.end(), secret.begin(), secret.end());
  EXPECT_EQ(found, bytes.end())
      << "a credential node's name reached the core graph payload";

  // The insensitive one does cross, so the test above is proving withholding
  // rather than proving the encoder writes no names at all.
  const std::string public_name = "Sign in";
  EXPECT_NE(std::search(bytes.begin(), bytes.end(), public_name.begin(),
                        public_name.end()),
            bytes.end());
}

TEST(BipGraphPayloadTest, ExactObservedLinkDestinationNeverCrossesToRust) {
  constexpr std::string_view kDestination =
      "https://private-destination.test/private-path?q=private-query-canary"
      "#private-fragment";
  auto snapshot = GoldenSnapshot();
  mojom::SemanticNode& link = *snapshot->nodes[0];
  link.role = mojom::SemanticRole::kLink;
  link.actions.push_back(mojom::ActionType::kActivate);
  link.destination = mojom::Destination::New();
  link.destination->url_metadata = mojom::UrlMetadata::New();
  link.destination->url_metadata->origin = mojom::Origin::New();
  link.destination->url_metadata->origin->kind = mojom::OriginKind::kTuple;
  link.destination->url_metadata->origin->serialization =
      "https://private-destination.test";
  link.destination->url_metadata->disclosure = mojom::UrlDisclosure::kFullUrl;
  link.destination->url_metadata->path = "/private-path";
  link.destination->url_metadata->url = std::string(kDestination);
  link.destination->url_metadata->has_query = true;
  link.destination->url_metadata->has_fragment = true;

  const std::vector<uint8_t> bytes =
      EncodeGraphPayload(*snapshot, BrowserFrameId());
  ASSERT_FALSE(bytes.empty());
  for (std::string_view private_part :
       {kDestination, std::string_view("private-destination.test"),
        std::string_view("private-path"),
        std::string_view("private-query-canary"),
        std::string_view("private-fragment")}) {
    EXPECT_EQ(std::search(bytes.begin(), bytes.end(), private_part.begin(),
                         private_part.end()),
              bytes.end());
  }
}

TEST(BipGraphPayloadTest, InspectorProjectionUsesFreshBoundedIdentities) {
  GraphPayloadEncoder::Encoded encoded = BipGraphPayloadEncoder().Encode(
      *GoldenSnapshot(), BrowserFrameId(), kMaxBipGraphPayloadBytes,
      kMaxBipGraphPayloadBytes);
  ASSERT_TRUE(encoded.inspector_projection);
  ASSERT_EQ(2u, encoded.inspector_projection->nodes.size());
  ASSERT_EQ(1u, encoded.inspector_projection->edges.size());

  const InspectorNodeProjection& public_node =
      encoded.inspector_projection->nodes[0];
  EXPECT_EQ("item-1", public_node.display_id);
  ASSERT_TRUE(public_node.name);
  EXPECT_EQ("Sign in", *public_node.name);
  EXPECT_EQ(InspectorSensitivity::kPublic, public_node.sensitivity);

  const InspectorNodeProjection& withheld_node =
      encoded.inspector_projection->nodes[1];
  EXPECT_EQ("item-2", withheld_node.display_id);
  EXPECT_FALSE(withheld_node.name);
  EXPECT_EQ(InspectorSensitivity::kWithheld, withheld_node.sensitivity);
  EXPECT_TRUE(withheld_node.value_withheld);

  const InspectorEdgeProjection& edge = encoded.inspector_projection->edges[0];
  EXPECT_EQ("item-1", edge.from_display_id);
  EXPECT_EQ("item-2", edge.to_display_id);
  EXPECT_EQ(InspectorRelationship::kLabel, edge.relationship);
  EXPECT_NE("n1", public_node.display_id);
  EXPECT_NE("n2", withheld_node.display_id);
}

TEST(BipGraphPayloadTest, ASecretInAnInsensitiveNameIsRescannedOut) {
  // Adversary A2. The renderer labelled this node kNotSensitive, so the
  // withholding rule above lets its name cross; the name is a credential. A
  // label written inside the sandbox cannot be the only thing standing between
  // a compromised renderer and the projection, so the browser rescans every
  // name it does let through.
  ASSERT_GT(GetSeededSecretCount(), 0u);
  const std::string token(GetSeededSecret(0).token);

  auto snapshot = GoldenSnapshot();
  ASSERT_FALSE(snapshot->nodes.empty());
  snapshot->nodes[0]->sensitivity = mojom::Sensitivity::kNotSensitive;
  snapshot->nodes[0]->name = token;

  RescanTally tally;
  std::vector<uint8_t> bytes =
      EncodeGraphPayload(*snapshot, BrowserFrameId(), &tally);
  ASSERT_FALSE(bytes.empty());

  EXPECT_EQ(std::search(bytes.begin(), bytes.end(), token.begin(), token.end()),
            bytes.end())
      << "a secret a renderer volunteered in an insensitive node's name "
         "reached the core graph payload";
  EXPECT_GT(tally.canary_hit_count, 0u)
      << "the secret was removed without being reported, so a renderer that "
         "stopped redacting would be indistinguishable from one that had "
         "nothing to redact";
}

TEST(BipGraphPayloadTest, TheDeltaPathRescansAnAddedNodeToo) {
  // The delta is a separate code path with the same obligation, and it shares
  // WriteNode precisely so the obligation cannot be met on one and forgotten
  // on the other. Asserted rather than assumed.
  ASSERT_GT(GetSeededSecretCount(), 0u);
  const std::string token(GetSeededSecret(0).token);

  auto delta = mojom::PageDelta::New();
  delta->schema_version = "0.6";
  delta->subscription_id = "sub-1";
  delta->frame_id = "frame-1";
  delta->page_epoch = "epoch-1";

  auto added = mojom::SemanticNode::New();
  added->node_id = "n3";
  added->frame_id = "frame-1";
  added->role = mojom::SemanticRole::kParagraph;
  added->name = token;
  added->sensitivity = mojom::Sensitivity::kNotSensitive;
  delta->added_nodes.push_back(std::move(added));

  RescanTally tally;
  std::vector<uint8_t> bytes =
      EncodeDeltaPayload(*delta, BrowserFrameId(), &tally);
  ASSERT_FALSE(bytes.empty());

  EXPECT_EQ(std::search(bytes.begin(), bytes.end(), token.begin(), token.end()),
            bytes.end())
      << "a secret reached the core service through a delta's added node";
  EXPECT_GT(tally.canary_hit_count, 0u);
}

TEST(BipGraphPayloadTest, AnHonestNameIsNotDisturbedByTheRescan) {
  // The rescan has to be invisible on an ordinary page, or the golden vector
  // and every consumer of a name would be paying for the defence.
  RescanTally tally;
  std::vector<uint8_t> bytes =
      EncodeGraphPayload(*GoldenSnapshot(), BrowserFrameId(), &tally);

  EXPECT_EQ(bytes, EncodeGraphPayload(*GoldenSnapshot(), BrowserFrameId()));
  EXPECT_EQ(RescanTally(), tally);
  const std::string public_name = "Sign in";
  EXPECT_NE(std::search(bytes.begin(), bytes.end(), public_name.begin(),
                        public_name.end()),
            bytes.end());
}

TEST(BipGraphPayloadTest, TheBrowsersFrameIdentityReplacesTheRenderers) {
  // The defect this replaced: the renderer stamps a value from its own
  // per-process namespace on every node, the browser wrote it through
  // unchanged, and the isolated core refuses a payload whose node rows do not
  // name the frame the envelope names. The two could never be equal, so every
  // real observation decoded as malformed and no node-targeted action was
  // reachable. Asserted on the bytes rather than on a call count, because what
  // matters is what the core reads.
  std::vector<uint8_t> bytes =
      EncodeGraphPayload(*GoldenSnapshot(), BrowserFrameId());
  ASSERT_FALSE(bytes.empty());

  const std::string renderer_frame = "frame-1";
  EXPECT_EQ(std::search(bytes.begin(), bytes.end(), renderer_frame.begin(),
                        renderer_frame.end()),
            bytes.end())
      << "a renderer-minted frame identity reached the core graph payload";

  const std::string browser_frame = BrowserFrameId().value;
  EXPECT_NE(std::search(bytes.begin(), bytes.end(), browser_frame.begin(),
                        browser_frame.end()),
            bytes.end())
      << "the browser's frame identity is not on the node rows, so the core "
         "has nothing to match the envelope against";
}

TEST(BipGraphPayloadTest, AnUnsetSensitivityWithholdsTheName) {
  // Fail-closed: kUnknownSensitive is not kNotSensitive, so the name stays.
  auto snapshot = GoldenSnapshot();
  ASSERT_FALSE(snapshot->nodes.empty());
  snapshot->nodes[0]->sensitivity = mojom::Sensitivity::kUnknownSensitive;

  std::vector<uint8_t> bytes = EncodeGraphPayload(*snapshot, BrowserFrameId());
  ASSERT_FALSE(bytes.empty());
  const std::string name = "Sign in";
  EXPECT_EQ(std::search(bytes.begin(), bytes.end(), name.begin(), name.end()),
            bytes.end());
}

}  // namespace
}  // namespace taffy
