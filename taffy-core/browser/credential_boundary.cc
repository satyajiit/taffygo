// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/credential_boundary.h"

#include <string>

#include "content/public/browser/browser_thread.h"

namespace taffy {

namespace {

// One bit per credential class, so overlapping interactions cannot end each
// other early. Twelve classes today against thirty-two bits; the assertion
// below is what notices when that stops being true.
uint32_t BitFor(CredentialClass credential_class) {
  return 1u << static_cast<uint32_t>(credential_class);
}

static_assert(static_cast<uint32_t>(CredentialClass::kCaptchaAnswer) < 32,
              "The interaction mask is a uint32_t with one bit per credential "
              "class. Adding a thirty-third class needs a wider mask, not a "
              "silently truncated shift.");

}  // namespace

CredentialBoundary::CredentialBoundary() = default;
CredentialBoundary::~CredentialBoundary() = default;

void CredentialBoundary::NoteCredentialField(
    const CredentialFieldMetadata& metadata) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  // The metadata cannot carry a value, so there is nothing here to sanitize.
  // What is recorded is that the tab has one, which is what the suspension
  // rule and the audit trail need.
  const TabId tab_id{std::string(metadata.tab_id.chars.data())};
  if (!tab_id.is_valid()) {
    return;
  }
  ++tabs_[tab_id].credential_field_count;
}

void CredentialBoundary::BeginCredentialInteraction(
    const TabId& tab_id,
    CredentialClass credential_class) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!tab_id.is_valid()) {
    return;
  }
  if (!CredentialClassSuspendsAssistantAccess(credential_class)) {
    return;
  }
  tabs_[tab_id].active_interaction_mask |= BitFor(credential_class);
}

void CredentialBoundary::EndCredentialInteraction(
    const TabId& tab_id,
    CredentialClass credential_class) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto it = tabs_.find(tab_id);
  if (it == tabs_.end()) {
    return;
  }
  it->second.active_interaction_mask &= ~BitFor(credential_class);
}

void CredentialBoundary::SetAuthorizationHandoffPending(const TabId& tab_id,
                                                        bool pending) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!tab_id.is_valid()) {
    return;
  }
  tabs_[tab_id].authorization_handoff_pending = pending;
}

AssistantAccess CredentialBoundary::EvaluateAssistantAccess(
    const TabId& tab_id) const {
  const TabState* state = Find(tab_id);
  if (!state) {
    // A tab nobody has recorded anything about has no credential interaction
    // and no handoff. Permitting is correct here and is not a fail-open: every
    // other authority — capability, actor lease, origin policy — is checked
    // separately and this class is not the one that grants anything.
    return AssistantAccess::kPermitted;
  }
  if (state->active_interaction_mask != 0) {
    return AssistantAccess::kSuspendedCredentialInteraction;
  }
  if (state->authorization_handoff_pending) {
    return AssistantAccess::kSuspendedAuthorizationHandoff;
  }
  return AssistantAccess::kPermitted;
}

bool CredentialBoundary::AssistantMayObserve(const TabId& tab_id) const {
  return EvaluateAssistantAccess(tab_id) == AssistantAccess::kPermitted;
}

bool CredentialBoundary::AssistantMayAct(const TabId& tab_id) const {
  // Identical to observation today, and separate on purpose: acting is the
  // stricter of the two, and if the rules ever diverge this is the call site
  // that must not have to change.
  return EvaluateAssistantAccess(tab_id) == AssistantAccess::kPermitted;
}

size_t CredentialBoundary::CredentialFieldCount(const TabId& tab_id) const {
  const TabState* state = Find(tab_id);
  return state ? state->credential_field_count : 0u;
}

void CredentialBoundary::ForgetTab(const TabId& tab_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  tabs_.erase(tab_id);
}

void CredentialBoundary::ForgetDocument(const TabId& tab_id,
                                        const FrameId& frame_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto it = tabs_.find(tab_id);
  if (it == tabs_.end()) {
    return;
  }

  // A cross-document commit retires every field the old document had, and with
  // it every interaction that was in progress. The frame identifier is taken
  // so the call site reads correctly and so a future per-frame refinement has
  // the argument it needs; today the browser suspends per tab, which is the
  // conservative direction.
  (void)frame_id;
  it->second.credential_field_count = 0;
  it->second.active_interaction_mask = 0;

  // The authorization handoff deliberately survives: an app switch returns
  // through a navigation, so clearing it here would clear it exactly when the
  // return arrives. OAuthContinuityTracker owns its lifetime.
}

const CredentialBoundary::TabState* CredentialBoundary::Find(
    const TabId& tab_id) const {
  auto it = tabs_.find(tab_id);
  return it == tabs_.end() ? nullptr : &it->second;
}

}  // namespace taffy
