// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/field_redaction.h"

#include <string>

#include "taffy/renderer/challenge_classifier.h"
#include "taffy/renderer/observation_limits.h"
#include "testing/gtest/include/gtest/gtest.h"

// Protocol section 9. The property under test is not "the classifier is
// accurate" - it will not always be - but "the classifier can only ever be
// wrong in the safe direction", which is a property a test can actually hold.
//
// The one exception is deliberate and is tested for explicitly: an aggressive
// hint list that matched raw substrings would classify a delivery address as
// a passcode field, because "shipping" contains "pin". Being wrong in the
// safe direction is not free - it deletes the answer the research workflow
// wanted - so the tokeniser has its own tests.
//
// The bounded detectors that run over free text are a different question with
// different evidence, and they have their own file:
// high_risk_pattern_detector_unittest.cc.

namespace taffy {
namespace {

const ObservationLimits& Limits() {
  return ObservationLimits::ProcessSafeCeiling();
}

FieldDescriptor OrdinaryField() {
  FieldDescriptor field;
  field.control_type = "search";
  field.authored_name = "search";
  field.autocomplete_token = "off";
  return field;
}

TEST(ProhibitedValueFilterTest, PasswordTypeIsProhibited) {
  const ProhibitedValueFilter filter(Limits());
  FieldDescriptor field = OrdinaryField();
  field.control_type = "password";
  EXPECT_EQ(filter.CategoryOf(field), ProhibitedCategory::kPassword);
}

TEST(ProhibitedValueFilterTest, BlinkPasswordSignalOverridesAuthoredType) {
  // A page that disguises a password field as type=text is the whole reason
  // this signal exists, and it is checked before any page-authored one.
  const ProhibitedValueFilter filter(Limits());
  FieldDescriptor field = OrdinaryField();
  field.control_type = "text";
  field.blink_reports_password_field = true;
  EXPECT_EQ(filter.CategoryOf(field), ProhibitedCategory::kPassword);
}

TEST(ProhibitedValueFilterTest, PasswordManagerSuggestionIsProhibited) {
  // Section 9.1 names password-manager suggestions specifically. A suggestion
  // is a secret whether or not the field currently holds one.
  const ProhibitedValueFilter filter(Limits());
  FieldDescriptor field = OrdinaryField();
  field.has_password_manager_suggestion = true;
  EXPECT_EQ(filter.CategoryOf(field),
            ProhibitedCategory::kPasswordManagerSuggestion);
}

TEST(ProhibitedValueFilterTest, CredentialAffordanceIsProhibited) {
  const ProhibitedValueFilter filter(Limits());
  FieldDescriptor field = OrdinaryField();
  field.has_credential_manager_affordance = true;
  EXPECT_EQ(filter.CategoryOf(field),
            ProhibitedCategory::kPasskeyOrPrivateKey);
}

TEST(ProhibitedValueFilterTest, EverySection91CategoryIsReachable) {
  // One case per category protocol section 9.1 lists, so that deleting a hint
  // list by accident fails a test rather than silently widening what BIP
  // emits.
  const ProhibitedValueFilter filter(Limits());
  struct Case {
    const char* authored_name;
    ProhibitedCategory category;
  };
  const Case cases[] = {
      {"current_password", ProhibitedCategory::kPassword},
      {"unlock_passcode", ProhibitedCategory::kPasscodeOrPin},
      {"card_cvv", ProhibitedCategory::kCardSecurityCode},
      {"one_time_code", ProhibitedCategory::kOneTimeCode},
      {"recovery_code", ProhibitedCategory::kRecoveryCode},
      {"private_key", ProhibitedCategory::kPasskeyOrPrivateKey},
      {"seed_phrase", ProhibitedCategory::kSeedPhrase},
      {"access_token", ProhibitedCategory::kAuthenticationToken},
      {"session_cookie", ProhibitedCategory::kSessionCookie},
  };
  for (const Case& test_case : cases) {
    FieldDescriptor field = OrdinaryField();
    field.authored_name = test_case.authored_name;
    EXPECT_EQ(filter.CategoryOf(field), test_case.category)
        << test_case.authored_name;
  }
}

TEST(ProhibitedValueFilterTest, HintsMatchTokensNotSubstrings) {
  // The regression this exists for: "shipping" contains the letters "pin".
  // A substring match would emit a delivery address as a credential
  // placeholder and delete the field the research workflow wanted. Being
  // conservative is not an excuse for being wrong.
  const ProhibitedValueFilter filter(Limits());
  for (const char* name : {"shipping", "shipping_address", "shippingCity",
                           "pinterest_handle", "coupon", "description",
                           "quantity", "email", "username", "cardnumber",
                           "firstName"}) {
    FieldDescriptor field = OrdinaryField();
    field.authored_name = name;
    EXPECT_EQ(filter.CategoryOf(field), ProhibitedCategory::kNone) << name;
  }
}

TEST(ProhibitedValueFilterTest, FormSemanticsAreASignalOfTheirOwn) {
  // A control called "code" means something different inside a form called
  // "two-factor" than inside one called "coupon". Section 9.2 lists form type
  // as a signal in its own right, so a form-level hint has to be enough on
  // its own.
  const ProhibitedValueFilter filter(Limits());
  FieldDescriptor field = OrdinaryField();
  field.authored_name = "code";
  field.control_type = "text";
  EXPECT_EQ(filter.CategoryOf(field), ProhibitedCategory::kNone);

  field.form_semantics = "one-time-code confirmation";
  EXPECT_EQ(filter.CategoryOf(field), ProhibitedCategory::kOneTimeCode);
}

TEST(ProhibitedValueFilterTest, HiddenTokenFieldsAreProhibited) {
  // A hidden control whose name mentions a token or a state parameter is the
  // shape of a cross-site request forgery token. The sensitive-form fixture
  // requires the hidden token's value never to be emitted.
  const ProhibitedValueFilter filter(Limits());
  FieldDescriptor field = OrdinaryField();
  field.control_type = "hidden";
  field.authored_name = "request_signature";
  field.hidden = true;
  EXPECT_EQ(filter.CategoryOf(field),
            ProhibitedCategory::kAuthenticationToken);
}

TEST(ProhibitedValueFilterTest, ScopedAutocompleteTokensStillMatch) {
  // Authored autocomplete can carry section and billing scopes ahead of the
  // field name. Comparing only the whole attribute would miss every one.
  const ProhibitedValueFilter filter(Limits());
  FieldDescriptor field = OrdinaryField();
  field.autocomplete_token = "section-blue billing cc-csc";
  EXPECT_EQ(filter.CategoryOf(field), ProhibitedCategory::kCardSecurityCode);
}

TEST(ProhibitedValueFilterTest, PlaceholderCarriesNoValueAtAll) {
  const ProhibitedValueFilter filter(Limits());
  FieldDescriptor field = OrdinaryField();
  field.control_type = "password";

  const SemanticNode node = filter.BuildStructuralPlaceholder(
      SemanticNodeId("n1"), FrameId("frame_main"), field);

  EXPECT_EQ(node.sensitivity, Sensitivity::kCredential);
  EXPECT_FALSE(node.name.has_value());
  EXPECT_FALSE(node.description.has_value());
  EXPECT_TRUE(node.text_runs.empty());
  ASSERT_TRUE(node.value_descriptor.has_value());
  EXPECT_EQ(node.value_descriptor->kind, ValueKind::kSecretWithheld);
  // False, not "whatever the control has". Emptiness is a fact about a
  // secret, and this code never looked (protocol section 9.1).
  EXPECT_FALSE(node.value_descriptor->present);
  EXPECT_FALSE(node.value_descriptor->normalized_value.has_value());
  EXPECT_TRUE(node.actions.empty());
  EXPECT_FALSE(node.destination.has_value());
}

TEST(ChallengeClassifierTest, OneTimeCodeCategoryIsExact) {
  FieldDescriptor field = OrdinaryField();
  field.control_type = "text";
  field.autocomplete_token = "one-time-code";

  ChallengeSignals signals;
  signals.has_image = true;
  signals.has_regenerate_or_audio_affordance = true;
  signals.has_embedded_widget = true;
  signals.text_entry_controls = 1;

  EXPECT_EQ(ClassifyChallenge(field, ProhibitedCategory::kOneTimeCode,
                              signals),
            ChallengeKind::kOneTimeCode);
}

TEST(ChallengeClassifierTest, CredentialMaterialNeverUpgradesFromNearbyImage) {
  FieldDescriptor field = OrdinaryField();
  field.control_type = "password";

  ChallengeSignals signals;
  signals.has_image = true;
  signals.has_regenerate_or_audio_affordance = true;
  signals.has_embedded_widget = true;
  signals.text_entry_controls = 1;

  EXPECT_EQ(ClassifyChallenge(field, ProhibitedCategory::kPassword, signals),
            ChallengeKind::kNone);
}

TEST(ChallengeClassifierTest, ImageNeedsImageAffordanceAndExactlyOneEntry) {
  FieldDescriptor field = OrdinaryField();
  field.control_type = "text";

  ChallengeSignals signals;
  EXPECT_EQ(ClassifyChallenge(field, ProhibitedCategory::kNone, signals),
            ChallengeKind::kNone);

  signals.has_image = true;
  signals.text_entry_controls = 1;
  EXPECT_EQ(ClassifyChallenge(field, ProhibitedCategory::kNone, signals),
            ChallengeKind::kNone);

  signals.has_regenerate_or_audio_affordance = true;
  EXPECT_EQ(ClassifyChallenge(field, ProhibitedCategory::kNone, signals),
            ChallengeKind::kImage);

  signals.text_entry_controls = 2;
  EXPECT_EQ(ClassifyChallenge(field, ProhibitedCategory::kNone, signals),
            ChallengeKind::kNone);
}

// A picture whose own label names a CAPTCHA is the same evidence as a refresh
// button beside it, and needs the same one field (decision 0193).
TEST(ChallengeClassifierTest, AnImageThatNamesACaptchaIsEvidenceToo) {
  FieldDescriptor field = OrdinaryField();
  field.control_type = "text";

  ChallengeSignals signals;
  signals.has_image = true;
  signals.image_names_a_challenge = true;
  signals.text_entry_controls = 1;
  EXPECT_EQ(ClassifyChallenge(field, ProhibitedCategory::kNone, signals),
            ChallengeKind::kImage);

  signals.text_entry_controls = 2;
  EXPECT_EQ(ClassifyChallenge(field, ProhibitedCategory::kNone, signals),
            ChallengeKind::kNone);

  signals.text_entry_controls = 1;
  EXPECT_EQ(ClassifyChallenge(field, ProhibitedCategory::kPassword, signals),
            ChallengeKind::kNone);
}

TEST(ChallengeClassifierTest, EmbeddedWidgetRequiresPersonInteraction) {
  FieldDescriptor field = OrdinaryField();
  field.control_type = "text";

  ChallengeSignals signals;
  signals.has_embedded_widget = true;
  signals.text_entry_controls = 1;

  EXPECT_EQ(ClassifyChallenge(field, ProhibitedCategory::kNone, signals),
            ChallengeKind::kInteractive);

  signals.text_entry_controls = 2;
  EXPECT_EQ(ClassifyChallenge(field, ProhibitedCategory::kNone, signals),
            ChallengeKind::kNone);

  signals.text_entry_controls = 1;
  signals.structure_complete = false;
  EXPECT_EQ(ClassifyChallenge(field, ProhibitedCategory::kNone, signals),
            ChallengeKind::kNone);
}

TEST(ChallengeClassifierTest, OrdinaryAndNonTextControlsCarryNoHint) {
  FieldDescriptor ordinary = OrdinaryField();
  ordinary.control_type = "text";
  EXPECT_EQ(ClassifyChallenge(ordinary, ProhibitedCategory::kNone,
                              ChallengeSignals{}),
            ChallengeKind::kNone);

  FieldDescriptor button = OrdinaryField();
  button.control_type = "button";
  ChallengeSignals signals;
  signals.has_image = true;
  signals.has_regenerate_or_audio_affordance = true;
  signals.text_entry_controls = 1;
  EXPECT_EQ(ClassifyChallenge(button, ProhibitedCategory::kNone, signals),
            ChallengeKind::kNone);
}

TEST(SensitivityClassifierTest, PageTextCanOnlyRaiseClassification) {
  const SensitivityClassifier classifier;
  const ProhibitedValueFilter filter(Limits());
  FieldDescriptor field = OrdinaryField();
  const Sensitivity baseline =
      classifier.Classify(field, filter.CategoryOf(field));
  EXPECT_EQ(baseline, Sensitivity::kNotSensitive);

  // A page claiming its card field is ordinary does not make it ordinary.
  field.authored_name = "creditcard";
  const Sensitivity raised =
      classifier.Classify(field, filter.CategoryOf(field));
  EXPECT_EQ(raised, Sensitivity::kPayment);
  EXPECT_EQ(SensitivityClassifier::Stricter(baseline, raised), raised);
}

TEST(SensitivityClassifierTest, SpecificEvidenceBeatsTheUnknownDefault) {
  // kUnknownSensitive ranks strictest for a ceiling comparison, and using
  // that same rank to accumulate evidence would swallow every positive
  // classification: a payment field with an unrecognised autocomplete token
  // would come out "unknown", and every later layer would see a graph in
  // which nothing is classified.
  const SensitivityClassifier classifier;
  const ProhibitedValueFilter filter(Limits());
  FieldDescriptor field;
  field.control_type = "text";
  field.autocomplete_token = "not-a-real-token";
  field.authored_name = "billing_card_number";
  EXPECT_EQ(classifier.Classify(field, filter.CategoryOf(field)),
            Sensitivity::kPayment);
}

TEST(SensitivityClassifierTest, UnclassifiableIsUnknownSensitive) {
  const SensitivityClassifier classifier;
  const ProhibitedValueFilter filter(Limits());
  FieldDescriptor field;
  field.control_type = "text";
  field.authored_name = "field3";
  EXPECT_EQ(classifier.Classify(field, filter.CategoryOf(field)),
            Sensitivity::kUnknownSensitive);
}

TEST(SensitivityClassifierTest, AnAutofillPreferenceCannotConcludeOrdinary) {
  // The regression this exists for, and it is a fail-open rather than a gap:
  // `autocomplete="off"` is a storage preference, not a statement about
  // content, and it is exactly what a careful government or banking form puts
  // on its most sensitive field. While it sat in the ordinary-autocomplete
  // list, the two tokens most correlated with identity material were the two
  // that concluded kNotSensitive - and MayObserveValue then read the value out
  // of the page.
  const SensitivityClassifier classifier;
  const ProhibitedValueFilter filter(Limits());
  for (const char* token : {"off", "on"}) {
    FieldDescriptor field;
    field.control_type = "text";
    field.authored_name = "field3";
    field.autocomplete_token = token;
    const Sensitivity classified =
        classifier.Classify(field, filter.CategoryOf(field));
    EXPECT_EQ(classified, Sensitivity::kUnknownSensitive) << token;
    EXPECT_FALSE(MayObserveValue(classified)) << token;
  }
}

TEST(SensitivityClassifierTest, AnIdentifierThisBuildCannotNameIsNotOrdinary) {
  // The property that has to hold whatever is in the hint list, because the
  // hint list can only ever be incomplete: a national scheme nobody here has
  // heard of, on a text control, on a form that turned autofill off. This is
  // the eAadhaar virtual id - "vid" is deliberately absent from the identity
  // vocabulary under clause 4 of decision 0061, and the answer is still safe.
  const SensitivityClassifier classifier;
  const ProhibitedValueFilter filter(Limits());
  for (const char* name : {"vid", "uid", "eid", "rrn", "cbu", "field3"}) {
    FieldDescriptor field;
    field.control_type = "text";
    field.authored_name = name;
    field.autocomplete_token = "off";
    const Sensitivity classified =
        classifier.Classify(field, filter.CategoryOf(field));
    EXPECT_EQ(classified, Sensitivity::kUnknownSensitive) << name;
    EXPECT_FALSE(MayObserveValue(classified)) << name;
  }
}

TEST(SensitivityClassifierTest, IdentityMaterialIsNotOnlyAnglophone) {
  // One case per region the vocabulary claims to cover, so that deleting a
  // block of it fails a test rather than silently returning most of the world
  // to "this is ordinary". The list had none of these: an Aadhaar number was
  // not identity material at all, and every redaction layer above this one was
  // working from that verdict.
  const SensitivityClassifier classifier;
  const ProhibitedValueFilter filter(Limits());
  for (const char* name : {
           "aadhaar_number", "aadhar", "uidai_reference", "pan_card",
           "voter_id", "tc_kimlik", "emirates_id", "iqama_number", "nric",
           "mykad", "philsys_id", "my_number", "cpf", "curp", "itin",
           "social_insurance_number", "codice_fiscale", "national_insurance",
           "bsn", "personnummer", "snils", "nin", "national_identity",
           "residence_permit", "driving_licence",
       }) {
    FieldDescriptor field;
    field.control_type = "text";
    field.authored_name = name;
    EXPECT_EQ(classifier.Classify(field, filter.CategoryOf(field)),
              Sensitivity::kIdentity)
        << name;
  }
}

TEST(SensitivityClassifierTest, TheIdentityVocabularyStillMatchesTokensOnly) {
  // Clause 4 of decision 0061 exists so that widening the list does not start
  // deleting ordinary answers. A certificate page's "fingerprint" and a
  // cookware page's "pan" are the two the rule was written against.
  const SensitivityClassifier classifier;
  const ProhibitedValueFilter filter(Limits());
  for (const char* name : {"certificate_fingerprint", "pan", "pan_size",
                           "tin_thickness", "dining_table"}) {
    FieldDescriptor field;
    field.control_type = "text";
    field.authored_name = name;
    EXPECT_NE(classifier.Classify(field, filter.CategoryOf(field)),
              Sensitivity::kIdentity)
        << name;
  }
}

TEST(SensitivityClassifierTest, ContextForbidsTheOrdinaryConclusion) {
  const SensitivityClassifier classifier;
  const ProhibitedValueFilter filter(Limits());
  FieldDescriptor field = OrdinaryField();
  EXPECT_EQ(classifier.Classify(field, filter.CategoryOf(field)),
            Sensitivity::kNotSensitive);

  for (bool FieldDescriptor::*signal :
       {&FieldDescriptor::in_cross_origin_frame, &FieldDescriptor::obscured,
        &FieldDescriptor::hidden, &FieldDescriptor::insecure_context}) {
    FieldDescriptor flagged = OrdinaryField();
    flagged.*signal = true;
    EXPECT_EQ(classifier.Classify(flagged, filter.CategoryOf(flagged)),
              Sensitivity::kUnknownSensitive);
  }
}

TEST(SensitivityClassifierTest, ContextDoesNotDestroySpecificity) {
  const SensitivityClassifier classifier;
  const ProhibitedValueFilter filter(Limits());
  FieldDescriptor field;
  field.control_type = "text";
  field.authored_name = "card_number";
  field.in_cross_origin_frame = true;
  EXPECT_EQ(classifier.Classify(field, filter.CategoryOf(field)),
            Sensitivity::kPayment);
}

TEST(SensitivityClassifierTest, BrowserPolicyFloorOnlyRaises) {
  const SensitivityClassifier classifier;
  const ProhibitedValueFilter filter(Limits());
  FieldDescriptor field = OrdinaryField();
  field.policy_floor = Sensitivity::kUnknownSensitive;
  EXPECT_EQ(classifier.Classify(field, filter.CategoryOf(field)),
            Sensitivity::kUnknownSensitive);

  // A floor cannot make an already stricter field looser.
  FieldDescriptor card;
  card.control_type = "text";
  card.authored_name = "card_number";
  card.policy_floor = Sensitivity::kAccount;
  EXPECT_EQ(classifier.Classify(card, filter.CategoryOf(card)),
            Sensitivity::kPayment);
}

TEST(SensitivityClassifierTest, ContentRegionsUseTheSameOneWayRule) {
  const SensitivityClassifier classifier;
  EXPECT_EQ(classifier.ClassifyContentRegion("Product details", false,
                                             Sensitivity::kNotSensitive),
            Sensitivity::kNotSensitive);
  EXPECT_EQ(classifier.ClassifyContentRegion("Payment details", false,
                                             Sensitivity::kNotSensitive),
            Sensitivity::kPayment);
  EXPECT_EQ(classifier.ClassifyContentRegion("Product details", true,
                                             Sensitivity::kNotSensitive),
            Sensitivity::kUnknownSensitive);
}

// A control's own name is not classified by its words (decision 0186): the
// myAadhaar portal's "Download Aadhaar" link was withheld for saying
// "Aadhaar". A cross-origin frame and the browser's floor still raise it, and
// a passage keeps the word rule.
TEST(SensitivityClassifierTest, AControlNameIsNotClassifiedByItsWords) {
  const SensitivityClassifier classifier;
  EXPECT_EQ(classifier.ClassifyControlName(false, Sensitivity::kNotSensitive),
            Sensitivity::kNotSensitive);
  EXPECT_EQ(classifier.ClassifyControlName(true, Sensitivity::kNotSensitive),
            Sensitivity::kUnknownSensitive);
  EXPECT_EQ(classifier.ClassifyControlName(false, Sensitivity::kFinancial),
            Sensitivity::kFinancial);
  EXPECT_EQ(classifier.ClassifyContentRegion("Download Aadhaar", false,
                                             Sensitivity::kNotSensitive),
            Sensitivity::kIdentity);
}

TEST(MayObserveValueTest, OnlyInsensitiveValuesMayBeObserved) {
  EXPECT_TRUE(MayObserveValue(Sensitivity::kNotSensitive));
  for (Sensitivity sensitivity :
       {Sensitivity::kUnknownSensitive, Sensitivity::kPersonal,
        Sensitivity::kAccount, Sensitivity::kPayment, Sensitivity::kIdentity,
        Sensitivity::kHealth, Sensitivity::kFinancial, Sensitivity::kLegal,
        Sensitivity::kPrivateCommunication, Sensitivity::kAdministration,
        Sensitivity::kCredential}) {
    EXPECT_FALSE(MayObserveValue(sensitivity));
  }
}

TEST(MayEmitTextTest, ValuesLabelsAndContentAreThreeDifferentQuestions) {
  // A payment form's "Card number" label is not the card number. Treating
  // them as one question produced a schema with no labels in it, which is not
  // a schema.
  EXPECT_FALSE(
      MayEmitText(Sensitivity::kPayment, NodeTextClass::kControlValue));
  EXPECT_TRUE(MayEmitText(Sensitivity::kPayment, NodeTextClass::kControlLabel));
  EXPECT_TRUE(MayEmitText(Sensitivity::kPayment, NodeTextClass::kContent));

  // Credential material is the one class that travels in no form at all.
  EXPECT_FALSE(
      MayEmitText(Sensitivity::kCredential, NodeTextClass::kControlValue));
  EXPECT_FALSE(
      MayEmitText(Sensitivity::kCredential, NodeTextClass::kControlLabel));
  EXPECT_FALSE(MayEmitText(Sensitivity::kCredential, NodeTextClass::kContent));
}

}  // namespace
}  // namespace taffy
