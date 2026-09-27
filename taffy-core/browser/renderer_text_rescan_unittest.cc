// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/renderer_text_rescan.h"

#include <string>

#include "taffy/browser/seeded_secret_corpus.h"
#include "testing/gtest/include/gtest/gtest.h"

// The browser process's second redaction layer, asserted on its own.
//
// The layer's reason for existing is adversary A2: the renderer's redaction is
// inside the sandbox, so a compromised renderer can skip it and send a
// perfectly well-formed reply. What is asserted here is the property that
// makes that a bounded failure — a secret shape does not survive the pass —
// together with the two things that keep the pass usable: ordinary text comes
// back untouched, and what was removed is counted rather than quietly
// repaired.

namespace taffy {
namespace {

TEST(RendererTextRescanTest, OrdinaryTextIsReturnedUnchanged) {
  RescanTally tally;
  EXPECT_EQ("Sign in", RescanRendererText("Sign in", &tally));
  EXPECT_EQ("Continue to checkout",
            RescanRendererText("Continue to checkout", &tally));
  EXPECT_EQ(RescanTally(), tally)
      << "An honest page paid a redaction. A layer that removes something from "
         "ordinary text teaches people to ignore what it reports.";
}

TEST(RendererTextRescanTest, EmptyTextIsEmptyAndCostsNothing) {
  RescanTally tally;
  EXPECT_EQ("", RescanRendererText("", &tally));
  EXPECT_EQ(RescanTally(), tally);
}

TEST(RendererTextRescanTest, ANullTallyStillRedacts) {
  // The removal is not optional, and a caller with nowhere to report it must
  // not be able to opt out of it by passing nothing.
  ASSERT_GT(GetSeededSecretCount(), 0u);
  const std::string token(GetSeededSecret(0).token);

  const std::string out = RescanRendererText("Welcome, " + token, nullptr);
  EXPECT_EQ(std::string::npos, out.find(token));
}

TEST(RendererTextRescanTest, NoSeededCanarySurvivesInAnyPosition) {
  ASSERT_GT(GetSeededSecretCount(), 0u)
      << "A rescan suite with no canary to look for passes for the wrong "
         "reason.";

  for (size_t i = 0; i < GetSeededSecretCount(); ++i) {
    const SeededSecret& secret = GetSeededSecret(i);
    const std::string token(secret.token);

    for (const std::string& carrier :
         {token, "Welcome, " + token, token + " is your code",
          "prefix" + token + "suffix", "value=" + token}) {
      RescanTally tally;
      const std::string out = RescanRendererText(carrier, &tally);
      EXPECT_EQ(std::string::npos, out.find(token))
          << "canary " << token << " (from fixture " << secret.carried_by
          << ") survived the browser's rescan in carrier: " << carrier;
      EXPECT_GT(tally.canary_hit_count, 0u)
          << "canary " << token
          << " was removed without being counted, so a "
             "renderer that stopped redacting would look like a page with "
             "nothing to redact";
      EXPECT_TRUE(tally.anything_was_removed());
    }
  }
}

TEST(RendererTextRescanTest, AnUnlabelledCredentialShapeIsRemovedToo) {
  // The canary rule is a tripwire, not the mitigation. What defends a real
  // page is the shape rules, so the layer is asserted against a secret that
  // carries no fixture prefix at all.
  RescanTally tally;
  const std::string out = RescanRendererText(
      "Authorization: Bearer sk-abcdefghijklmnopqrstuvwx", &tally);

  EXPECT_EQ(std::string::npos, out.find("sk-abcdefghijklmnopqrstuvwx"))
      << "a bearer token reached the projection";
  EXPECT_NE(std::string::npos, out.find("Authorization"))
      << "the header name is not the secret, and a layer that removes the "
         "whole string removes the reason a consumer could read it";
  EXPECT_GE(tally.redacted_span_count, 1u);
  EXPECT_EQ(0u, tally.canary_hit_count)
      << "the tripwire fired on a secret that carries no fixture prefix, which "
         "would make every real credential look like a corpus canary";
}

TEST(RendererTextRescanTest, TheTallyAccumulatesAcrossStrings) {
  // One tally covers every string in one envelope, so a snapshot whose nodes
  // each carried a secret reports every one of them rather than the last.
  ASSERT_GE(GetSeededSecretCount(), 2u);
  RescanTally tally;
  RescanRendererText(std::string(GetSeededSecret(0).token), &tally);
  RescanRendererText(std::string(GetSeededSecret(1).token), &tally);

  EXPECT_EQ(2u, tally.canary_hit_count);
  EXPECT_GE(tally.redacted_span_count, 2u);
}

TEST(RendererTextRescanTest, ARepeatedRescanIsStable) {
  // A name can pass through more than one layer. The second pass must not
  // resurrect anything and must not keep chewing at the placeholder the first
  // one left.
  ASSERT_GT(GetSeededSecretCount(), 0u);
  const std::string token(GetSeededSecret(0).token);

  RescanTally first;
  const std::string once = RescanRendererText("Welcome, " + token, &first);
  RescanTally second;
  const std::string twice = RescanRendererText(once, &second);

  EXPECT_EQ(once, twice);
  EXPECT_EQ(std::string::npos, twice.find(token));
  EXPECT_EQ(0u, second.canary_hit_count)
      << "the placeholder tripped the canary tripwire, which would turn every "
         "clean second pass into a reported upstream failure";
}

}  // namespace
}  // namespace taffy
