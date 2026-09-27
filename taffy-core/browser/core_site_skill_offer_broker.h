// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_SITE_SKILL_OFFER_BROKER_H_
#define TAFFY_BROWSER_CORE_SITE_SKILL_OFFER_BROKER_H_

#include <stdint.h>

#include <optional>
#include <string>

#include "base/containers/flat_map.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "taffy/browser/core_service_manager_types.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace content {
class WebContents;
}

namespace taffy {

// Browser custody for page-bound saved-skill offers.
//
// The isolated core returns immutable skill-version identities. This owner
// turns them into opaque UI capabilities and is the only place that can turn
// one back into a version at Start. The capability stays bound to the exact
// WebContents, tab, frame, page epoch, graph revision, and origin that matched.
class CoreSiteSkillOfferBroker final {
 public:
  using MatchCallback =
      base::OnceCallback<void(core_service::mojom::SiteSkillMatchResultPtr)>;

  explicit CoreSiteSkillOfferBroker(bool private_profile);
  CoreSiteSkillOfferBroker(const CoreSiteSkillOfferBroker&) = delete;
  CoreSiteSkillOfferBroker& operator=(const CoreSiteSkillOfferBroker&) = delete;
  ~CoreSiteSkillOfferBroker();

  // Announces pending-match custody transitions so the profile can maintain
  // its one idle deadline without periodically asking this broker.
  void SetPendingChangedCallback(base::RepeatingClosure callback);

  size_t pending_count() const;
  bool has_pending() const;

  // Begins custody before the Mojo request is sent and returns its single
  // completion callback. A later match supersedes the capabilities of every
  // earlier one; an earlier reply can still settle its inspector snapshot but
  // cannot mint a capability the current page surface could select.
  MatchCallback TrackMatch(
      uint64_t service_generation,
      std::string operation_id,
      base::WeakPtr<content::WebContents> web_contents,
      CorePageObservationIdentity identity,
      core_service::mojom::OperationEnvelopePtr operation,
      core_api::mojom::PageInspectorSnapshotViewPtr snapshot,
      CoreServicePageInspectorCallback callback);

  // Settles every inspector waiting on a service generation and revokes every
  // outstanding offer. The page projection remains useful even when matching
  // is unavailable, so it is preserved with a typed sub-status.
  void ResolveUnavailable();

  // Resolves only a current opaque offer under the exact single-source consent
  // shape. No caller receives the version without another live page check.
  std::optional<std::string> ResolveForStart(
      const std::string& offer_id,
      core_api::mojom::TaskTemplateId template_id,
      const core_service::mojom::TaskConsentPreview& consent) const;

 private:
  struct PendingMatch {
    base::WeakPtr<content::WebContents> web_contents;
    CorePageObservationIdentity identity;
    core_service::mojom::OperationEnvelopePtr operation;
    core_api::mojom::PageInspectorSnapshotViewPtr snapshot;
    CoreServicePageInspectorCallback callback;
    uint64_t sequence = 0;
  };

  struct OfferBinding {
    base::WeakPtr<content::WebContents> web_contents;
    CorePageObservationIdentity identity;
    std::string skill_version_id;
  };

  void OnMatched(uint64_t service_generation,
                 std::string operation_id,
                 core_service::mojom::SiteSkillMatchResultPtr result);

  base::flat_map<std::string, PendingMatch> pending_;
  base::flat_map<std::string, OfferBinding> bindings_;
  base::RepeatingClosure pending_changed_callback_;
  const bool private_profile_;
  uint64_t latest_sequence_ = 0;
  bool accepting_offers_ = false;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<CoreSiteSkillOfferBroker> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_SITE_SKILL_OFFER_BROKER_H_
