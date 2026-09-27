// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_OAUTH_RETURN_FACTS_H_
#define TAFFY_BROWSER_OAUTH_RETURN_FACTS_H_

#include <string>

#include "taffy/common/public/bip_identity.h"

class GURL;

// What comes back from an external authorization app switch, with the secrets
// removed before there is anywhere to put them
// (PAR-AUTH-004: "state survives the external provider or app flow without
// leaking callback data to Taffy").
//
// An authorization callback URL is one of the most sensitive strings a browser
// handles. Its query and fragment carry the authorization code, the state
// parameter, and on the implicit flows the access token and the identity token
// outright. Anything that logs the URL, records it in an audit event, puts it
// in a crash key or hands it to a model has leaked a credential, and the usual
// mitigation — "remember to strip the query here" — is a rule in a comment.
//
// This type removes the rule by removing the opportunity:
//
//   * The only producer is FromCallbackUrl(), which reads the origin and the
//     path and drops everything else on the floor. The query and the fragment
//     are never assigned to a member, because there is no member for them.
//   * The constructor is private. There is no way to build one of these from a
//     string, so no future call site can construct the "full" version.
//   * There is no accessor for callback parameters, only a boolean saying
//     whether any existed. That boolean is what the continuity check needs; the
//     values are what only Chromium's own navigation stack ever sees.
//
// The values still reach the site, because the navigation itself is Chromium's
// and this type is not in its path. What this type governs is what TaffyGo
// code is able to know, which is deliberately almost nothing.

namespace taffy {

class OAuthReturnFacts {
 public:
  // The only producer. `callback_url` is the URL the app switch returned to,
  // as the browser process committed it.
  static OAuthReturnFacts FromCallbackUrl(const GURL& callback_url,
                                          const TabId& tab_id);

  OAuthReturnFacts(const OAuthReturnFacts&);
  OAuthReturnFacts& operator=(const OAuthReturnFacts&);
  ~OAuthReturnFacts();

  const TabId& tab_id() const { return tab_id_; }

  // Scheme, host and port. This is what the continuity check compares against
  // the origin the handoff started from.
  const Origin& callback_origin() const { return callback_origin_; }

  // Path only. Redirect URIs are registered per path, so the path is part of
  // deciding whether this callback belongs to the handoff that is pending. It
  // carries no parameters by construction.
  const std::string& callback_path() const { return callback_path_; }

  // True when the callback URL had a query or a fragment. Nothing records what
  // was in either.
  bool carried_callback_parameters() const {
    return carried_callback_parameters_;
  }

 private:
  OAuthReturnFacts();

  TabId tab_id_;
  Origin callback_origin_;
  std::string callback_path_;
  bool carried_callback_parameters_ = false;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_OAUTH_RETURN_FACTS_H_
