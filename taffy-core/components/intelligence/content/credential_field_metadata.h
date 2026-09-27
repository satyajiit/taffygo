// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_CREDENTIAL_FIELD_METADATA_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_CREDENTIAL_FIELD_METADATA_H_

#include <stdint.h>

#include <type_traits>

#include "taffy/components/intelligence/content/observability_recorder.h"

// The boundary type that makes a credential value unrepresentable on the AI
// side (PAR-AUTH-001, PAR-AI-BR-004, REQ-SEC-001, threat model invariant 4).
//
// The requirement is absolute: password, one-time code, card security code,
// personal identification number, passkey assertion, authentication token,
// private key and seed phrase values never enter model context, audit records,
// analytics, crash reports or workspace data. The renderer's field redaction
// (//taffy/renderer/field_redaction.h) omits the value before it is
// ever serialized, which is the first line. This file is the second, and it
// works differently: instead of removing the value, it removes the *place a
// value could go*.
//
// Two devices do that.
//
// 1. **CredentialFieldMetadata is trivially copyable.** It can hold
//    identifiers, an enum and a boolean, and it cannot hold a std::string, a
//    std::vector or any other owning member. There is no way to add "just the
//    masked value, for debugging" to it: that is a compile error naming this
//    rule, not a review comment somebody can argue with.
//
// 2. **CredentialValue is declared and never defined.** Everything downstream
//    of the browser process — the sandboxed core service, model request
//    builders, audit engine and Android facade — reaches //taffy through
//    //taffy/common/public, and no header there mentions a credential
//    value at all. This incomplete type exists here so that a reader looking
//    for the representation finds this paragraph instead of writing one. An
//    incomplete type cannot be stored by value, copied, serialized or
//    inspected; only a reference to it can exist, and a reference to something
//    with no definition can do nothing.
//
// What is deliberately absent from the metadata, and why:
//
//   * the value, in any form, including hashed, masked, truncated or
//     length-prefixed. A hash of a password is a password oracle for anything
//     with a word list.
//   * the value's length. It narrows the search space and buys a diagnostic
//     almost nothing.
//   * the field's label, placeholder or accessible name. Those are
//     page-controlled strings, and a page that wants its label in a log can
//     write anything into one.

namespace taffy {

// Declared, never defined. See the second device above.
class CredentialValue;

// The sensitivity classes REQ-SEC-001 enumerates, plus the classes the fixture
// corpus seeds canaries for (test-fixtures/web/manifest.json). One list, so
// that the renderer, the browser and the seeded-secret suite cannot disagree
// about what counts.
enum class CredentialClass : uint8_t {
  // Fail closed: an unclassified sensitive field is treated as the strictest
  // class by every consumer, never as ordinary text.
  kUnknown = 0,
  kPassword = 1,
  kOneTimeCode = 2,
  kCardSecurityCode = 3,
  kPersonalIdentificationNumber = 4,
  kPasskeyAssertion = 5,
  kAuthenticationToken = 6,
  kSessionToken = 7,
  kApiKey = 8,
  kPrivateKey = 9,
  kSeedPhrase = 10,
  kRecoveryCode = 11,
  // PAR-AUTH-006: the assistant pauses. Solving or bypassing is a permanent
  // product prohibition, so this class exists to be refused, never handled.
  kCaptchaAnswer = 12,
};

// Everything the browser process is allowed to know about a credential field.
struct CredentialFieldMetadata {
  RecordIdentifier tab_id;
  RecordIdentifier frame_id;
  // Stable within one frame and page epoch, so a field can be spoken about
  // without being read.
  RecordIdentifier node_id;

  CredentialClass credential_class = CredentialClass::kUnknown;

  // True when the field currently holds something. Not how much, not what.
  bool value_is_present = false;

  // True when the user, not the assistant, put it there. The assistant has no
  // capability to fill a credential field before the M5 exit review and the
  // design is [Open (OD-056)]; this field records the fact for the audit
  // trail, and a true value from any other source is a defect.
  bool filled_by_user = false;
};

static_assert(std::is_trivially_copyable_v<CredentialFieldMetadata>,
              "Credential field metadata carries identifiers, an enum and "
              "booleans only. An owning member would create a place for a "
              "credential value to sit, which REQ-SEC-001 forbids "
              "unconditionally.");

// True for the classes that must additionally suspend assistant page access
// while the user is interacting with them. Every class qualifies today; the
// function exists so that a future exception has one place to be argued for
// and reviewed, rather than being sprinkled across call sites.
constexpr bool CredentialClassSuspendsAssistantAccess(CredentialClass value) {
  switch (value) {
    case CredentialClass::kUnknown:
    case CredentialClass::kPassword:
    case CredentialClass::kOneTimeCode:
    case CredentialClass::kCardSecurityCode:
    case CredentialClass::kPersonalIdentificationNumber:
    case CredentialClass::kPasskeyAssertion:
    case CredentialClass::kAuthenticationToken:
    case CredentialClass::kSessionToken:
    case CredentialClass::kApiKey:
    case CredentialClass::kPrivateKey:
    case CredentialClass::kSeedPhrase:
    case CredentialClass::kRecoveryCode:
    case CredentialClass::kCaptchaAnswer:
      return true;
  }
}

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_CREDENTIAL_FIELD_METADATA_H_
