// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_FIELD_REDACTION_H_
#define TAFFY_RENDERER_FIELD_REDACTION_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>

#include "base/memory/raw_ref.h"
#include "taffy/renderer/high_risk_pattern_detector.h"
#include "taffy/renderer/observation_limits.h"
#include "taffy/renderer/semantic_graph.h"

// The first of the five redaction layers in protocol section 9. This one runs
// inside the sandbox, before anything is copied out of the page, and it has
// exactly one job: a prohibited value must never be read, so that no later
// layer has to be trusted to drop it.
//
// The ordering matters more than the classification quality. A classifier
// that is 99% accurate but runs after the value has been copied into a struct
// has already lost: the value is now in renderer memory that gets serialised,
// logged, and crash-dumped. So the API here is a question you ask BEFORE
// touching the page value, and the adapters have no code path that reads a
// control's value without asking first.
//
// Everything below is host-testable. No Blink type appears in this header,
// which is why the properties a security review asks about are proved in the
// fast lane rather than only on the Chromium builder.

namespace taffy {

// The categories protocol section 9.1 names. Naming the category is safe and
// useful - "a password field is here" is explicitly permitted - while the
// value, the suggested value, the length, and any other measurement of it are
// not.
enum class ProhibitedCategory {
  kNone,
  kPassword,
  kPasscodeOrPin,
  kCardSecurityCode,
  kOneTimeCode,
  kRecoveryCode,
  kPasskeyOrPrivateKey,
  kSeedPhrase,
  kAuthenticationToken,
  kSessionCookie,
  kPasswordManagerSuggestion,
};

// Which kind of text a caller wants to emit. Sensitivity means different
// things for the three: a payment field's label is what the user reads and is
// safe, its value is not, and a paragraph inside a payment region is content
// that later layers minimize for their destination (protocol section 9.3).
enum class NodeTextClass {
  kContent,
  kControlLabel,
  kControlValue,
};

// A description of a form field assembled from Blink, with no page value in
// it. Adapters build one of these, then ask the two classes below what they
// are allowed to observe.
//
// The member list is protocol section 9.2's signal list, not the HTML input
// type: form semantics, autocomplete, role, label, origin, browser security
// context, hidden and obscured state, cross-origin embedding, and the policy
// floor the browser supplied.
struct FieldDescriptor {
  FieldDescriptor();
  FieldDescriptor(const FieldDescriptor&);
  FieldDescriptor(FieldDescriptor&&);
  FieldDescriptor& operator=(const FieldDescriptor&);
  FieldDescriptor& operator=(FieldDescriptor&&);
  ~FieldDescriptor();

  // Lowercased HTML input type or control type, e.g. "password", "text".
  std::string control_type;
  // Lowercased `autocomplete` token as authored, e.g. "cc-number".
  std::string autocomplete_token;
  // Lowercased name/id as authored. Untrusted page text: used as a signal
  // only, never echoed into a result.
  std::string authored_name;
  // Accessible label text. Same caveat.
  std::string label_text;
  // The enclosing form's own semantics - its name, its action path, its
  // autocomplete token. A control called "code" means something different
  // inside a form called "two-factor" than inside one called "coupon", and
  // section 9.2 lists form type as a signal in its own right.
  std::string form_semantics;
  // Authored ARIA role, lowercased. A role of "textbox" on a div is still a
  // text entry surface.
  std::string aria_role;

  // Set when Blink itself reports the control as a password field for
  // autofill purposes, which is a stronger signal than the authored type.
  bool blink_reports_password_field = false;
  // Set when the platform credential or password manager UI is attached to
  // this control - conditional passkey mediation, for instance.
  bool has_credential_manager_affordance = false;
  // Set when the browser's password manager has a suggestion for this field.
  // Section 9.1 names password-manager suggestions specifically, and a
  // suggestion is a secret whether or not the field currently holds one.
  bool has_password_manager_suggestion = false;

  // Set when the field is inside a cross-origin frame.
  bool in_cross_origin_frame = false;
  // Set when the page hides or obscures the control.
  bool obscured = false;
  // Set when the control is not rendered at all - `type=hidden`, or removed
  // from the box tree. Hidden fields carry tokens far more often than they
  // carry anything a user typed.
  bool hidden = false;
  // The document's security context is not a secure context. Everything on
  // such a page is treated more strictly, not less.
  bool insecure_context = false;

  // The strictest classification the browser has already decided applies to
  // this document, from organization policy or a user label. It is a floor,
  // never a ceiling: the renderer may classify something more strictly than
  // the browser did, and may never classify it less strictly.
  Sensitivity policy_floor = Sensitivity::kNotSensitive;
};

// Protocol section 9.1. The answer is a hard refusal, not a policy input: no
// setting, capability, request field, or user approval reaching the renderer
// can turn a prohibited field into a readable one, because the renderer is
// not where that decision could be safely made.
class ProhibitedValueFilter {
 public:
  explicit ProhibitedValueFilter(const ObservationLimits& limits);
  ProhibitedValueFilter(const ProhibitedValueFilter&) = delete;
  ProhibitedValueFilter& operator=(const ProhibitedValueFilter&) = delete;
  ~ProhibitedValueFilter();

  // Which section 9.1 category this field falls into, or kNone. Passwords,
  // passcodes, PINs, card verification values, one-time codes, recovery
  // codes, passkeys, private keys, seed phrases, authentication tokens,
  // session cookies, and platform credential payloads all resolve here.
  ProhibitedCategory CategoryOf(const FieldDescriptor& field) const;

  bool IsProhibited(const FieldDescriptor& field) const {
    return CategoryOf(field) != ProhibitedCategory::kNone;
  }

  // The only thing a prohibited field may contribute to the graph: a
  // structural statement that the control exists. No value, no suggested
  // value, no length, no emptiness, no hash, no character-class summary.
  // Section 9.1 calls a length-derived fingerprint out by name, and the same
  // reasoning covers every other measurement of a secret.
  //
  // The node carries ValueKind::kSecretWithheld. A one-time-code control uses
  // the narrower Sensitivity::kOneTimeCode class; every other prohibited
  // class carries Sensitivity::kCredential. Prohibition remains a fact about
  // what this code did - it did not read the value - not permission to expose
  // any class of value.
  SemanticNode BuildStructuralPlaceholder(SemanticNodeId id,
                                          FrameId frame_id,
                                          const FieldDescriptor& field) const;

  // Defence in depth for text that was collected from somewhere other than a
  // form control - an accessible name, a text run, a structured-data value -
  // where the page may have placed a secret. Delegated to
  // HighRiskPatternDetector, which owns the "what is a secret-shaped string"
  // rules; this class owns the "what is a secret-shaped field" ones. They
  // answer different questions from different evidence.
  HighRiskPatternKind DetectHighRiskPattern(std::string_view text) const;

  bool LooksLikeHighRiskIdentifier(std::string_view text) const {
    return DetectHighRiskPattern(text) != HighRiskPatternKind::kNone;
  }

  // The version of the rules in this file, stamped into every piece of
  // evidence this layer produces so a persisted observation can be told apart
  // from one made under today's rules.
  static uint32_t rule_version();

 private:
  const raw_ref<const ObservationLimits> limits_;
};

// Protocol section 9.2. Conservative by construction: an unclassifiable field
// is kUnknownSensitive, which downstream policy must treat at least as
// strictly as its destination requires.
class SensitivityClassifier {
 public:
  SensitivityClassifier();
  SensitivityClassifier(const SensitivityClassifier&) = delete;
  SensitivityClassifier& operator=(const SensitivityClassifier&) = delete;
  ~SensitivityClassifier();

  // Classification uses the whole section 9.2 signal list, not the HTML input
  // type: form semantics, autocomplete, role, authored name, label, framing,
  // hidden and obscured state, security context, and the browser's policy
  // floor. Page-authored text can only ever raise the classification. There
  // is no input to this function that produces kNotSensitive from a field
  // that any signal marked, which is the encoding of "page instructions
  // cannot lower sensitivity".
  //
  // `category` is what ProhibitedValueFilter::CategoryOf() already said about
  // this field. It is a parameter rather than a second lookup so that a
  // caller cannot classify a field it never asked the filter about: the two
  // questions are asked together or not at all.
  Sensitivity Classify(const FieldDescriptor& field,
                       ProhibitedCategory category) const;

  // The sensitivity of a region of ordinary content, from its accessible
  // context. Same one-way rule.
  Sensitivity ClassifyContentRegion(std::string_view region_label,
                                    bool in_cross_origin_frame,
                                    Sensitivity policy_floor) const;

  // The sensitivity of a link's or a button's own accessible name. That name
  // says what the control does - "Download Aadhaar", "Login", "Update
  // address" - and not what a person holds, so none of the page-word hint
  // lists applies to it; they describe what a field or a passage contains.
  // What still applies is everything that did not come from the words: a
  // cross-origin frame, the browser's policy floor, and the shape detectors
  // that drop a name carrying an identifier-shaped value (decision 0186).
  Sensitivity ClassifyControlName(bool in_cross_origin_frame,
                                  Sensitivity policy_floor) const;

  // Returns the stricter of two classifications, where kUnknownSensitive
  // counts as strictest: an unclassified field must be handled at least as
  // strictly as any classified one. Used for ceiling comparisons and when two
  // adapters disagree - disagreement resolves upward, always.
  static Sensitivity Stricter(Sensitivity a, Sensitivity b);
};

// True when a value of this sensitivity may be observed at all by the
// renderer adapters at the current milestone. Kept as a free function so the
// answer is stated once and every adapter shares it.
bool MayObserveValue(Sensitivity sensitivity);

// The one rule for whether a piece of text may leave this process, stated
// once so the adapters and the serializer cannot disagree about it.
//
// A control's value is the strictest case and matches MayObserveValue. A
// control's label is what the user reads off the screen and travels for every
// class except credential material. Content travels with its sensitivity
// marking, because protocol section 9.3 puts minimisation in the layers that
// know the destination, and the local core projection is meant to stay rich -
// but content classified as credential material does not.
bool MayEmitText(Sensitivity sensitivity, NodeTextClass text_class);

}  // namespace taffy

#endif  // TAFFY_RENDERER_FIELD_REDACTION_H_
