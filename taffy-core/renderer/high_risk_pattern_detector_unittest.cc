// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/high_risk_pattern_detector.h"

#include "taffy/renderer/observation_limits.h"
#include "testing/gtest/include/gtest/gtest.h"

// The bounded detectors of protocol section 9.2, over free text that did not
// come from a form control.
//
// Two properties, and the second is as important as the first. A detector
// that misses a card number is a leak. A detector that eats an ordinary
// paragraph is not "conservative", it is a redaction layer that deleted the
// answer - and every string in the second test below is one the deterministic
// fixtures actually contain.

namespace taffy {
namespace {

const ObservationLimits& Limits() {
  return ObservationLimits::ProcessSafeCeiling();
}

TEST(HighRiskPatternTest, PaymentCardNumbersAreCaughtInsideText) {
  // The old detector required the whole string to be digits and separators,
  // so "Card ending 4111 1111 1111 1111" walked straight through it.
  const HighRiskPatternDetector detector(Limits());
  EXPECT_EQ(detector.Detect("Card ending 4111 1111 1111 1111"),
            HighRiskPatternKind::kPaymentCardNumber);
  EXPECT_EQ(detector.Detect("4111-1111-1111-1111"),
            HighRiskPatternKind::kPaymentCardNumber);
}

TEST(HighRiskPatternTest, LongDigitRunsAreCaught) {
  const HighRiskPatternDetector detector(Limits());
  EXPECT_EQ(detector.Detect("Account 123456789012345"),
            HighRiskPatternKind::kLongDigitRun);
}

TEST(HighRiskPatternTest, KeyMaterialAndTokensAreCaught) {
  const HighRiskPatternDetector detector(Limits());
  EXPECT_EQ(
      detector.Detect("key 0123456789abcdef0123456789abcdef"),
      HighRiskPatternKind::kHexKeyMaterial);
  EXPECT_EQ(detector.Detect(
                "token eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxIn0.abc123XYZ"),
            HighRiskPatternKind::kOpaqueToken);
  // The corpus canaries are shaped like the secrets they stand in for, so
  // the detector catches them as a second line even when the first line -
  // never reading a prohibited value - has already done its job.
  EXPECT_NE(detector.Detect("TAFFYGO-CANARY-APIKEY-6E30D9"),
            HighRiskPatternKind::kNone);
}

TEST(HighRiskPatternTest, OrdinaryPageTextSurvives) {
  // A detector that eats the answer is not a redaction layer, it is a bug.
  // Every string here is from the deterministic fixtures.
  const HighRiskPatternDetector detector(Limits());
  for (const char* text : {"How desk lighting affects focus",
                           "Lumen Arc desk lamp",
                           "129.00 USD",
                           "in stock, ships today",
                           "800 lumens, 2700-5000 K, 12 W, 2-year warranty",
                           "3 lamps x 45 s reading time, 600 lux",
                           "2026-02-11",
                           "SKU LA-2600",
                           "Call 1-800-555-0123 today"}) {
    EXPECT_EQ(detector.Detect(text), HighRiskPatternKind::kNone)
        << text;
  }
}

TEST(HighRiskPatternTest, SeedPhraseDetectionIsWholeStringOnly) {
  // A run of short lowercase words is also what ordinary prose looks like, so
  // scanning for one inside a paragraph would delete paragraphs. A recovery
  // phrase sits alone in its own element, so the test is the whole string.
  // The cost is stated rather than hidden: an all-lowercase sentence of the
  // right length is treated as a phrase, which errs safely.
  const HighRiskPatternDetector detector(Limits());
  EXPECT_EQ(detector.Detect(
                "witch collapse practice feed shame open despair creek road "
                "again ice"),
            HighRiskPatternKind::kSeedPhrase);
  EXPECT_EQ(detector.Detect(
                "The quick brown fox jumps over the lazy dog and then some "
                "more"),
            HighRiskPatternKind::kNone);
}

}  // namespace
}  // namespace taffy
