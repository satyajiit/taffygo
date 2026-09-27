// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
#include <vector>

#include "taffy/renderer/test/endpoint_test_harness.h"
#include "taffy/renderer/test/fixture_corpus.h"
#include "testing/gtest/include/gtest/gtest.h"

// The zero-leak property, over the fixtures that carry seeded secrets.
//
// Protocol section 17.4 makes this a required safety property, and
// docs/security/data-and-privacy.md section 7.3 lists what may never be
// eligible for AI context at all. The corpus seeds each of those classes as a
// greppable canary so the assertion is a search rather than a judgement.
//
// Two things about how this is written matter as much as what it asserts:
//
//   * The tokens come from the corpus manifest, never from this file. The
//     corpus is versioned and immutable and its canaries can rotate; a test
//     with the tokens typed into it would keep passing after a rotation, and
//     a rotation is exactly when a leak would be introduced.
//
//   * Every string in the reply is searched, not only the obvious ones.
//     Names, descriptions, text runs, normalized values, attribute values,
//     source locators, warning detail codes, and destination serializations
//     all leave this process. A secret that escaped through a source locator
//     is still a secret that escaped.

namespace taffy::test {
namespace {

// Fails with the token and the field it was found in. A leak test whose
// failure message says only "expected 0, got 1" costs an hour to diagnose.
void ExpectNoTokenAnywhere(const std::vector<std::string>& strings,
                           const std::vector<std::string>& tokens,
                           const std::string& fixture_id) {
  for (const std::string& value : strings) {
    for (const std::string& token : tokens) {
      EXPECT_EQ(value.find(token), std::string::npos)
          << "fixture '" << fixture_id << "' leaked the canary '" << token
          << "' inside the emitted string '" << value << "'";
    }
  }
}

class RedactionCanaryTest : public EndpointTestHarness {};

TEST_F(RedactionCanaryTest, SensitiveCheckoutFormEmitsNoSecret) {
  const FixtureCorpus corpus = FixtureCorpus::Load();
  const Fixture& fixture = corpus.ById("sensitive-form");
  ASSERT_FALSE(fixture.sensitive_omissions.empty())
      << "the sensitive-form fixture declares nothing to omit, so this test "
         "would assert nothing";

  LoadAndBind(corpus.ReadFixtureHtml(fixture.id));
  mojom::SnapshotResultPtr result = Snapshot(DocumentRequest());
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->snapshot);

  // Every canary in the corpus, not only the ones this fixture is supposed to
  // carry: a token that leaked from the wrong page is still a leak, and a
  // per-fixture search would miss it.
  ExpectNoTokenAnywhere(AllStringsIn(*result), corpus.AllCanaryTokens(),
                        fixture.id);
  // Plus everything the fixture itself declares, which includes the card
  // number and expiry - values that are not canaries but are still secrets.
  ExpectNoTokenAnywhere(AllStringsIn(*result), fixture.sensitive_omissions,
                        fixture.id);

  // Not merely absent: positively reported as withheld. An empty snapshot
  // would also contain no secrets.
  ASSERT_TRUE(result->snapshot->redaction_summary);
  EXPECT_GT(result->snapshot->redaction_summary->suppressed_secret_value_count,
            0u);
}

TEST_F(RedactionCanaryTest, WithheldSecretsCarryNoMeasurementOfThemselves) {
  // Protocol section 9.1 names a length-derived fingerprint specifically, and
  // the same reasoning covers every other measurement of a secret: presence,
  // emptiness, a hash, a character-class summary.
  const FixtureCorpus corpus = FixtureCorpus::Load();
  LoadAndBind(corpus.ReadFixtureHtml("sensitive-form"));
  mojom::SnapshotResultPtr result = Snapshot(DocumentRequest());
  ASSERT_TRUE(result && result->snapshot);

  int withheld = 0;
  for (const mojom::SemanticNodePtr& node : result->snapshot->nodes) {
    if (!node->value_descriptor ||
        node->value_descriptor->kind != mojom::ValueKind::kSecretWithheld) {
      continue;
    }
    ++withheld;
    EXPECT_FALSE(node->name.has_value());
    EXPECT_FALSE(node->description.has_value());
    EXPECT_TRUE(node->text_runs.empty());
    EXPECT_FALSE(node->value_descriptor->normalized_value.has_value());
    // False, not "whatever the control has": emptiness is itself a fact about
    // a secret, and this code never looked.
    EXPECT_FALSE(node->value_descriptor->present);
    EXPECT_TRUE(node->value_descriptor->redacted);
    EXPECT_EQ(node->sensitivity, mojom::Sensitivity::kCredential);
    // No actions. Focusing a credential control on an assistant's behalf is a
    // separate question nothing before the M5 exit review may answer.
    EXPECT_TRUE(node->actions.empty());
  }
  EXPECT_GT(withheld, 0)
      << "the checkout fixture carries credential controls; if none produced "
         "a placeholder, the form adapter is not seeing them at all";
}

TEST_F(RedactionCanaryTest, LoginPasswordNeverLeaves) {
  const FixtureCorpus corpus = FixtureCorpus::Load();
  const Fixture& fixture = corpus.ById("login-page");
  LoadAndBind(corpus.ReadFixtureHtml(fixture.id));
  mojom::SnapshotResultPtr result = Snapshot(DocumentRequest());
  ASSERT_TRUE(result);
  ExpectNoTokenAnywhere(AllStringsIn(*result), corpus.AllCanaryTokens(),
                        fixture.id);
  ExpectNoTokenAnywhere(AllStringsIn(*result), fixture.sensitive_omissions,
                        fixture.id);
}

TEST_F(RedactionCanaryTest, ClosedShadowRootSecretNeverLeaves) {
  // The closed-root fixture is the one that proves the shadow path is a
  // capability Chromium already has rather than a bypass added for AI: the
  // widget is reported as present, and the password inside it is not read.
  const FixtureCorpus corpus = FixtureCorpus::Load();
  const Fixture& fixture = corpus.ById("shadow-dom-closed");
  LoadAndBind(corpus.ReadFixtureHtml(fixture.id));
  mojom::SnapshotResultPtr result = Snapshot(DocumentRequest());
  ASSERT_TRUE(result);
  ExpectNoTokenAnywhere(AllStringsIn(*result), corpus.AllCanaryTokens(),
                        fixture.id);
}

TEST_F(RedactionCanaryTest, EverySecretCarryingFixtureIsClean) {
  // The sweep. Any fixture the corpus says carries something omissible is
  // observed and searched, so adding a fixture with a new secret class
  // extends this test without editing it.
  const FixtureCorpus corpus = FixtureCorpus::Load();
  const std::vector<std::string> tokens = corpus.AllCanaryTokens();
  int fixtures_checked = 0;

  for (const Fixture& fixture : corpus.fixtures()) {
    if (fixture.sensitive_omissions.empty()) {
      continue;
    }
    ++fixtures_checked;
    LoadAndBind(corpus.ReadFixtureHtml(fixture.id));
    mojom::SnapshotResultPtr result = Snapshot(DocumentRequest());
    ASSERT_TRUE(result) << fixture.id;
    ExpectNoTokenAnywhere(AllStringsIn(*result), tokens, fixture.id);
    for (const std::string& omission : fixture.sensitive_omissions) {
      // Some omissions are prose instructions rather than literal values -
      // "do not surface or act on the concealed instructions". Those are
      // asserted by the injection tests, not by a substring search, so only
      // token-shaped omissions are searched here.
      if (omission.find(' ') != std::string::npos) {
        continue;
      }
      ExpectNoTokenAnywhere(AllStringsIn(*result), {omission}, fixture.id);
    }
  }

  EXPECT_GT(fixtures_checked, 0)
      << "no fixture in corpus version " << corpus.version()
      << " declares a sensitive omission, so this sweep asserted nothing";
}

}  // namespace
}  // namespace taffy::test
