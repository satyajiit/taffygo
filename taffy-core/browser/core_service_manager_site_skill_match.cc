// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/uuid.h"
#include "taffy/browser/core_api/task_consent_shape.h"
#include "taffy/browser/core_site_skill_offer_broker.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;
namespace service = core_service::mojom;

bool SameOperation(const service::OperationEnvelope& actual,
                   const service::OperationEnvelope& expected) {
  return actual.operation_id == expected.operation_id &&
         actual.service_generation == expected.service_generation &&
         actual.task_revision == expected.task_revision &&
         actual.deadline_monotonic_ms == expected.deadline_monotonic_ms &&
         actual.idempotency_key == expected.idempotency_key;
}

bool SameDocument(const DirectObservationContext& live,
                  const CorePageObservationIdentity& expected) {
  return live.tab_id == expected.tab_id && live.frame_id == expected.frame_id &&
         live.page_epoch == expected.page_epoch &&
         live.origin == expected.origin &&
         live.graph_revision == expected.graph_revision;
}

bool IsContentFree(const service::SiteSkillMatchResult& result) {
  return result.tab_id.empty() && result.frame_id.empty() &&
         result.page_epoch.empty() && result.graph_revision == 0u &&
         result.origin.empty() && result.offers.empty();
}

bool IsBoundedOffer(const service::SiteSkillMatchOffer& offer) {
  return !offer.skill_version_id.empty() &&
         offer.skill_version_id.size() <= service::kMaxIdentifierBytes &&
         !offer.skill_id.empty() &&
         offer.skill_id.size() <= service::kMaxSkillIdBytes &&
         offer.active_version > 0u &&
         offer.active_version <= service::kMaxSkillVersionsPerSkill &&
         offer.step_count > 0u && offer.step_count <= service::kMaxSkillSteps;
}

api::SiteSkillOfferAvailability ApiAvailability(
    service::SiteSkillMatchStatus status) {
  switch (status) {
    case service::SiteSkillMatchStatus::kAvailable:
      return api::SiteSkillOfferAvailability::kAvailable;
    case service::SiteSkillMatchStatus::kStalePage:
      return api::SiteSkillOfferAvailability::kStaleDocument;
    case service::SiteSkillMatchStatus::kPrivateProfile:
      return api::SiteSkillOfferAvailability::kPrivateProfile;
    case service::SiteSkillMatchStatus::kIncomplete:
      return api::SiteSkillOfferAvailability::kIncomplete;
    case service::SiteSkillMatchStatus::kUnavailable:
      return api::SiteSkillOfferAvailability::kCoreUnavailable;
    case service::SiteSkillMatchStatus::kMalformed:
      return api::SiteSkillOfferAvailability::kInvalidResponse;
  }
}

api::PageInspectorSnapshotResultPtr SnapshotResult(
    api::PageInspectorAvailability availability,
    api::PageInspectorSnapshotViewPtr snapshot = nullptr) {
  auto output = api::PageInspectorSnapshotResult::New();
  output->availability = availability;
  output->snapshot = std::move(snapshot);
  return output;
}

}  // namespace

CoreSiteSkillOfferBroker::CoreSiteSkillOfferBroker(bool private_profile)
    : private_profile_(private_profile) {}

CoreSiteSkillOfferBroker::~CoreSiteSkillOfferBroker() = default;

void CoreSiteSkillOfferBroker::SetPendingChangedCallback(
    base::RepeatingClosure callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  pending_changed_callback_ = std::move(callback);
}

size_t CoreSiteSkillOfferBroker::pending_count() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return pending_.size();
}

bool CoreSiteSkillOfferBroker::has_pending() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return !pending_.empty();
}

CoreSiteSkillOfferBroker::MatchCallback CoreSiteSkillOfferBroker::TrackMatch(
    uint64_t service_generation,
    std::string operation_id,
    base::WeakPtr<content::WebContents> web_contents,
    CorePageObservationIdentity identity,
    service::OperationEnvelopePtr operation,
    api::PageInspectorSnapshotViewPtr snapshot,
    CoreServicePageInspectorCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const uint64_t sequence = ++latest_sequence_;
  accepting_offers_ = !private_profile_;
  bindings_.clear();
  const bool was_empty = pending_.empty();
  const bool inserted =
      pending_
          .emplace(operation_id,
                   PendingMatch{std::move(web_contents), std::move(identity),
                                std::move(operation), std::move(snapshot),
                                std::move(callback), sequence})
          .second;
  CHECK(inserted);
  if (was_empty && pending_changed_callback_) {
    pending_changed_callback_.Run();
  }
  return base::BindOnce(&CoreSiteSkillOfferBroker::OnMatched,
                        weak_factory_.GetWeakPtr(), service_generation,
                        std::move(operation_id));
}

void CoreSiteSkillOfferBroker::OnMatched(
    uint64_t service_generation,
    std::string operation_id,
    service::SiteSkillMatchResultPtr result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = pending_.find(operation_id);
  if (found == pending_.end()) {
    return;
  }
  PendingMatch pending = std::move(found->second);
  pending_.erase(found);
  if (pending_.empty() && pending_changed_callback_) {
    pending_changed_callback_.Run();
  }
  TaffyPageIntelligenceHost* host =
      pending.web_contents ? TaffyPageIntelligenceHost::FromWebContents(
                                 pending.web_contents.get())
                           : nullptr;
  const std::optional<DirectObservationContext> live =
      host ? host->BuildDirectObservationContext() : std::nullopt;
  if (!live || !SameDocument(*live, pending.identity)) {
    std::move(pending.callback)
        .Run(SnapshotResult(api::PageInspectorAvailability::kStaleDocument));
    return;
  }
  if (!pending.snapshot) {
    std::move(pending.callback)
        .Run(SnapshotResult(api::PageInspectorAvailability::kInvalidResponse));
    return;
  }
  if (private_profile_) {
    pending.snapshot->site_skill_offer_availability =
        api::SiteSkillOfferAvailability::kPrivateProfile;
    pending.snapshot->site_skill_offers.clear();
    bindings_.clear();
    std::move(pending.callback)
        .Run(SnapshotResult(api::PageInspectorAvailability::kAvailable,
                            std::move(pending.snapshot)));
    return;
  }
  if (!result || !result->operation || !pending.operation ||
      !SameOperation(*result->operation, *pending.operation) ||
      result->operation->service_generation != service_generation ||
      (result->status != service::SiteSkillMatchStatus::kAvailable &&
       !IsContentFree(*result))) {
    pending.snapshot->site_skill_offer_availability =
        api::SiteSkillOfferAvailability::kInvalidResponse;
    std::move(pending.callback)
        .Run(SnapshotResult(api::PageInspectorAvailability::kAvailable,
                            std::move(pending.snapshot)));
    return;
  }

  pending.snapshot->site_skill_offer_availability =
      ApiAvailability(result->status);
  if (pending.sequence != latest_sequence_) {
    pending.snapshot->site_skill_offer_availability =
        api::SiteSkillOfferAvailability::kStaleDocument;
    pending.snapshot->site_skill_offers.clear();
    std::move(pending.callback)
        .Run(SnapshotResult(api::PageInspectorAvailability::kAvailable,
                            std::move(pending.snapshot)));
    return;
  }
  bindings_.clear();
  if (result->status == service::SiteSkillMatchStatus::kAvailable) {
    if (result->tab_id != pending.identity.tab_id ||
        result->frame_id != pending.identity.frame_id ||
        result->page_epoch != pending.identity.page_epoch ||
        result->graph_revision != pending.identity.graph_revision ||
        result->origin != pending.identity.origin ||
        result->offers.size() > service::kMaxSkillsPerProfile) {
      pending.snapshot->site_skill_offer_availability =
          api::SiteSkillOfferAvailability::kInvalidResponse;
    } else {
      std::string previous_skill_id;
      for (const service::SiteSkillMatchOfferPtr& offer : result->offers) {
        if (!offer || !IsBoundedOffer(*offer) ||
            (!previous_skill_id.empty() &&
             previous_skill_id >= offer->skill_id)) {
          pending.snapshot->site_skill_offer_availability =
              api::SiteSkillOfferAvailability::kInvalidResponse;
          pending.snapshot->site_skill_offers.clear();
          bindings_.clear();
          break;
        }
        previous_skill_id = offer->skill_id;
        const std::string offer_id =
            "site-skill-offer-" +
            base::Uuid::GenerateRandomV4().AsLowercaseString();
        bindings_.emplace(offer_id,
                          OfferBinding{pending.web_contents, pending.identity,
                                       offer->skill_version_id});
        pending.snapshot->site_skill_offers.push_back(
            api::SiteSkillOfferView::New(offer_id, offer->skill_id,
                                         offer->active_version,
                                         offer->step_count));
      }
    }
  }
  std::move(pending.callback)
      .Run(SnapshotResult(api::PageInspectorAvailability::kAvailable,
                          std::move(pending.snapshot)));
}

void CoreSiteSkillOfferBroker::ResolveUnavailable() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  weak_factory_.InvalidateWeakPtrs();
  accepting_offers_ = false;
  const bool had_pending = !pending_.empty();
  auto pending = std::move(pending_);
  pending_.clear();
  bindings_.clear();
  if (had_pending && pending_changed_callback_) {
    pending_changed_callback_.Run();
  }
  for (auto& [operation_id, match] : pending) {
    static_cast<void>(operation_id);
    if (match.snapshot) {
      match.snapshot->site_skill_offer_availability =
          api::SiteSkillOfferAvailability::kCoreUnavailable;
      std::move(match.callback)
          .Run(SnapshotResult(api::PageInspectorAvailability::kAvailable,
                              std::move(match.snapshot)));
    } else {
      std::move(match.callback)
          .Run(
              SnapshotResult(api::PageInspectorAvailability::kCoreUnavailable));
    }
  }
}

std::optional<std::string> CoreSiteSkillOfferBroker::ResolveForStart(
    const std::string& offer_id,
    api::TaskTemplateId template_id,
    const service::TaskConsentPreview& consent) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = bindings_.find(offer_id);
  const bool errand = template_id == api::TaskTemplateId::kWebErrand;
  const bool valid_discovery =
      errand && consent.provider_route != service::TaskProviderRoute::kNoModelRequired
          ? consent.source_discovery_enabled && consent.new_source_cap > 0u &&
                consent.new_source_cap <= kMaxErrandNewSourceCap
          : !consent.source_discovery_enabled && consent.new_source_cap == 0u;
  if (!accepting_offers_ || found == bindings_.end() || !valid_discovery ||
      consent.sources.size() != 1u || !consent.sources.front()) {
    return std::nullopt;
  }
  const OfferBinding& binding = found->second;
  const service::TaskConsentSource& source = *consent.sources.front();
  TaffyPageIntelligenceHost* host =
      binding.web_contents ? TaffyPageIntelligenceHost::FromWebContents(
                                 binding.web_contents.get())
                           : nullptr;
  const std::optional<DirectObservationContext> live =
      host ? host->BuildDirectObservationContext() : std::nullopt;
  if (!live || !SameDocument(*live, binding.identity) ||
      source.tab_id != binding.identity.tab_id ||
      source.normalized_origin != binding.identity.origin) {
    return std::nullopt;
  }
  return binding.skill_version_id;
}

}  // namespace taffy
