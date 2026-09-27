// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_SECURITY_BROWSER_VALUE_REFERENCE_VAULT_H_
#define TAFFY_COMPONENTS_SECURITY_BROWSER_VALUE_REFERENCE_VAULT_H_

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <optional>
#include <string>

#include "base/time/time.h"
#include "base/timer/timer.h"
#include "taffy/common/public/bip_budget.h"
#include "taffy/common/public/bip_identity.h"

// The browser process is the only thing in TaffyGo that ever holds a value
// destined for a form field, and this file is where it holds it.
//
// The rule the whole file exists for: **a value that enters a form field is
// minted by the browser when the person put it there.** It is never minted in
// Rust, never written into the journal, never carried in a model message, and
// never derived from page content. The assistant can say "put the value the
// person gave for the national identifier into handle 7". It cannot say what
// that value is, and it cannot read it back.
//
// Four properties carry that, and each is a mechanism rather than a promise.
//
// 1. **Only this process can mint one.** This header is in a browser-process
//    source set. It is not in //taffy/common/public, which is the entire
//    surface the sandboxed core service and the Android facade link against,
//    and //taffy/renderer's DEPS cannot reach it either. So there is no
//    translation unit outside the browser process in which the type exists -
//    "never minted in Rust" is a fact about the build graph, not a convention.
//
// 2. **A reference is bound to one task, spends on first use, and dies at its
//    deadline whether or not anybody asks.** A reference that survives its own
//    use is a value: whatever holds it can present it again, at another field,
//    in another turn, after the person stopped watching. Resolve() moves the
//    bytes out and erases the record in the same statement, so a second
//    resolution of the same reference finds nothing. The deadline is enforced
//    by the vault's own timer rather than by the next caller, because the
//    likely history of a held value is that nothing ever names it - the person
//    types their identifier, the errand goes another way, and no resolution
//    arrives to notice the record is stale.
//
// 3. **Stored credential material has no representation here at all.** See
//    FillClearance below. This is structural rather than tabular: there is no
//    value of type FillClearance naming the credential class, so Mint() cannot
//    be called for a password, passcode, card security code, personal
//    identification number, passkey assertion, recovery code, token, API key,
//    private key or seed phrase. One-time codes and challenge responses are
//    separate, person-supplied-per-use classifications with short-lived,
//    one-use clearances (decision 0088). It is not that the table says no to
//    stored credentials; the argument cannot be constructed.
//
// 4. **Nothing about a held value is observable from outside.** There is no
//    accessor for the bytes but Resolve(), and there is deliberately no
//    method returning a length, a masked form, a prefix, a character class or
//    a digest.
//
//    A digest is the one that has to be argued rather than asserted, because
//    it is the form that looks safe and is not. The values that go into form
//    fields come from tiny domains: a six-digit one-time code has a million
//    members, a card security code ten thousand, a date of birth about forty
//    thousand, a personal identification number ten thousand. A digest over a
//    domain that small **is** the value - anything holding it enumerates the
//    domain, hashes each candidate and compares, and a phone does the whole
//    million in well under a second. So no function of a held value is
//    exposed to anything, and in particular none reaches the AI data plane or
//    the durable journal, which are the two places a digest would otherwise
//    look like reasonable evidence.
//
// UI thread only.

namespace taffy {

// Permission to hold a value on behalf of a field of one classification.
//
// This is the credential invariant, expressed as a type. The member is
// private, For() is the only constructor, and For() answers nullopt for every
// classification decision 0061's fill table calls never fillable. So a caller
// holding Sensitivity::kCredential has nothing it can pass to Mint(): not a
// value it must not use, but no value at all.
//
// It mirrors `FillClearance` in
// taffy-core/components/security/core/rust/policy-engine/src/field_allowance.rs
// deliberately. Policy decides whether a class of field may ever be filled;
// this decides whether the browser may hold bytes for one. Two processes, one
// shape, and neither takes a parameter that could relax the other.
class FillClearance {
 public:
  // The classification of the field the value is for, as the person's own
  // control declared it. Never derived from the value.
  constexpr Sensitivity field_class() const { return field_class_; }

  // Answers nullopt for a classification Taffy may never carry a value for.
  //
  // The switch is exhaustive with no default arm on purpose: a classification
  // added to the protocol without an entry here fails the build rather than
  // falling through into whichever answer happened to be first.
  static constexpr std::optional<FillClearance> For(Sensitivity field_class) {
    switch (field_class) {
      // The ordinary form-assistance classes. A person retypes these into form
      // after form and none of them names them to an authority.
      case Sensitivity::kNotSensitive:
      case Sensitivity::kPersonal:
      case Sensitivity::kAccount:
      // A government identifier. Decision 0061 admits it and narrows it: the
      // person confirms each use, because the number outlives the form it is
      // typed into and cannot be reissued. That confirmation is the policy
      // engine's and the M5 surface's; holding the bytes is this file's, and
      // the two gates are independent.
      case Sensitivity::kIdentity:
      // Person-supplied, short-lived values. Policy independently requires a
      // fresh confirmation for each use; this clearance only says the browser
      // may hold the bytes briefly in its one-use vault (decision 0088).
      case Sensitivity::kOneTimeCode:
      case Sensitivity::kChallengeResponse:
        return FillClearance(field_class);

      // Never fillable, and therefore never held. The first six are closed
      // because each is a commitment, a regulated record or somebody else's
      // confidence and none of them has a design yet; the seventh is the
      // fail-safe for a field nothing classified, which is stricter than the
      // identity answer rather than weaker than it; the eighth is permanent.
      case Sensitivity::kPayment:
      case Sensitivity::kHealth:
      case Sensitivity::kFinancial:
      case Sensitivity::kLegal:
      case Sensitivity::kPrivateCommunication:
      case Sensitivity::kAdministration:
      case Sensitivity::kUnknownSensitive:
      case Sensitivity::kCredential:
        return std::nullopt;
    }
    // Fail closed on a value this build does not recognize.
    return std::nullopt;
  }

 private:
  explicit constexpr FillClearance(Sensitivity field_class)
      : field_class_(field_class) {}

  Sensitivity field_class_;
};

// The invariant, checked by the compiler rather than by a reader.
//
// Passwords, passcodes, card security codes, personal identification numbers,
// passkey assertions, recovery codes, authentication and session tokens, API
// keys, private keys and seed phrases all classify as kCredential
// (taffy-core/components/intelligence/content/credential_field_metadata.h
// enumerates them and the renderer's redaction assigns the classification).
// One-time codes deliberately do not: decision 0088 split their short-lived,
// person-supplied use from stored credentials. This assertion covers the
// permanent group and fails the build rather than a test if it gains a
// clearance.
static_assert(!FillClearance::For(Sensitivity::kCredential).has_value(),
              "Credential material is never fillable and is therefore never "
              "held. This is a permanent product prohibition, not a setting: "
              "no approval, no milestone and no configuration reaches it.");
static_assert(
    !FillClearance::For(Sensitivity::kUnknownSensitive).has_value(),
    "A field this build could not classify is completed by the person. The "
    "fail-safe is stricter than the identity answer, so an identifier scheme "
    "nobody listed costs a person one manual entry and never a disclosure.");
static_assert(FillClearance::For(Sensitivity::kOneTimeCode).has_value());
static_assert(FillClearance::For(Sensitivity::kChallengeResponse).has_value());

// Why a resolution did not produce bytes. Every member is a refusal; there is
// one success and it is the enumerator named for it.
enum class ValueResolution : uint8_t {
  kResolved = 0,
  // The field being written to may never carry a value Taffy holds. Reported
  // ahead of every other refusal, so the sentence a person reads about a
  // credential field does not appear to soften when some other condition is
  // also wrong.
  kFieldMayNotBeFilled = 1,
  // No such reference: never minted, already spent, dropped at its deadline,
  // or dropped with its task or its profile generation. One verdict for all
  // five on purpose - telling them apart would answer questions about
  // references the caller never held.
  kUnknownReference = 2,
  // Held, but for a different task. A value a person gave to one errand is not
  // available to the next one.
  kNotThisTask = 3,
  // Past its deadline when the caller read the clock, and not yet swept by the
  // vault's own timer. The two orderings are the same refusal; this one keeps
  // its name because the caller's reading is what decides.
  kExpired = 4,
  // Held for a field of a different classification than the one being written.
  // A value the person gave for their national identifier goes into a national
  // identifier field and nowhere else.
  kFieldClassMismatch = 5,
};

class ValueReferenceVault {
 public:
  ValueReferenceVault();
  ValueReferenceVault(const ValueReferenceVault&) = delete;
  ValueReferenceVault& operator=(const ValueReferenceVault&) = delete;
  ~ValueReferenceVault();

  // The sole authority domain, mirroring ActorLeaseRegistry and
  // CapabilityLedger so that one profile generation cannot see another's
  // values. Beginning a generation drops everything the previous one held.
  void BeginGeneration(std::string profile_id, uint64_t generation);
  void RevokeGeneration(uint64_t generation);

  // Drops every value held for a task. Called when a task ends, is cancelled,
  // or has its leases preempted: a value outliving the errand it was given for
  // is the same defect as a lease outliving it.
  void RevokeTask(const TaskId& task_id);

  // Drops one exact held reference without exposing its bytes. The form-value
  // coordinator uses this to roll back an all-or-nothing answer when a later
  // field in the same reviewed sequence cannot be minted.
  void RevokeReference(const ValueReference& reference);

  // Mints a reference over bytes a person entered into a Taffy-owned control.
  //
  // `value` is taken by value and moved in, so no caller is left holding a
  // copy it forgot about. The returned reference is unpredictable and is not
  // a function of the bytes; see ValueReference in bip_identity.h.
  //
  // Returns an invalid (empty) reference when there is no active generation,
  // when the task identifier is not well formed, or when `value` is empty. An
  // empty value is refused rather than held because "the person entered
  // nothing" is not something to write into a field later.
  ValueReference Mint(const TaskId& task_id,
                      FillClearance clearance,
                      std::string value,
                      base::TimeTicks expires_at);

  // The same mint, under a name the two sides derived rather than exchanged.
  //
  // Decision 0088's crossing carries a **count** and nothing else: the person
  // fills a sheet in, and what reaches the isolated core is the length of the
  // contiguous prefix the browser successfully held. That is only sufficient
  // because the core can name the n-th answer afterwards without ever having
  // been told what it is called; the coordinator stops at the first refusal,
  // so there is no unrepresentable hole. Both sides compute the same name from
  // the request identity and the answer's position -
  // `value_reference_for_supplied` in
  // taffy-core/components/intelligence/core/rust/task-engine/src/field_values.rs
  // is the other half, and the two must agree exactly or a fill proposal
  // naming the person's n-th answer resolves to nothing.
  //
  // A derived name is predictable and a minted one is not, so it is worth
  // saying why that costs nothing. Decision 0063 section 4's property is that
  // a reference is not a function of the **value** - it discloses nothing
  // about the bytes, and two references over identical bytes differ. That
  // holds here: this name is a function of a request identity and an integer,
  // neither of which the person typed. What predictability would otherwise
  // buy an attacker is a reference it could present, and there is nowhere to
  // present one: the vault is a browser-process type no renderer and no
  // sandboxed service links against, and a resolution still has to match the
  // task, the generation, the deadline and the field's class.
  //
  // Returns an empty reference for everything `Mint` refuses, and for two
  // more: a name that is not a well-formed identifier, and a name this
  // generation already holds. The second is the vault's half of "a request is
  // answered once" - the record already there belongs to the answer that
  // arrived first, and replacing it would destroy a value the core may
  // already have been told about.
  ValueReference MintNamed(ValueReference reference,
                           const TaskId& task_id,
                           FillClearance clearance,
                           std::string value,
                           base::TimeTicks expires_at);

  // Spends the reference and moves the bytes into `out`, exactly once.
  //
  // `target_field_class` is what the browser observed about the field being
  // written, re-read at dispatch from the renderer's own resolution - never
  // what a proposal claimed it was.
  //
  // A refusal does **not** spend the reference. It cannot have leaked
  // anything: no branch that returns a refusal has touched the bytes, and the
  // verdict is about the reference and the target rather than about the value.
  // Spending on refusal would let a proposal aimed at the wrong field destroy
  // a value the person had already typed, which costs the person a retype and
  // buys nothing.
  //
  // `out` is left untouched unless the answer is kResolved.
  [[nodiscard]] ValueResolution Resolve(const ValueReference& reference,
                                        const TaskId& task_id,
                                        Sensitivity target_field_class,
                                        base::TimeTicks now,
                                        std::string& out);

  // How many references are currently spendable. A count of records, which
  // says nothing about any value in any of them; it exists for the tests that
  // prove a value was dropped.
  size_t HeldCount() const;

  // True when the vault still holds a spendable record under this reference.
  // Deliberately not a peek: it answers about the record, never the bytes.
  bool Holds(const ValueReference& reference) const;

 private:
  struct HeldValue {
    HeldValue();
    HeldValue(HeldValue&&);
    HeldValue& operator=(HeldValue&&);
    ~HeldValue();

    TaskId task_id;
    Sensitivity field_class = Sensitivity::kUnknownSensitive;
    base::TimeTicks expires_at;
    uint64_t generation = 0;
    std::string value;
  };

  // The one body both mint entry points run. Whether the name was drawn from
  // randomness or derived is decided by the caller and is the only difference
  // between them; every refusal, every binding and the scrub are here, so the
  // two cannot drift into holding a value under different rules.
  ValueReference MintUnder(ValueReference reference,
                           const TaskId& task_id,
                           FillClearance clearance,
                           std::string value,
                           base::TimeTicks expires_at);

  // Overwrites the bytes before the record goes away. A freed heap block keeps
  // its contents until something else claims them, and a crash dump taken in
  // between is a place a value could still be read out of; the whole point of
  // holding these for seconds rather than for a session is that the window is
  // small, and this is what makes the window actually close.
  static void Scrub(HeldValue& held);

  void Erase(std::map<ValueReference, HeldValue>::iterator it);

  // Drops every record whose deadline has passed, then re-arms the timer.
  //
  // This is the other half of the sentence above. Pruning only inside
  // Resolve() would close the window for a value somebody eventually names and
  // leave it open for the life of the profile generation for one nobody does,
  // which is the case a deadline exists for.
  void PruneExpired();

  // Arms the timer for the earliest deadline still held, or stops it when
  // nothing is. One timer for the whole map rather than one per record: the
  // map is small, and a record whose deadline is not the earliest is dropped
  // by the pass the earliest one wakes.
  void ScheduleNextExpiry();

  std::map<ValueReference, HeldValue> held_;
  std::string active_profile_id_;
  uint64_t active_generation_ = 0;
  // Cancelled by its own destructor, which runs before held_ is destroyed.
  base::DeadlineTimer expiry_timer_;
};

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_SECURITY_BROWSER_VALUE_REFERENCE_VAULT_H_
