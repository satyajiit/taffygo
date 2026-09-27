// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/omnibox_input_classifier.h"

#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

// PAR-BOX-001. The classifier is pure, so this file is a table rather than a
// selection of examples: every row is an input somebody will type, and the
// expected kind is the whole contract.
//
// The property that outranks every individual row: **no input ever produces a
// value that starts a task.** The type cannot express one, and the last test
// in this file states that in terms a reviewer can check without reading the
// header.

namespace taffy {
namespace {

struct Row {
  const char* input;
  OmniboxInputKind expected_kind;
  ClassificationReason expected_reason;
  const char* note;
};

const Row kRows[] = {
    // --- nothing ----------------------------------------------------------
    {"", OmniboxInputKind::kEmpty, ClassificationReason::kNone, "empty"},
    {"   ", OmniboxInputKind::kEmpty, ClassificationReason::kNone,
     "whitespace only"},

    // --- explicit schemes -------------------------------------------------
    {"https://example.com/", OmniboxInputKind::kUrlWithExplicitScheme,
     ClassificationReason::kNone, "ordinary https"},
    {"http://example.com/path?q=1#frag",
     OmniboxInputKind::kUrlWithExplicitScheme, ClassificationReason::kNone,
     "query and fragment survive"},
    {"HTTPS://EXAMPLE.COM", OmniboxInputKind::kUrlWithExplicitScheme,
     ClassificationReason::kNone, "scheme match is case insensitive"},
    {"ftp://files.example.com/pub", OmniboxInputKind::kUrlWithExplicitScheme,
     ClassificationReason::kNone, "ftp is navigable"},
    {"wss://socket.example.com/", OmniboxInputKind::kUrlWithExplicitScheme,
     ClassificationReason::kNone, "web socket scheme"},
    {"https://", OmniboxInputKind::kSearchQuery, ClassificationReason::kNone,
     "a scheme with no location is prose"},
    {"mailto:someone@example.com", OmniboxInputKind::kSearchQuery,
     ClassificationReason::kNone,
     "an unknown scheme is neither navigated nor refused"},

    // --- schemes that execute --------------------------------------------
    {"javascript:alert(1)", OmniboxInputKind::kRefusedScheme,
     ClassificationReason::kActiveContentScheme, "the paste attack"},
    {"JavaScript:alert(1)", OmniboxInputKind::kRefusedScheme,
     ClassificationReason::kActiveContentScheme, "case does not help"},
    {"data:text/html,<script>1</script>", OmniboxInputKind::kRefusedScheme,
     ClassificationReason::kActiveContentScheme, "data URL"},
    {"blob:https://example.com/abcd", OmniboxInputKind::kRefusedScheme,
     ClassificationReason::kActiveContentScheme, "blob URL"},

    // --- schemes with a later milestone ----------------------------------
    {"file:///sdcard/report.pdf", OmniboxInputKind::kRefusedScheme,
     ClassificationReason::kSchemeNotSupportedYet, "PAR-FILE-006 is M4"},
    {"content://media/external/images/1", OmniboxInputKind::kRefusedScheme,
     ClassificationReason::kSchemeNotSupportedYet, "content provider URI"},
    {"intent://scan#Intent;scheme=zxing;end", OmniboxInputKind::kRefusedScheme,
     ClassificationReason::kSchemeNotSupportedYet, "PAR-PERM-007 is M4"},
    {"view-source:https://example.com", OmniboxInputKind::kRefusedScheme,
     ClassificationReason::kSchemeNotSupportedYet,
     "PAR-NAV-008 is out of scope for 1.0"},

    // --- implicit scheme, unambiguous ------------------------------------
    {"example.com", OmniboxInputKind::kUrlWithImplicitScheme,
     ClassificationReason::kNone, "the common case"},
    {"www.example.co.uk/page", OmniboxInputKind::kUrlWithImplicitScheme,
     ClassificationReason::kNone, "multi-label registry"},
    {"example.com:8443/admin", OmniboxInputKind::kUrlWithImplicitScheme,
     ClassificationReason::kNone, "explicit port on a real registry"},
    {"localhost", OmniboxInputKind::kUrlWithImplicitScheme,
     ClassificationReason::kNone, "localhost is a place"},
    {"localhost:8080/debug", OmniboxInputKind::kUrlWithImplicitScheme,
     ClassificationReason::kNone, "localhost with a port"},
    {"dev.localhost", OmniboxInputKind::kUrlWithImplicitScheme,
     ClassificationReason::kNone, "the localhost suffix"},
    {"127.0.0.1", OmniboxInputKind::kUrlWithImplicitScheme,
     ClassificationReason::kNone, "dotted quad"},
    {"192.168.1.20:3000", OmniboxInputKind::kUrlWithImplicitScheme,
     ClassificationReason::kNone, "dotted quad with a port"},
    {"[::1]:8080", OmniboxInputKind::kUrlWithImplicitScheme,
     ClassificationReason::kNone, "bracketed IPv6 literal"},

    // --- search ----------------------------------------------------------
    {"best desk lamp", OmniboxInputKind::kSearchQuery,
     ClassificationReason::kNone, "whitespace means prose"},
    {"example.com is down", OmniboxInputKind::kSearchQuery,
     ClassificationReason::kNone, "a host inside a sentence is a sentence"},
    {"weather", OmniboxInputKind::kSearchQuery, ClassificationReason::kNone,
     "a bare word"},
    {"how do i restore tabs?", OmniboxInputKind::kSearchQuery,
     ClassificationReason::kNone, "a question"},

    // --- ambiguous, which is the row that matters -------------------------
    {"server.internal", OmniboxInputKind::kAmbiguous,
     ClassificationReason::kUnknownRegistry,
     "a dotted name with no public registry"},
    // ".docx" deliberately: the obvious choice ".final" is a real gTLD in
    // the Public Suffix List, so "report.final" is a navigable host — the
    // first on-device run of this suite caught exactly that.
    {"report.docx", OmniboxInputKind::kAmbiguous,
     ClassificationReason::kUnknownRegistry,
     "a filename shaped like a host"},
    {"3.14", OmniboxInputKind::kAmbiguous,
     ClassificationReason::kNumericHostShape,
     "URL parsing would make this an IP address"},
    {"1.2", OmniboxInputKind::kAmbiguous,
     ClassificationReason::kNumericHostShape, "two-component number"},
    {"wiki:8080", OmniboxInputKind::kAmbiguous,
     ClassificationReason::kSingleLabelHost,
     "a single label with a port is an intranet host or a typo"},
    {"https://user:secret@example.com/",
     OmniboxInputKind::kAmbiguous, ClassificationReason::kEmbeddedCredentials,
     "the visible prefix is not the destination"},
    {"user@example.com:80", OmniboxInputKind::kAmbiguous,
     ClassificationReason::kEmbeddedCredentials,
     "credentials without a scheme"},
    {"例え.テスト", OmniboxInputKind::kAmbiguous,
     ClassificationReason::kNonAsciiHost,
     "internationalized domains are PAR-WEB-012 at M4"},
};

TEST(OmniboxInputClassifierTest, EveryRowInTheTable) {
  for (const Row& row : kRows) {
    const OmniboxClassification result =
        OmniboxInputClassifier::Classify(row.input);
    EXPECT_EQ(row.expected_kind, result.kind)
        << "input \"" << row.input << "\" (" << row.note << ")";
    EXPECT_EQ(row.expected_reason, result.reason)
        << "input \"" << row.input << "\" (" << row.note << ")";
  }
}

TEST(OmniboxInputClassifierTest, AmbiguityAlwaysAsks) {
  for (const Row& row : kRows) {
    const OmniboxClassification result =
        OmniboxInputClassifier::Classify(row.input);
    EXPECT_EQ(result.kind == OmniboxInputKind::kAmbiguous,
              result.requires_user_choice)
        << "input \"" << row.input << "\"";
  }
}

TEST(OmniboxInputClassifierTest, AmbiguousInputCarriesBothInterpretations) {
  const OmniboxClassification result =
      OmniboxInputClassifier::Classify("server.internal");

  ASSERT_EQ(OmniboxInputKind::kAmbiguous, result.kind);
  EXPECT_TRUE(result.requires_user_choice);
  // The surface asking the question does not have to reconstruct either
  // option, so it cannot reconstruct them differently.
  EXPECT_EQ("server.internal", result.search_terms);
  EXPECT_EQ("https://server.internal/", result.navigation_url);
}

TEST(OmniboxInputClassifierTest, NavigationUrlIsOnlySetForNavigableKinds) {
  for (const Row& row : kRows) {
    const OmniboxClassification result =
        OmniboxInputClassifier::Classify(row.input);
    const bool navigable =
        result.kind == OmniboxInputKind::kUrlWithExplicitScheme ||
        result.kind == OmniboxInputKind::kUrlWithImplicitScheme ||
        result.kind == OmniboxInputKind::kAmbiguous;
    if (!navigable) {
      EXPECT_TRUE(result.navigation_url.empty())
          << "input \"" << row.input
          << "\" produced a navigation target it should not have";
    }
  }
}

TEST(OmniboxInputClassifierTest, RefusedSchemesStillOfferTheText) {
  // Refusing must not swallow what the person typed. A pasted javascript: URL
  // becomes a search — which is what somebody who pasted it by accident wants,
  // and what somebody who was told to paste it gets instead of an execution.
  const OmniboxClassification result =
      OmniboxInputClassifier::Classify("javascript:alert(1)");

  EXPECT_EQ(OmniboxInputKind::kRefusedScheme, result.kind);
  EXPECT_EQ("javascript:alert(1)", result.search_terms);
  EXPECT_TRUE(result.navigation_url.empty());
  EXPECT_FALSE(result.requires_user_choice);
}

TEST(OmniboxInputClassifierTest, LeadingAndTrailingWhitespaceIsTrimmed) {
  const OmniboxClassification spaced =
      OmniboxInputClassifier::Classify("  example.com\t");
  const OmniboxClassification bare =
      OmniboxInputClassifier::Classify("example.com");

  EXPECT_EQ(bare, spaced);
}

TEST(OmniboxInputClassifierTest, PolicyDecidesTheSingleLabelCase) {
  OmniboxClassifierPolicy permissive;
  permissive.single_label_hosts_are_ambiguous = false;

  EXPECT_EQ(OmniboxInputKind::kAmbiguous,
            OmniboxInputClassifier::Classify("wiki:8080").kind);
  EXPECT_EQ(OmniboxInputKind::kSearchQuery,
            OmniboxInputClassifier::Classify("wiki:8080", permissive).kind);
}

TEST(OmniboxInputClassifierTest, TheSameInputAlwaysGivesTheSameAnswer) {
  // Determinism is the parity row's word. No history, no ranking, no clock, no
  // network — so repeating a call must be pointless, and this test is what
  // notices if somebody adds state.
  for (const Row& row : kRows) {
    const OmniboxClassification first =
        OmniboxInputClassifier::Classify(row.input);
    const OmniboxClassification second =
        OmniboxInputClassifier::Classify(row.input);
    EXPECT_EQ(first, second) << "input \"" << row.input << "\"";
  }
}

TEST(OmniboxInputClassifierTest, NoInputCanStartATask) {
  // The parity row's second half — "ambiguous input never starts a task
  // silently" — is encoded in the result type: there is no kind meaning
  // research, and no field carrying a goal or a plan. Enumerating the kinds
  // here means a value added for that purpose would have to change this test.
  const std::vector<OmniboxInputKind> every_kind = {
      OmniboxInputKind::kEmpty,
      OmniboxInputKind::kUrlWithExplicitScheme,
      OmniboxInputKind::kUrlWithImplicitScheme,
      OmniboxInputKind::kSearchQuery,
      OmniboxInputKind::kAmbiguous,
      OmniboxInputKind::kRefusedScheme,
  };
  EXPECT_EQ(static_cast<size_t>(OmniboxInputKind::kRefusedScheme) + 1,
            every_kind.size());

  // And no classification of anything in the table reaches a kind outside it.
  for (const Row& row : kRows) {
    const OmniboxClassification result =
        OmniboxInputClassifier::Classify(row.input);
    EXPECT_LE(static_cast<int>(result.kind),
              static_cast<int>(OmniboxInputKind::kRefusedScheme));
  }
}

}  // namespace
}  // namespace taffy
