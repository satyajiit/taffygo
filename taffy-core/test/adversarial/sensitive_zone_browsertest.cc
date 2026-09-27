// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <string>
#include <vector>

#include "taffy/browser/credential_boundary.h"
#include "taffy/components/intelligence/content/credential_field_metadata.h"
#include "taffy/components/intelligence/content/observability_recorder.h"
#include "taffy/components/intelligence/content/page_intelligence_broker.h"
#include "taffy/common/public/bip_budget.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/test/corpus/corpus_manifest.h"
#include "taffy/test/support/canary_leak_scanner.h"
#include "taffy/test/support/taffy_observation_test_base.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/shell/browser/shell.h"
#include "testing/gtest/include/gtest/gtest.h"

// Sensitive zones: probing them, observing near them, and proving that nothing
// crosses out of them.
//
// **The property this file defends.** Password, passkey, payment,
// authentication-token, website-cookie and key material never enters model
// context, cloud logs, analytics, crash reports, screenshots or task artifacts.
// It is a zero-tolerance invariant, and the corpus's seeded canaries are how it
// is measured rather than argued.
//
// **Why probing is its own test.** A runtime does not have to read a secret to
// leak information about one. Field counts, value lengths, a redaction summary
// that says "one secret was suppressed here" in a page where only one field
// exists — each of those is a probe, and the answer to all of them has to be
// the same shape whether the field holds a canary or nothing at all.

namespace taffy::test {
namespace {

using SensitiveZoneTest = TaffyObservationTestBase;

// The sensitive form, observed. Nothing it carries reaches any sink, and the
// redaction summary says suppression happened rather than leaving the caller to
// infer it from an absence.
IN_PROC_BROWSER_TEST_F(SensitiveZoneTest, TheSensitiveFormLeaksNothing) {
  const ObservationEnvelope envelope = ObserveFixture("sensitive-form");
  ASSERT_TRUE(envelope.code == ObservationResultCode::kOk ||
              envelope.code == ObservationResultCode::kIncomplete);

  CanaryLeakScanner scanner = CanaryLeakScanner::ForFixture("sensitive-form");
  scanner.AddSink("projection", client().TranscriptForLeakScan());
  scanner.AddJournal(journal());
  scanner.AddAuditStream(audit_stream());
  EXPECT_TRUE(scanner.AssertAllSinksClean());

  EXPECT_GT(envelope.redaction.suppressed_secret_value_count, 0u)
      << "The sensitive form was observed and nothing was reported as "
         "suppressed. Either the fields were not classified, or they were and "
         "the caller cannot tell the difference between a page with no secrets "
         "and a page whose secrets were withheld.";
  EXPECT_GT(envelope.redaction.sensitive_zone_count, 0u);
}

// The login page. Its canary is in a password field, and the field's presence
// is observable while its value is not — which is exactly the split the
// credential boundary exists to produce.
IN_PROC_BROWSER_TEST_F(SensitiveZoneTest, ThePasswordFieldIsVisibleAndItsValueIsNot) {
  const ObservationEnvelope envelope = ObserveFixture("login-page");

  CanaryLeakScanner scanner = CanaryLeakScanner::ForFixture("login-page");
  scanner.AddSink("projection", client().TranscriptForLeakScan());
  EXPECT_TRUE(scanner.AssertAllSinksClean());

  EXPECT_GT(envelope.node_count, 0u)
      << "The login page produced no nodes at all. A page whose structure is "
         "invisible is one nothing can reason about, including the part of the "
         "runtime whose job is to notice that a credential is being entered.";
  EXPECT_GT(envelope.redaction.suppressed_secret_value_count, 0u);
}

// The classification is by field, not by page. A page with one credential field
// and twenty ordinary ones still yields the twenty: refusing to observe a page
// because part of it is sensitive would make an authentication flow invisible
// rather than safe.
IN_PROC_BROWSER_TEST_F(SensitiveZoneTest, OrdinaryFieldsSurviveBesideSensitiveOnes) {
  const ObservationEnvelope sensitive = ObserveFixture("sensitive-form");
  const ObservationEnvelope ordinary = ObserveFixture("form-laboratory");

  EXPECT_GT(ordinary.node_count, 0u);
  EXPECT_GT(sensitive.node_count, 0u)
      << "The sensitive form yielded nothing. Suppressing a value is not the "
         "same as suppressing a page.";
  EXPECT_EQ(0u, ordinary.redaction.suppressed_secret_value_count)
      << "The ordinary form laboratory reported suppressed secrets. If every "
         "page reports suppression, the signal on the pages that matter carries "
         "no information.";
}

// A probe that asks how much was suppressed gets a count, not a length and not
// a fingerprint. A length-derived value is a secret in a smaller container.
IN_PROC_BROWSER_TEST_F(SensitiveZoneTest, SuppressionIsCountedNotMeasured) {
  const ObservationEnvelope envelope = ObserveFixture("sensitive-form");

  const CorpusFixture& fixture = CorpusManifest::Get().ById("sensitive-form");
  ASSERT_FALSE(fixture.sensitive_omissions.empty())
      << "The corpus declares no sensitive omissions for the sensitive form, "
         "so this assertion would have nothing to be about.";

  // The transcript is everything the caller received. No omitted value's
  // length may be recoverable from it, which is asserted the only way it can
  // be: the values themselves, in every spelling the scanner knows, are absent.
  CanaryLeakScanner scanner = CanaryLeakScanner::ForFixture("sensitive-form");
  scanner.AddSink("projection", client().TranscriptForLeakScan());
  EXPECT_TRUE(scanner.AssertAllSinksClean());

  EXPECT_GT(envelope.redaction.suppressed_secret_value_count, 0u);
  EXPECT_LE(envelope.redaction.suppressed_secret_value_count,
            envelope.node_count)
      << "More secrets were reported suppressed than there are nodes, which "
         "means the count is measuring something other than fields.";
}

// The credential boundary suspends assistant access while a credential is being
// entered, and it does so on the field's classification rather than on the
// page's address. A boundary keyed to a URL would be defeated by any site that
// puts a password field somewhere unexpected.
IN_PROC_BROWSER_TEST_F(SensitiveZoneTest, TheBoundaryFollowsTheFieldNotThePage) {
  const TabId tab = broker()->tab_id();
  CredentialBoundary boundary;

  CredentialFieldMetadata password;
  password.tab_id = ToRecordIdentifier(tab.value);
  password.credential_class = CredentialClass::kPassword;
  boundary.NoteCredentialField(password);
  EXPECT_EQ(1u, boundary.CredentialFieldCount(tab));

  boundary.BeginCredentialInteraction(tab, CredentialClass::kPassword);
  EXPECT_FALSE(boundary.AssistantMayObserve(tab))
      << "Interacting with a password field did not suspend assistant "
         "observation.";
  EXPECT_FALSE(boundary.AssistantMayAct(tab));

  boundary.EndCredentialInteraction(tab, CredentialClass::kPassword);
  EXPECT_TRUE(boundary.AssistantMayObserve(tab));

  boundary.BeginCredentialInteraction(tab, CredentialClass::kOneTimeCode);
  EXPECT_FALSE(boundary.AssistantMayObserve(tab))
      << "A one-time code is suspended exactly like a password. Treating it as "
         "ordinary text is how a code intended for one login ends up in a "
         "context that outlives it.";
  EXPECT_TRUE(CredentialClassSuspendsAssistantAccess(
      CredentialClass::kOneTimeCode));
}

// Every fixture the corpus seeds with a canary, observed in one test. The
// interesting failure is a value crossing from a page nobody was looking at, so
// the scan is over the whole corpus rather than per page.
IN_PROC_BROWSER_TEST_F(SensitiveZoneTest, NoSeededSecretEscapesAnyCarryingFixture) {
  std::vector<std::string> carriers;
  for (const CorpusCanary& canary : CorpusManifest::Get().canaries()) {
    for (const std::string& fixture_id : canary.carried_by) {
      if (std::find(carriers.begin(), carriers.end(), fixture_id) ==
          carriers.end()) {
        carriers.push_back(fixture_id);
      }
    }
  }
  ASSERT_FALSE(carriers.empty());

  for (const std::string& fixture_id : carriers) {
    SCOPED_TRACE(fixture_id);
    // One task per page. The carriers are spread across origins — the framed
    // payload is served from the partner origin and the rest from the
    // first-party one — and a grant may only narrow, so on one task the
    // service refuses every origin after the first and the walk continues
    // under a grant naming a page it has left. The pages are still projected
    // (a root frame is never subject to the child-frame allowlist), so the
    // scan below is not vacuous; what is lost is that each page was read under
    // the grant this test says it was read under.
    BeginNewTask();
    ObserveFixture(fixture_id);
  }

  CanaryLeakScanner scanner = CanaryLeakScanner::ForWholeCorpus();
  scanner.AddSink("projection across every carrying fixture",
                  client().TranscriptForLeakScan());
  scanner.AddJournal(journal());
  scanner.AddAuditStream(audit_stream());
  EXPECT_TRUE(scanner.AssertAllSinksClean());
}

}  // namespace
}  // namespace taffy::test
