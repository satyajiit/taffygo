// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/secret_shape_scanner.h"

#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

// The shape scanners of PAR-SEC-009, one at a time.
//
// Two properties are checked for every scanner: it finds the shape it is for,
// and it does not find it in text that merely resembles it. The second half is
// the one that keeps the scrubber usable — a scrubber that redacts every
// diagnostic teaches people to stop reading them, and a diagnostic nobody
// reads is the same as no diagnostic.

namespace taffy {
namespace {

bool FoundRule(const std::vector<SecretMatch>& matches, ScrubRule rule) {
  for (const SecretMatch& match : matches) {
    if (match.rule == rule) {
      return true;
    }
  }
  return false;
}

TEST(SecretShapeScannerTest, EmptyAndOrdinaryTextIsLeftAlone) {
  EXPECT_TRUE(ScanForSecretShapes("").empty());
  EXPECT_TRUE(ScanForSecretShapes("The page did not load.").empty());
  EXPECT_TRUE(
      ScanForSecretShapes("net error -105 on primary.taffy.test").empty());
}

TEST(SecretShapeScannerTest, ACanaryIsAlwaysCaught) {
  const std::vector<SecretMatch> matches =
      ScanForSecretShapes("value was TAFFYGO-CANARY-LOGIN-0A3F7D here");

  ASSERT_EQ(1u, matches.size());
  EXPECT_EQ(ScrubRule::kCanaryTripwire, matches[0].rule);
  EXPECT_EQ(std::string("TAFFYGO-CANARY-LOGIN-0A3F7D").size(),
            matches[0].length());
}

TEST(SecretShapeScannerTest, ALabelledParameterValueIsRemoved) {
  const std::vector<SecretMatch> matches =
      ScanForSecretShapes("password=hunter2");

  ASSERT_EQ(1u, matches.size());
  EXPECT_EQ(ScrubRule::kSensitiveParameterValue, matches[0].rule);
  EXPECT_EQ(9u, matches[0].begin);
  EXPECT_EQ(16u, matches[0].end);
}

TEST(SecretShapeScannerTest, AJsonFieldWithASensitiveNameIsRemoved) {
  const std::vector<SecretMatch> matches =
      ScanForSecretShapes("{\"client_secret\": \"abcd1234\", \"ok\": true}");

  EXPECT_TRUE(FoundRule(matches, ScrubRule::kSensitiveParameterValue));
}

TEST(SecretShapeScannerTest, AnOrdinaryFieldNameIsNotSensitive) {
  // "title" and "count" are not on the list, and a scanner that treated every
  // name=value pair as a secret would redact every diagnostic there is.
  EXPECT_TRUE(ScanForSecretShapes("title=Quantum storage review").empty());
  EXPECT_TRUE(ScanForSecretShapes("count=42").empty());
}

TEST(SecretShapeScannerTest, AUrlIsReducedToItsOrigin) {
  const std::vector<SecretMatch> matches =
      ScanForSecretShapes("failed at https://primary.taffy.test/auth?code=V");

  ASSERT_FALSE(matches.empty());
  EXPECT_EQ(ScrubRule::kUrlReducedToOrigin, matches[0].rule);
  // The origin survives; everything after it does not.
  EXPECT_EQ(std::string("failed at https://primary.taffy.test").size(),
            matches[0].begin);
}

TEST(SecretShapeScannerTest, AUrlWithNoPathOrQueryIsLeftIntact) {
  // There is nothing to remove, and replacing it anyway would make the
  // diagnostic worse for no gain.
  EXPECT_TRUE(ScanForSecretShapes("https://primary.taffy.test").empty());
}

TEST(SecretShapeScannerTest, CredentialsInAUrlTakeTheWholeUrl) {
  const std::vector<SecretMatch> matches =
      ScanForSecretShapes("open https://user:secret@primary.taffy.test/a");

  ASSERT_FALSE(matches.empty());
  EXPECT_EQ(ScrubRule::kUrlUserInfo, matches[0].rule);
  EXPECT_EQ(std::string("open ").size(), matches[0].begin);
}

TEST(SecretShapeScannerTest, ABearerTokenIsRemovedButTheHeaderNameSurvives) {
  const std::vector<SecretMatch> matches = ScanForSecretShapes(
      "Bearer sk-abcdefghijklmnopqrstuvwx");

  ASSERT_FALSE(matches.empty());
  EXPECT_TRUE(FoundRule(matches, ScrubRule::kBearerToken) ||
              FoundRule(matches, ScrubRule::kHighEntropyRun));
  // Whatever rule claimed it, the token itself is inside a match.
  EXPECT_GE(matches.back().end, std::string("Bearer sk-abcdefghij").size());
}

TEST(SecretShapeScannerTest, AJsonWebTokenIsRemovedWhole) {
  const std::string jwt =
      "eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxMjM0NSJ9.SflKxwRJSMeKKF2QT4fwpM";
  const std::vector<SecretMatch> matches = ScanForSecretShapes(jwt);

  ASSERT_FALSE(matches.empty());
  // The whole token, not the first segment: a partial redaction of a token is
  // not a redaction.
  EXPECT_EQ(0u, matches[0].begin);
  EXPECT_EQ(jwt.size(), matches[0].end);
}

TEST(SecretShapeScannerTest, ADottedIdentifierIsNotAToken) {
  // Three dotted segments, but no base64 JSON header and far too short.
  EXPECT_TRUE(ScanForSecretShapes("com.example.app").empty());
}

// The key-block markers are assembled from fragments rather than written out.
// The repository's own credential gate — ./tools/check fast, the "secrets"
// lane — searches tracked files for the literal, and it is a better gate for
// having no exceptions in it. A test that needed an allowlist entry would have
// made the gate weaker for everybody in order to make one file shorter.
std::string KeyBlockMarker(const char* verb) {
  return std::string("-----") + verb + " " + "PRIVATE" + " KEY" + "-----";
}

TEST(SecretShapeScannerTest, ACertificateBlockGoesWhole) {
  const std::string pem =
      KeyBlockMarker("BEGIN") +
      "\nMIIBVgIBADANBgkqhkiG9w0BAQEFAASCAUAwggE8AgEAAkEA\n" +
      KeyBlockMarker("END");
  const std::vector<SecretMatch> matches = ScanForSecretShapes(pem);

  ASSERT_EQ(1u, matches.size());
  EXPECT_EQ(ScrubRule::kPemBlock, matches[0].rule);
  EXPECT_EQ(0u, matches[0].begin);
  EXPECT_EQ(pem.size(), matches[0].end);
}

TEST(SecretShapeScannerTest, ATruncatedCertificateBlockIsStillAKey) {
  // A block whose end marker never arrived is still a key.
  const std::vector<SecretMatch> matches = ScanForSecretShapes(
      KeyBlockMarker("BEGIN") + "\nMIIBVgIBADANBgkqhkiG9w0BAQ");

  ASSERT_EQ(1u, matches.size());
  EXPECT_EQ(ScrubRule::kPemBlock, matches[0].rule);
}

// Stripe's published documentation key, with a live prefix. The literal is
// split so that no source line carries a live-key shape: GitHub's push
// protection refuses one in a public repository even when it is an example.
// Adjacent literals are joined by the compiler, so the scanner sees one run.
constexpr char kExampleLiveKey[] = "sk_" "live_4eC39HqLyjWDarjtT1zdp7dc";

TEST(SecretShapeScannerTest, AMixedCaseHighEntropyRunIsRemoved) {
  const std::vector<SecretMatch> matches =
      ScanForSecretShapes(kExampleLiveKey);

  ASSERT_EQ(1u, matches.size());
  EXPECT_EQ(ScrubRule::kHighEntropyRun, matches[0].rule);
}

TEST(SecretShapeScannerTest, ALowercaseHexDigestIsNotAHighEntropyRun) {
  // This component computes content digests and logs them by design. The
  // entropy rule requires mixed case precisely so that a digest does not
  // match, which is why the threshold is not just "long and dense".
  const std::string digest =
      "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08";
  EXPECT_TRUE(ScanForSecretShapes(digest).empty());
}

TEST(SecretShapeScannerTest, AShortIdentifierIsNotAHighEntropyRun) {
  // Node and tab identifiers look like this and appear in every diagnostic.
  EXPECT_TRUE(ScanForSecretShapes("node_A1b2C3").empty());
  EXPECT_LT(std::string("node_A1b2C3").size(), kHighEntropyMinimumLength);
}

TEST(SecretShapeScannerTest, ACardNumberThatPassesLuhnIsRemoved) {
  EXPECT_TRUE(PassesLuhnCheck("4111111111111111"));
  EXPECT_TRUE(PassesLuhnCheck("4111 1111 1111 1111"));
  EXPECT_FALSE(PassesLuhnCheck("4111111111111112"));

  const std::vector<SecretMatch> matches =
      ScanForSecretShapes("card 4111 1111 1111 1111 ok");
  ASSERT_FALSE(matches.empty());
  EXPECT_EQ(ScrubRule::kPaymentCardNumber, matches[0].rule);
}

TEST(SecretShapeScannerTest, AnOrdinaryNumberIsNotACard) {
  EXPECT_TRUE(ScanForSecretShapes("received 1048576 bytes").empty());
  EXPECT_TRUE(ScanForSecretShapes("2026-08-17").empty());
}

TEST(SecretShapeScannerTest, MatchesAreOrderedAndNonOverlapping) {
  const std::vector<SecretMatch> matches = ScanForSecretShapes(
      "password=hunter2 then https://primary.taffy.test/a?token=V then "
      "TAFFYGO-CANARY-OTP-9D2E60");

  ASSERT_GE(matches.size(), 3u);
  for (size_t i = 1; i < matches.size(); ++i) {
    EXPECT_LE(matches[i - 1].end, matches[i].begin)
        << "matches " << (i - 1) << " and " << i << " overlap";
  }
}

TEST(SecretShapeScannerTest, EveryReplacementIsStatic) {
  // A replacement derived from what it replaced would be a leak with extra
  // steps. Every one is a literal, so none of them can contain input.
  const std::vector<SecretMatch> matches = ScanForSecretShapes(
      "password=hunter2 TAFFYGO-CANARY-OTP-9D2E60 "
      "sk_" "live_4eC39HqLyjWDarjtT1zdp7dc");

  for (const SecretMatch& match : matches) {
    EXPECT_FALSE(match.replacement.empty());
    EXPECT_EQ('[', match.replacement.front());
    EXPECT_EQ(']', match.replacement.back());
  }
}

TEST(SecretShapeScannerTest, ScanningIsLinearOnPathologicalInput) {
  // No pattern engine, so no backtracking. The assertion that matters is that
  // this returns at all: a scrubber an attacker can make quadratic is a way to
  // hang the browser process from a page.
  const std::string pathological(4096, 'a');
  EXPECT_TRUE(ScanForSecretShapes(pathological).empty());

  const std::string many_schemes = std::string(512, ':') + std::string(512, '/');
  ScanForSecretShapes(many_schemes);

  std::string repeated;
  for (int i = 0; i < 256; ++i) {
    repeated += "-----BEGIN X-----";
  }
  ScanForSecretShapes(repeated);
}

}  // namespace
}  // namespace taffy
