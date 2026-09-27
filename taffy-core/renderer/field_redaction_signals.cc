// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/field_redaction_signals.h"

#include <algorithm>
#include <array>

#include "base/strings/string_split.h"
#include "base/strings/string_util.h"

namespace taffy::field_redaction_signals {

namespace {

// --- signal vocabularies ----------------------------------------------------
//
// Every list below is matched against TOKENS, never against a raw substring.
// That distinction is not cosmetic: "shipping" contains the letters "pin",
// and a substring match would classify a delivery address as a passcode
// field, emit it as a structural placeholder, and quietly delete the one
// piece of information the research workflow actually wanted. Tokenising
// first is what makes an aggressive hint list safe to have.

// Single tokens that name a secret outright.
constexpr auto kStorageProhibitedTokenHints = std::to_array<CategoryHint>({
    {"password", ProhibitedCategory::kPassword},
    {"passwd", ProhibitedCategory::kPassword},
    {"pwd", ProhibitedCategory::kPassword},
    {"passphrase", ProhibitedCategory::kPassword},
    {"passcode", ProhibitedCategory::kPasscodeOrPin},
    {"pin", ProhibitedCategory::kPasscodeOrPin},
    {"cvv", ProhibitedCategory::kCardSecurityCode},
    {"cvc", ProhibitedCategory::kCardSecurityCode},
    {"csc", ProhibitedCategory::kCardSecurityCode},
    {"cid", ProhibitedCategory::kCardSecurityCode},
    {"otp", ProhibitedCategory::kOneTimeCode},
    {"totp", ProhibitedCategory::kOneTimeCode},
    {"2fa", ProhibitedCategory::kOneTimeCode},
    {"mfa", ProhibitedCategory::kOneTimeCode},
    {"mnemonic", ProhibitedCategory::kSeedPhrase},
    {"passkey", ProhibitedCategory::kPasskeyOrPrivateKey},
    {"webauthn", ProhibitedCategory::kPasskeyOrPrivateKey},
    {"apikey", ProhibitedCategory::kAuthenticationToken},
    {"jwt", ProhibitedCategory::kAuthenticationToken},
    {"bearer", ProhibitedCategory::kAuthenticationToken},
    {"csrf", ProhibitedCategory::kAuthenticationToken},
    {"xsrf", ProhibitedCategory::kAuthenticationToken},
    {"nonce", ProhibitedCategory::kAuthenticationToken},
    {"cookie", ProhibitedCategory::kSessionCookie},
    {"sessionid", ProhibitedCategory::kSessionCookie},
    {"jsessionid", ProhibitedCategory::kSessionCookie},
});

// Two- and three-token phrases. Checked against adjacent token joins so that
// "seed phrase", "seed_phrase", and "seedPhrase" are one hint rather than
// three, without letting a phrase match across unrelated words.
constexpr auto kStorageProhibitedPhraseHints = std::to_array<CategoryHint>({
    {"currentpassword", ProhibitedCategory::kPassword},
    {"newpassword", ProhibitedCategory::kPassword},
    {"confirmpassword", ProhibitedCategory::kPassword},
    {"seedphrase", ProhibitedCategory::kSeedPhrase},
    {"recoveryphrase", ProhibitedCategory::kSeedPhrase},
    {"secretphrase", ProhibitedCategory::kSeedPhrase},
    {"recoverycode", ProhibitedCategory::kRecoveryCode},
    {"backupcode", ProhibitedCategory::kRecoveryCode},
    {"onetimecode", ProhibitedCategory::kOneTimeCode},
    {"onetimepassword", ProhibitedCategory::kOneTimeCode},
    {"onetimepasscode", ProhibitedCategory::kOneTimeCode},
    {"verificationcode", ProhibitedCategory::kOneTimeCode},
    {"securitycode", ProhibitedCategory::kCardSecurityCode},
    {"cardsecuritycode", ProhibitedCategory::kCardSecurityCode},
    {"privatekey", ProhibitedCategory::kPasskeyOrPrivateKey},
    {"secretkey", ProhibitedCategory::kPasskeyOrPrivateKey},
    {"signingkey", ProhibitedCategory::kPasskeyOrPrivateKey},
    {"apikey", ProhibitedCategory::kAuthenticationToken},
    {"accesstoken", ProhibitedCategory::kAuthenticationToken},
    {"refreshtoken", ProhibitedCategory::kAuthenticationToken},
    {"idtoken", ProhibitedCategory::kAuthenticationToken},
    {"bearertoken", ProhibitedCategory::kAuthenticationToken},
    {"clientsecret", ProhibitedCategory::kAuthenticationToken},
    {"integrationkey", ProhibitedCategory::kAuthenticationToken},
    {"sessiontoken", ProhibitedCategory::kSessionCookie},
    {"sessioncookie", ProhibitedCategory::kSessionCookie},
    {"authtoken", ProhibitedCategory::kAuthenticationToken},
    {"securityanswer", ProhibitedCategory::kRecoveryCode},
});

// The `autocomplete` tokens the platform itself defines for secrets. These
// are exact, authored values, so they are compared whole.
constexpr auto kStorageProhibitedAutocompleteTokens = std::to_array<CategoryHint>({
    {"current-password", ProhibitedCategory::kPassword},
    {"new-password", ProhibitedCategory::kPassword},
    {"one-time-code", ProhibitedCategory::kOneTimeCode},
    {"cc-csc", ProhibitedCategory::kCardSecurityCode},
    {"webauthn", ProhibitedCategory::kPasskeyOrPrivateKey},
});

constexpr auto kStorageProhibitedControlTypes = std::to_array<CategoryHint>({
    {"password", ProhibitedCategory::kPassword},
});

// Sensitivity vocabularies. Same tokenised matching.
constexpr auto kStorageFinancialHints = std::to_array<std::string_view>({
    "iban", "swift", "routing", "sortcode", "balance", "accountnumber",
    "bankaccount", "invoice", "payout", "salary",
});

constexpr auto kStoragePaymentHints = std::to_array<std::string_view>({
    "ccnumber", "ccexp", "ccname", "cctype", "ccnumber", "creditcard",
    "cardnumber", "cardholder", "debitcard", "billing", "billingaddress",
    "payment",
});

// Identity material: an identifier that proves who somebody is.
//
// THE RULE FOR ADDING TO THIS LIST (decision 0061). A token belongs here when
// all four of these hold, and it is left out when any one of them fails:
//
//   1. ISSUER — a government or a national authority issues it, rather than a
//      shop, a bank, or a site. A loyalty number is kAccount and a bank
//      account number is kFinancial; neither is here.
//   2. SUBJECT — it names one natural person, not a company or a household.
//   3. PURPOSE — it is used to prove identity, rather than merely to describe
//      a person. A given name is kPersonal; a passport number is here.
//   4. DISTINCTNESS — the token is not an ordinary word and not a common
//      field-name abbreviation. "fingerprint" fails this one and is
//      deliberately absent: a certificate detail page uses it for a digest,
//      and misreading that as identity material would delete a value a
//      research workflow wanted. "pan" and "tin" fail it as English words, so
//      the joined forms "pancard" and "pannumber" carry India's PAN instead.
//
// Clause 4 is safe to apply strictly, and that is the point of the design
// rather than a concession. A token this list omits leaves the field at
// kUnknownSensitive, which MayObserveValue refuses and which the fill
// allowance table treats *more* strictly than kIdentity — so an omission
// costs a person one manual entry, never a disclosure. **This list is a
// convenience for classification quality. It is not the control.**
//
// Two limits are worth knowing before trusting it. Tokenize() keeps ASCII
// alphanumerics only, so a label authored in Devanagari, Arabic, Han, or
// Cyrillic contributes no tokens at all and no list can reach it
// `[Open (OD-108)]`. And a national scheme nobody here has heard of is the
// normal case, not the exception, which is why the answer for an unknown
// field can never be "ordinary".
constexpr auto kStorageIdentityHints = std::to_array<std::string_view>({
    // Vocabulary that is not tied to one country.
    "nationalid", "nationalidentity", "identitynumber", "identitycard",
    "identitydocument", "idnumber", "passport", "passportnumber",
    "residencepermit", "birthcertificate", "taxid", "taxidentification",
    "dateofbirth", "birthdate", "bday", "biometric", "driverslicense",
    "drivinglicense", "drivinglicence", "licensenumber", "licencenumber",
    // The Americas: the United States, Canada, Brazil, Mexico.
    "ssn", "socialsecurity", "itin", "socialinsurance", "cpf", "curp",
    // Europe: Spain, Italy, Germany, the Netherlands, the Nordic countries,
    // the United Kingdom, Russia.
    "dni", "nie", "nif", "codicefiscale", "steuerid", "steuernummer",
    "personalausweis", "bsn", "personnummer", "fodselsnummer",
    "nationalinsurance", "nhsnumber", "snils",
    // South and West Asia: India, Turkey, the Gulf states.
    "aadhaar", "aadhar", "uidai", "pannumber", "pancard", "voterid",
    "emiratesid", "iqama", "tckimlik", "tckn",
    // East and South-East Asia: Japan, Korea, Indonesia, Malaysia,
    // Singapore, the Philippines.
    "mynumber", "jumin", "nik", "ktp", "mykad", "mykid", "nric", "philsys",
    // Africa: Nigeria.
    "nin",
});

constexpr auto kStorageHealthHints = std::to_array<std::string_view>({
    "diagnosis", "prescription", "medication", "patient", "symptom",
    "insurancemember", "healthplan", "allergy",
});

constexpr auto kStorageLegalHints = std::to_array<std::string_view>({
    "casenumber", "docket", "attorney", "litigation", "settlement",
});

constexpr auto kStoragePrivateCommunicationHints = std::to_array<std::string_view>({
    "message", "inbox", "thread", "directmessage", "chat", "conversation",
});

constexpr auto kStorageAdministrationHints = std::to_array<std::string_view>({
    "admin", "roleassignment", "permission", "tenant", "billingadmin",
    "audit",
});

constexpr auto kStorageContactHints = std::to_array<std::string_view>({
    "email", "phone", "tel", "mobile", "address", "street", "postal", "zip",
    "postcode", "givenname", "familyname", "fullname", "shipping",
});

constexpr auto kStorageAccountHints = std::to_array<std::string_view>({
    "username", "userid", "login", "account", "handle", "screenname",
    "customerid", "memberid",
});

// The narrow set of authored autocomplete tokens that positively state a
// field is ordinary. Everything outside it leaves the classification at
// kUnknownSensitive, which is the conservative default.
//
// THE RULE FOR ADDING TO THIS LIST (decision 0061): a token qualifies only if
// it names WHAT THE FIELD HOLDS, and what it names cannot be sensitive. A
// token that expresses a browser preference names nothing about the content
// and does not qualify.
//
// "off" and "on" used to be here and are the reason the rule is written down.
// They are autofill storage preferences, not content classes — and "off" is
// what a careful government or banking form puts on its most sensitive field
// precisely to stop the browser retaining it. So the two tokens most
// correlated with identity material were the two that concluded "ordinary",
// and that conclusion is what MayObserveValue reads. An Aadhaar field
// authored `<input type="text" name="uid" autocomplete="off">` classified
// kNotSensitive and its value was readable, whatever the hint lists above
// said. That was a fail-open on page-authored text, in a classifier whose
// stated property is that page text can only raise.
constexpr auto kStorageOrdinaryAutocompleteTokens = std::to_array<std::string_view>({
    "url", "organization", "country-name", "language", "transaction-currency",
});

// Control types that carry no personal semantics of their own.
constexpr auto kStorageOrdinaryControlTypes = std::to_array<std::string_view>({
    "search", "submit", "button", "reset", "image", "color", "range",
});

}  // namespace

// --- tokenisation -----------------------------------------------------------
//
// The matchers below are the header's declared API and therefore live at
// namespace scope, not in the anonymous namespace above: only the vocabularies
// are private to this file. Defining them internally would give the classifier
// next door nothing to link against, and clang flags each one as an unused
// static function before the link ever gets a chance to complain.

// Splits authored text into lowercase alphanumeric tokens, breaking on
// punctuation, on whitespace, and on a lower-to-upper case transition so that
// "ccCsc" and "cc_csc" produce the same tokens.
std::vector<std::string> Tokenize(std::string_view text) {
  std::vector<std::string> tokens;
  std::string current;
  bool previous_was_lower_or_digit = false;
  for (char c : text) {
    if (!base::IsAsciiAlphaNumeric(c)) {
      if (!current.empty()) {
        tokens.push_back(current);
        current.clear();
      }
      previous_was_lower_or_digit = false;
      continue;
    }
    if (base::IsAsciiUpper(c) && previous_was_lower_or_digit &&
        !current.empty()) {
      tokens.push_back(current);
      current.clear();
    }
    previous_was_lower_or_digit =
        base::IsAsciiLower(c) || base::IsAsciiDigit(c);
    current.push_back(base::ToLowerASCII(c));
  }
  if (!current.empty()) {
    tokens.push_back(current);
  }
  return tokens;
}

// Every token, plus every adjacent pair and triple joined. A hint list can
// then be written the way a person says it ("recovery code") and matched
// however the page spelled it, without matching across unrelated words.
std::vector<std::string> TokensAndJoins(std::string_view text) {
  std::vector<std::string> tokens = Tokenize(text);
  const size_t base_count = tokens.size();
  for (size_t i = 0; i + 1 < base_count; ++i) {
    tokens.push_back(tokens[i] + tokens[i + 1]);
    if (i + 2 < base_count) {
      tokens.push_back(tokens[i] + tokens[i + 1] + tokens[i + 2]);
    }
  }
  return tokens;
}

bool ContainsAnyHint(const std::vector<std::string>& candidates,
                     base::span<const std::string_view> hints) {
  for (const std::string& candidate : candidates) {
    for (std::string_view hint : hints) {
      if (candidate == hint) {
        return true;
      }
    }
  }
  return false;
}

ProhibitedCategory MatchCategory(const std::vector<std::string>& candidates,
                                 base::span<const CategoryHint> hints) {
  for (const std::string& candidate : candidates) {
    for (const CategoryHint& hint : hints) {
      if (candidate == hint.token) {
        return hint.category;
      }
    }
  }
  return ProhibitedCategory::kNone;
}

// Rank used only by Stricter(). It is a local implementation detail: the
// enum's own order is documentation, and relying on enum order for a security
// comparison is the kind of coupling that breaks the day someone inserts a
// value in the middle. kUnknownSensitive ranks highest because an
// unclassified field must be handled at least as strictly as any classified
// one (protocol section 9.2).
int StrictnessRank(Sensitivity s) {
  switch (s) {
    case Sensitivity::kNotSensitive:
      return 0;
    case Sensitivity::kAccount:
      return 1;
    case Sensitivity::kPersonal:
      return 2;
    case Sensitivity::kIdentity:
      return 3;
    case Sensitivity::kLegal:
      return 4;
    case Sensitivity::kAdministration:
      return 5;
    case Sensitivity::kFinancial:
      return 6;
    case Sensitivity::kPayment:
      return 7;
    case Sensitivity::kHealth:
      return 8;
    case Sensitivity::kPrivateCommunication:
      return 9;
    case Sensitivity::kCredential:
    case Sensitivity::kOneTimeCode:
    case Sensitivity::kChallengeResponse:
      return 10;
    case Sensitivity::kUnknownSensitive:
      return 11;
  }
}

// Combines a new piece of positive evidence with what is known so far.
//
// This is NOT Stricter(). Stricter() ranks kUnknownSensitive highest, which
// is right for a ceiling comparison and wrong for accumulating evidence: once
// a signal positively says "payment field", that is more information than
// "unknown", and swallowing it would hand every downstream layer a graph in
// which nothing is ever classified. Evidence therefore replaces the unknown
// default, and after that only ever moves up the rank.
//
// It can never reach kNotSensitive from anything else, which is the encoding
// of "page instructions cannot lower sensitivity".
Sensitivity AccumulateEvidence(Sensitivity current, Sensitivity evidence) {
  if (evidence == Sensitivity::kNotSensitive) {
    return current;
  }
  if (current == Sensitivity::kNotSensitive ||
      current == Sensitivity::kUnknownSensitive) {
    return evidence;
  }
  return StrictnessRank(current) >= StrictnessRank(evidence) ? current
                                                            : evidence;
}

const base::span<const CategoryHint> kProhibitedTokenHints = kStorageProhibitedTokenHints;
const base::span<const CategoryHint> kProhibitedPhraseHints = kStorageProhibitedPhraseHints;
const base::span<const CategoryHint> kProhibitedAutocompleteTokens = kStorageProhibitedAutocompleteTokens;
const base::span<const CategoryHint> kProhibitedControlTypes = kStorageProhibitedControlTypes;
const base::span<const std::string_view> kFinancialHints = kStorageFinancialHints;
const base::span<const std::string_view> kPaymentHints = kStoragePaymentHints;
const base::span<const std::string_view> kIdentityHints = kStorageIdentityHints;
const base::span<const std::string_view> kHealthHints = kStorageHealthHints;
const base::span<const std::string_view> kLegalHints = kStorageLegalHints;
const base::span<const std::string_view> kPrivateCommunicationHints = kStoragePrivateCommunicationHints;
const base::span<const std::string_view> kAdministrationHints = kStorageAdministrationHints;
const base::span<const std::string_view> kContactHints = kStorageContactHints;
const base::span<const std::string_view> kAccountHints = kStorageAccountHints;
const base::span<const std::string_view> kOrdinaryAutocompleteTokens = kStorageOrdinaryAutocompleteTokens;
const base::span<const std::string_view> kOrdinaryControlTypes = kStorageOrdinaryControlTypes;

}  // namespace taffy::field_redaction_signals
