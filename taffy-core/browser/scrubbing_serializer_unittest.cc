// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/scrubbing_serializer.h"

#include <string>
#include <type_traits>

#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

// The gate itself: that it cannot be bypassed, and that what comes out of it
// no longer contains what went in.

namespace taffy {
namespace {

TEST(ScrubbingSerializerTest, ScrubbedTextCannotBeForged) {
  // The property the whole seam rests on. Every diagnostic sink in
  // //taffy takes a ScrubbedText, and the serializer is the only
  // thing that can make one, so forgetting to scrub does not compile.
  static_assert(!std::is_default_constructible_v<ScrubbedText>,
                "A default-constructible ScrubbedText could be filled with an "
                "unscrubbed value.");
  static_assert(!std::is_constructible_v<ScrubbedText, std::string>,
                "A string constructor would let unscrubbed text into a sink "
                "that believes it is scrubbed.");
  static_assert(!std::is_constructible_v<ScrubbedText, const char*>,
                "Same, for a literal.");
  // It is still a value: copying and moving one is how it reaches a sink.
  static_assert(std::is_copy_constructible_v<ScrubbedText>);
  static_assert(std::is_move_constructible_v<ScrubbedText>);
}

TEST(ScrubbingSerializerTest, OrdinaryTextPassesThroughUnchanged) {
  const ScrubbedText scrubbed =
      ScrubbingSerializer::Serialize("The page did not load: net error -105.");

  EXPECT_EQ("The page did not load: net error -105.", scrubbed.value());
  EXPECT_FALSE(scrubbed.report().anything_was_removed());
  EXPECT_EQ(0u, scrubbed.report().canary_hit_count);
}

TEST(ScrubbingSerializerTest, ALabelledSecretIsReplacedInPlace) {
  const ScrubbedText scrubbed =
      ScrubbingSerializer::Serialize("password=hunter2");

  EXPECT_EQ("password=[redacted]", scrubbed.value());
  EXPECT_EQ(1u, scrubbed.report().redaction_count);
  EXPECT_TRUE(RuleFired(scrubbed.report().applied_rule_mask,
                        ScrubRule::kSensitiveParameterValue));
}

TEST(ScrubbingSerializerTest, AQueryStringDoesNotSurvive) {
  const ScrubbedText scrubbed = ScrubbingSerializer::Serialize(
      "redirect to https://primary.taffy.test/auth?code=SECRET&state=OPAQUE");

  EXPECT_EQ(std::string::npos, scrubbed.value().find("SECRET"));
  EXPECT_EQ(std::string::npos, scrubbed.value().find("OPAQUE"));
  // The origin survives, because an origin is what a diagnostic is allowed to
  // carry and losing it would make the record useless.
  EXPECT_NE(std::string::npos, scrubbed.value().find("primary.taffy.test"));
  EXPECT_TRUE(RuleFired(scrubbed.report().applied_rule_mask,
                        ScrubRule::kUrlReducedToOrigin));
}

TEST(ScrubbingSerializerTest, ACanaryIsCountedAsWellAsRemoved) {
  const ScrubbedText scrubbed = ScrubbingSerializer::Serialize(
      "unexpected value TAFFYGO-CANARY-PASSWORD-4F1A9C in the payload");

  EXPECT_EQ(std::string::npos, scrubbed.value().find("TAFFYGO-CANARY"));
  // Counted, not silently cleaned: a canary that reached the scrubber means a
  // redaction upstream of here failed, and the count is how that becomes
  // visible instead of invisible.
  EXPECT_EQ(1u, scrubbed.report().canary_hit_count);
  EXPECT_TRUE(RuleFired(scrubbed.report().applied_rule_mask,
                        ScrubRule::kCanaryTripwire));
}

TEST(ScrubbingSerializerTest, SeveralSecretsInOneStringAllGo) {
  const ScrubbedText scrubbed = ScrubbingSerializer::Serialize(
      "password=hunter2 and api_key=abcd1234 and "
      "https://primary.taffy.test/a?token=V");

  EXPECT_EQ(std::string::npos, scrubbed.value().find("hunter2"));
  EXPECT_EQ(std::string::npos, scrubbed.value().find("abcd1234"));
  EXPECT_EQ(std::string::npos, scrubbed.value().find("token=V"));
  EXPECT_GE(scrubbed.report().redaction_count, 3u);
}

TEST(ScrubbingSerializerTest, AnOriginIsAllAUrlEverContributes) {
  const ScrubbedText scrubbed = ScrubbingSerializer::SerializeOriginOf(
      GURL("https://primary.taffy.test/auth/callback?code=SECRET#token=T"));

  EXPECT_EQ("https://primary.taffy.test", scrubbed.value());
  EXPECT_TRUE(RuleFired(scrubbed.report().applied_rule_mask,
                        ScrubRule::kUrlReducedToOrigin));
}

TEST(ScrubbingSerializerTest, AnOpaqueOriginSaysNullAndNothingElse) {
  const ScrubbedText scrubbed = ScrubbingSerializer::SerializeOriginOf(
      GURL("data:text/html,<p>TAFFYGO-CANARY-SEED-2B44E8"));

  // "a sandboxed document" without saying which one, and without the document.
  EXPECT_EQ("null", scrubbed.value());
}

TEST(ScrubbingSerializerTest, UrlUserInfoIsReported) {
  const ScrubbedText scrubbed = ScrubbingSerializer::SerializeOriginOf(
      GURL("https://user:secret@primary.taffy.test/a"));

  EXPECT_EQ(std::string::npos, scrubbed.value().find("secret"));
  EXPECT_TRUE(
      RuleFired(scrubbed.report().applied_rule_mask, ScrubRule::kUrlUserInfo));
}

TEST(ScrubbingSerializerTest, AKnownSensitiveValueIsNeverPassedAtAll) {
  // The correct call for a value the caller already knows is sensitive. The
  // value is not an argument, so there is nothing to leak even if the scanner
  // had a gap.
  const ScrubbedText scrubbed =
      ScrubbingSerializer::RedactedPlaceholder(CredentialClass::kPassword);

  EXPECT_EQ("[redacted-password]", scrubbed.value());
  EXPECT_EQ(1u, scrubbed.report().redaction_count);
}

TEST(ScrubbingSerializerTest, EveryCredentialClassHasItsOwnPlaceholder) {
  std::string previous;
  for (int value = 0;
       value <= static_cast<int>(CredentialClass::kCaptchaAnswer); ++value) {
    const ScrubbedText scrubbed = ScrubbingSerializer::RedactedPlaceholder(
        static_cast<CredentialClass>(value));
    EXPECT_FALSE(scrubbed.value().empty()) << "class " << value;
    EXPECT_EQ('[', scrubbed.value().front());
    EXPECT_NE(previous, scrubbed.value())
        << "class " << value << " reuses the previous placeholder";
    previous = scrubbed.value();
  }
}

TEST(ScrubbingSerializerTest, AnOversizedPayloadIsTruncatedBeforeScanning) {
  // A diagnostic larger than the bound is a payload, and scanning an unbounded
  // attacker-influenced string in the browser process is a cost an attacker
  // gets to choose.
  const std::string oversized(ScrubbingSerializer::kMaxScannedCharacters * 2,
                              'x');
  const ScrubbedText scrubbed = ScrubbingSerializer::Serialize(oversized);

  EXPECT_LT(scrubbed.value().size(), oversized.size());
  EXPECT_NE(std::string::npos, scrubbed.value().find("[truncated]"));
}

TEST(ScrubbingSerializerTest, ASecretPastTheBoundIsTruncatedAway) {
  std::string payload(ScrubbingSerializer::kMaxScannedCharacters, 'x');
  payload += "TAFFYGO-CANARY-APIKEY-6E30D9";

  const ScrubbedText scrubbed = ScrubbingSerializer::Serialize(payload);
  EXPECT_EQ(std::string::npos, scrubbed.value().find("TAFFYGO-CANARY"));
}

TEST(ScrubbingSerializerTest, TheEmptyCaseNeedsNoScan) {
  const ScrubbedText empty = ScrubbingSerializer::Empty();
  EXPECT_TRUE(empty.value().empty());
  EXPECT_FALSE(empty.report().anything_was_removed());
}

TEST(ScrubbingSerializerTest, ScrubbingIsIdempotent) {
  // Whatever comes out has nothing left to remove. A serializer whose output
  // still tripped its own rules would make the redaction count meaningless,
  // and would eventually chew through a legitimate diagnostic one pass at a
  // time.
  const ScrubbedText once = ScrubbingSerializer::Serialize(
      "password=hunter2 https://primary.taffy.test/a?token=V "
      "TAFFYGO-CANARY-OTP-9D2E60");
  const ScrubbedText twice = ScrubbingSerializer::Serialize(once.value());

  EXPECT_EQ(once.value(), twice.value());
  EXPECT_FALSE(twice.report().anything_was_removed());
}

}  // namespace
}  // namespace taffy
