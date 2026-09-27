// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CREDENTIAL_BOUNDARY_H_
#define TAFFY_BROWSER_CREDENTIAL_BOUNDARY_H_

#include <stddef.h>
#include <stdint.h>

#include <map>

#include "taffy/components/intelligence/content/credential_field_metadata.h"
#include "taffy/common/public/bip_identity.h"

// The browser-process credential boundary (PAR-AUTH-001: a manual website
// login works, and neither the assistant nor the model can read the password
// value).
//
// The renderer omits the value; this class removes the assistant's access to
// the tab while a credential interaction is happening at all. Both are needed,
// and the second is the one a compromised renderer cannot lie its way past,
// because the decision is made from browser-owned state.
//
// The rule, in one sentence: **while the user is entering a credential, the
// tab is not observable and not actionable by the assistant.** Not
// "redacted" — unavailable. A redacted observation of a login form still tells
// a task when the user typed, how long the field is, and when submission
// happened, and none of those are things the assistant needs in order for
// manual login to work.
//
// Read the public interface for what is missing from it: there is no method
// that accepts a credential value, no method that returns one, and no method
// that takes a CredentialValue by value — the last is impossible anyway,
// because CredentialValue has no definition (credential_field_metadata.h).
//
// This class has no dependency on the AI runtime. With the runtime absent
// every query still answers, manual login still works, and the suspension it
// describes simply has nothing to suspend (PAR-AI-BR-001).
//
// UI thread only.

namespace taffy {

// Why the assistant cannot touch a tab right now. Never a free-form string:
// the surface that explains it uses a trusted local template keyed by this
// value.
enum class AssistantAccess : uint8_t {
  kPermitted = 0,
  // A credential field in this tab is being interacted with.
  kSuspendedCredentialInteraction = 1,
  // The tab is mid-way through an external authorization app switch
  // (oauth_continuity_tracker.h).
  kSuspendedAuthorizationHandoff = 2,
};

class CredentialBoundary {
 public:
  CredentialBoundary();
  CredentialBoundary(const CredentialBoundary&) = delete;
  CredentialBoundary& operator=(const CredentialBoundary&) = delete;
  ~CredentialBoundary();

  // Records that a credential field exists. Called from the browser side of
  // the observation path, with metadata the renderer produced *after* omitting
  // the value. It is safe to call repeatedly for the same field.
  void NoteCredentialField(const CredentialFieldMetadata& metadata);

  // The user began or finished interacting with a credential field. Begin is
  // idempotent per class; a tab can be inside several at once — a password
  // manager filling both a password and a one-time code, for instance — and
  // access resumes only when the last of them ends.
  void BeginCredentialInteraction(const TabId& tab_id,
                                  CredentialClass credential_class);
  void EndCredentialInteraction(const TabId& tab_id,
                                CredentialClass credential_class);

  // Set by OAuthContinuityTracker while an external authorization app switch
  // is outstanding for the tab.
  void SetAuthorizationHandoffPending(const TabId& tab_id, bool pending);

  AssistantAccess EvaluateAssistantAccess(const TabId& tab_id) const;

  // Convenience for the many call sites whose only question is "may I".
  bool AssistantMayObserve(const TabId& tab_id) const;
  bool AssistantMayAct(const TabId& tab_id) const;

  // How many credential fields have been seen in a tab. Diagnostic only, and
  // it says nothing about any of them.
  size_t CredentialFieldCount(const TabId& tab_id) const;

  // Called when a tab goes away or navigates across documents. State that
  // outlived its document would suspend a tab forever, or worse, stop
  // suspending one that still needs it.
  void ForgetTab(const TabId& tab_id);
  void ForgetDocument(const TabId& tab_id, const FrameId& frame_id);

 private:
  struct TabState {
    // One entry per credential class currently being interacted with, so that
    // overlapping interactions cannot end each other early.
    uint32_t active_interaction_mask = 0;
    bool authorization_handoff_pending = false;
    size_t credential_field_count = 0;
  };

  const TabState* Find(const TabId& tab_id) const;

  std::map<TabId, TabState> tabs_;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_CREDENTIAL_BOUNDARY_H_
