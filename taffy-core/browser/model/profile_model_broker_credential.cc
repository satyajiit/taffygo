// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/model/profile_model_broker.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {
namespace {

namespace service = core_service::mojom;

// Overwrites a credential before letting go of it.
//
// Best effort, and deliberately named so rather than described as erasure: an
// optimizer is free to notice that nothing reads the buffer afterwards. What
// it buys is that the window in which a secret is legible in this process is
// the length of one call rather than the life of a freed allocation, which is
// the difference a heap dump would show.
void ClearString(std::string* value) {
  if (!value) {
    return;
  }
  std::ranges::fill(*value, '\0');
  value->clear();
}

// Whether a resolved credential can be a header value at all.
//
// The credential does not come from the core — it comes from the platform's
// secret store — so this is a different question from the contract validation
// the effect went through, asked of a different party. The answer still
// matters for the same reason: a value carrying a carriage return and a line
// feed ends its own header and starts one of its own choosing, and a store
// that was ever written through by something other than the provider settings
// screen is a store that could hold one.
//
// Visible ASCII with no space, which is narrower than a header value may be.
// Every provider credential in the four families is an opaque token, so the
// narrower alphabet costs nothing and the surrounding whitespace a paste from
// a web page carries is refused here rather than sent and rejected upstream.
bool IsUsableCredential(const std::string& credential) {
  if (credential.empty() ||
      credential.size() > service::kMaxModelHeaderValueBytes) {
    return false;
  }
  for (char character : credential) {
    const auto byte = static_cast<unsigned char>(character);
    if (byte < 0x21u || byte > 0x7Eu) {
      return false;
    }
  }
  return true;
}

}  // namespace

void ProfileModelBroker::OnCredentialResolved(
    std::string effect_id,
    std::optional<std::string> credential,
    std::optional<std::string> credential_origin) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Phase two. Every path below this line clears the credential before it
  // returns, including the ones that never send it, which is why the lookup
  // that decides whether the call still exists comes after the value is in
  // hand rather than before it.
  auto it = in_flight_.find(effect_id);
  if (it == in_flight_.end()) {
    // Cancelled, or its generation went away, while the store was being asked.
    // The answer arrived for a call nobody is waiting on and is destroyed
    // here rather than sent.
    if (credential) {
      ClearString(&*credential);
    }
    return;
  }

  if (!credential || !IsUsableCredential(*credential)) {
    if (credential) {
      ClearString(&*credential);
    }
    // Denied rather than unavailable: the browser reached the store and the
    // store had nothing usable filed under this handle. That is a fact about
    // this provider's configuration, and it is the one a person can act on.
    Finish(effect_id,
           MakeResult(*it->second->effect, service::EffectStatus::kDenied, {}));
    return;
  }

  // The address the credential named, applied before anything leaves. It is
  // written into the pending call rather than passed alongside it, so the URL
  // a request is sent to stays the one this class checked — the property the
  // header argues for `PendingCall::url` — and so a cancellation, a deadline
  // and the transport all read the same one address.
  //
  // Three answers, and the switch is exhaustive on purpose: a fourth verdict
  // has to be a compilation failure here rather than a case that quietly falls
  // through to sending the request. Falling through is what this did while a
  // refusal and a credential that named no address were the same answer, and
  // `ResolveCredentialOrigin` is where the difference is now made.
  PendingCall& pending = *it->second;
  const CredentialOriginOutcome origin = ResolveCredentialOrigin(
      *pending.effect->model_request, credential_origin, pending.route);
  switch (origin.verdict) {
    case CredentialOriginVerdict::kUnchanged:
      break;
    case CredentialOriginVerdict::kSubstituted:
      pending.url = origin.url;
      break;
    case CredentialOriginVerdict::kRefused:
      ClearString(&*credential);
      // Denied rather than unavailable, for the reason the branch above it is
      // denied: the browser reached the store and the store answered, and what
      // it answered with does not address this provider. That is a fact about
      // this provider's configuration, and it is the one a person can act on.
      // The result is composed before `Finish` runs, because `Finish` destroys
      // the entry `pending` names.
      Finish(effect_id, MakeResult(*pending.effect,
                                   service::EffectStatus::kDenied, {}));
      return;
  }

  StartRequest(effect_id, std::move(credential));
}

}  // namespace taffy
