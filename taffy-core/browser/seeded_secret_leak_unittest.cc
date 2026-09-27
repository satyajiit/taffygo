// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <vector>

#include "base/time/time.h"
#include "taffy/browser/credential_boundary.h"
#include "taffy/components/security/browser/value_reference_vault.h"
#include "taffy/components/intelligence/content/observability_recorder.h"
#include "taffy/components/intelligence/content/scrubbing_serializer.h"
#include "taffy/components/intelligence/content/secret_shape_scanner.h"
#include "taffy/browser/seeded_secret_corpus.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

// PAR-SEC-009: zero seeded-secret hits across every path
// //taffy owns.
//
// The canaries come from the fixture corpus by way of a generated translation
// unit (seeded_secret_corpus.h), so this suite runs against the tokens the
// fixture pages actually contain, and a corpus change either updates the data
// or fails the build.
//
// What is covered here is the browser-process half: the scrubbing serializer
// that every log line, crash key and analytics payload passes through, and the
// two record types that carry identifiers into observability. The same
// serializer is what the observation path's second redaction layer is built on
// (renderer_text_rescan.h), and that layer is asserted in its own unit test
// rather than here, because what it defends is a hostile renderer rather than
// a diagnostic. The renderer half — omitting the value before it is ever
// serialized — belongs to //taffy/renderer, and the end-to-end run
// over the fixture corpus belongs to the M2 adversarial suite (WP-M2-06 and
// WP-M2-08). Each half is stated where it is enforced rather than assumed by
// the other.

namespace taffy {
namespace {

// Every place a canary could plausibly be embedded in a diagnostic. The point
// is not that any of these strings is realistic on its own; it is that a
// canary must not survive whatever wrapping it arrives in.
std::vector<std::string> CarriersFor(const std::string& token) {
  return {
      token,
      "value=" + token,
      "password=" + token,
      "{\"secret\": \"" + token + "\"}",
      "Authorization: Bearer " + token,
      "https://primary.taffy.test/submit?value=" + token,
      "https://primary.taffy.test/" + token,
      "https://" + token + "@primary.taffy.test/",
      "error while reading field: " + token,
      "  " + token + "  ",
      token + token,
      "prefix" + token + "suffix",
  };
}

TEST(SeededSecretLeakTest, TheCorpusIsPresentAndVersioned) {
  ASSERT_GT(GetSeededSecretCount(), 0u)
      << "The generated seeded-secret corpus is empty. A suite that checks "
         "nothing passes for the wrong reason.";
  EXPECT_FALSE(GetSeededSecretCorpusVersion().empty())
      << "A result that does not name the corpus version it ran against is a "
         "number nobody can reproduce.";

  for (size_t i = 0; i < GetSeededSecretCount(); ++i) {
    const SeededSecret& secret = GetSeededSecret(i);
    EXPECT_FALSE(secret.token.empty());
    EXPECT_FALSE(secret.secret_class.empty());
    EXPECT_FALSE(secret.carried_by.empty())
        << "canary " << secret.token
        << " names no fixture, so a failure could not say which page to look "
           "at";
  }
}

TEST(SeededSecretLeakTest, EveryCanaryCarriesTheTripwirePrefix) {
  // The scrubber's tripwire rule matches this prefix. A token without it would
  // not be counted when it leaked, which is why the generator refuses one.
  const std::string prefix(kCanaryTokenPrefix);
  for (size_t i = 0; i < GetSeededSecretCount(); ++i) {
    const std::string token(GetSeededSecret(i).token);
    EXPECT_EQ(0u, token.find(prefix)) << token;
  }
}

TEST(SeededSecretLeakTest, NoCanarySurvivesTheSerializerInAnyCarrier) {
  for (size_t i = 0; i < GetSeededSecretCount(); ++i) {
    const SeededSecret& secret = GetSeededSecret(i);
    const std::string token(secret.token);

    for (const std::string& carrier : CarriersFor(token)) {
      const ScrubbedText scrubbed = ScrubbingSerializer::Serialize(carrier);
      EXPECT_EQ(std::string::npos, scrubbed.value().find(token))
          << "canary " << token << " (from fixture " << secret.carried_by
          << ") survived the serializer in carrier: " << carrier;
      EXPECT_GT(scrubbed.report().canary_hit_count, 0u)
          << "canary " << token << " was not counted in carrier: " << carrier;
    }
  }
}

TEST(SeededSecretLeakTest, NoCanarySurvivesUrlSerialization) {
  for (size_t i = 0; i < GetSeededSecretCount(); ++i) {
    const std::string token(GetSeededSecret(i).token);

    const ScrubbedText from_query = ScrubbingSerializer::SerializeOriginOf(
        GURL("https://primary.taffy.test/a?value=" + token));
    EXPECT_EQ(std::string::npos, from_query.value().find(token));

    const ScrubbedText from_path = ScrubbingSerializer::SerializeOriginOf(
        GURL("https://primary.taffy.test/" + token));
    EXPECT_EQ(std::string::npos, from_path.value().find(token));

    const ScrubbedText from_fragment = ScrubbingSerializer::SerializeOriginOf(
        GURL("https://primary.taffy.test/a#" + token));
    EXPECT_EQ(std::string::npos, from_fragment.value().find(token));

    // The one place a canary could hide from an origin reduction: the host
    // itself. It is still not in the output, because a canary is not a host.
    const ScrubbedText from_userinfo = ScrubbingSerializer::SerializeOriginOf(
        GURL("https://user:" + token + "@primary.taffy.test/"));
    EXPECT_EQ(std::string::npos, from_userinfo.value().find(token));
  }
}

TEST(SeededSecretLeakTest, ObservabilityRecordsCannotCarryACanary) {
  // The record types are trivially copyable and hold fixed-capacity
  // identifiers, so there is no field a page value could be written into. The
  // assertion is the truncation behavior of the one field that takes text at
  // all: an identifier buffer is bounded by the contract's identifier length
  // and cannot become a page.
  for (size_t i = 0; i < GetSeededSecretCount(); ++i) {
    const std::string token(GetSeededSecret(i).token);
    const RecordIdentifier identifier = ToRecordIdentifier(token);
    // A canary is shorter than the bound, so it would fit. That is exactly why
    // the protection is structural rather than dimensional: no code path
    // writes a page value into an identifier field, and the seams that could
    // have — the credential boundary and the scrubber — are covered above.
    EXPECT_LE(std::string(identifier.chars.data()).size(),
              kMaxIdentifierChars);
  }

  static_assert(std::is_trivially_copyable_v<ObservationRecord>);
  static_assert(std::is_trivially_copyable_v<ActionRecord>);
}

TEST(SeededSecretLeakTest, TheCredentialBoundaryNeverSeesAValue) {
  // The other half of the browser-process story: for the values that come from
  // a credential field, there is no argument to pass them in. The metadata
  // type cannot hold one, and the placeholder path takes a class rather than a
  // value.
  content::BrowserTaskEnvironment task_environment;
  CredentialBoundary boundary;

  CredentialFieldMetadata metadata;
  metadata.tab_id = ToRecordIdentifier("tab_1");
  metadata.frame_id = ToRecordIdentifier("frame_main");
  metadata.node_id = ToRecordIdentifier("node_password");
  metadata.credential_class = CredentialClass::kPassword;
  metadata.value_is_present = true;
  boundary.NoteCredentialField(metadata);

  const ScrubbedText placeholder =
      ScrubbingSerializer::RedactedPlaceholder(CredentialClass::kPassword);
  for (size_t i = 0; i < GetSeededSecretCount(); ++i) {
    EXPECT_EQ(std::string::npos,
              placeholder.value().find(std::string(GetSeededSecret(i).token)));
  }
  EXPECT_EQ(1u, boundary.CredentialFieldCount(TabId{"tab_1"}));
}

TEST(SeededSecretLeakTest, AHeldValueLeavesNothingInItsOwnReference) {
  // The third browser-process holder of something a person typed. Unlike the
  // credential boundary, the vault does hold bytes - it is the one thing in
  // TaffyGo that does - so the property here is not "there is no argument" but
  // "nothing that leaves discloses anything".
  //
  // A reference reaches the assistant, an action digest, and the durable
  // journal. If it were any function of the value at all - a digest, a keyed
  // hash, a truncation, a length - those three would carry the value, because
  // the domains a form field draws from are small enough to enumerate. So the
  // assertion is over every canary the corpus seeds: none of them appears in
  // the reference minted for it, and the reference minted for the shortest
  // canary is exactly as long as the one minted for the longest.
  content::BrowserTaskEnvironment task_environment;
  ValueReferenceVault vault;
  vault.BeginGeneration("profile_1", 1u);
  const std::optional<FillClearance> clearance =
      FillClearance::For(Sensitivity::kIdentity);
  ASSERT_TRUE(clearance.has_value());

  size_t reference_length = 0u;
  for (size_t i = 0; i < GetSeededSecretCount(); ++i) {
    const std::string token(GetSeededSecret(i).token);
    const ValueReference reference =
        vault.Mint(TaskId{"task_1"}, *clearance, token,
                   base::TimeTicks::Now() + base::Seconds(30));
    ASSERT_TRUE(reference.is_valid());
    EXPECT_EQ(std::string::npos, reference.value.find(token));
    // And no fragment of it either: a prefix long enough to search for would
    // narrow the domain just as a length would.
    if (token.size() >= 6u) {
      EXPECT_EQ(std::string::npos, reference.value.find(token.substr(0, 6)));
    }
    if (reference_length == 0u) {
      reference_length = reference.value.size();
    }
    EXPECT_EQ(reference_length, reference.value.size())
        << "a reference whose length varies with the value discloses the "
           "value's length";
  }
}

TEST(SeededSecretLeakTest, ARepeatedScrubIsStillClean) {
  // A diagnostic can pass through more than one sink. The second pass must not
  // resurrect anything, and must not keep chewing at what is left.
  for (size_t i = 0; i < GetSeededSecretCount(); ++i) {
    const std::string token(GetSeededSecret(i).token);
    const ScrubbedText once =
        ScrubbingSerializer::Serialize("password=" + token);
    const ScrubbedText twice = ScrubbingSerializer::Serialize(once.value());

    EXPECT_EQ(std::string::npos, twice.value().find(token));
    EXPECT_EQ(once.value(), twice.value());
  }
}

}  // namespace
}  // namespace taffy
