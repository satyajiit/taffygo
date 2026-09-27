// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/uuid.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "taffy/browser/core_task_policy_destination.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/browser/task_source_selection_registry.h"
#include "taffy/components/intelligence/content/page_intelligence_broker.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace taffy {
namespace {

namespace service = core_service::mojom;

bool IsIdentifier(const std::string& value) {
  return !value.empty() && value.size() <= service::kMaxIdentifierBytes &&
         std::none_of(value.begin(), value.end(), [](char character) {
           return static_cast<unsigned char>(character) < 0x20u;
         });
}

bool IsNormalizedHttpTupleOrigin(const std::string& value) {
  const GURL parsed(value);
  const url::Origin origin = url::Origin::Create(parsed);
  return parsed.is_valid() && parsed.SchemeIsHTTPOrHTTPS() &&
         !origin.opaque() && origin.Serialize() == value;
}

}  // namespace

void TaskSourceSelectionRegistry::PrepareDiscoveryTab(
    const std::string& task_id,
    const std::string& effect_id,
    const std::string& browser_session_id,
    uint32_t remaining_new_source_cap,
    TaskDiscoveryPreparationCallback callback) {
  if (!IsIdentifier(task_id) || !IsIdentifier(effect_id) ||
      !IsIdentifier(browser_session_id) || remaining_new_source_cap == 0u ||
      remaining_new_source_cap > service::kMaxNewSourceCap || !callback) {
    if (callback) {
      std::move(callback).Run(std::nullopt);
    }
    return;
  }
  PruneOwnedTaskTabs();
  const auto key = std::make_pair(task_id, effect_id);
  const auto repeated = discovery_bootstraps_.find(key);
  if (repeated != discovery_bootstraps_.end()) {
    DiscoveryBootstrapRecord& record = repeated->second;
    if (record.browser_session_id != browser_session_id ||
        record.remaining_new_source_cap != remaining_new_source_cap) {
      std::move(callback).Run(std::nullopt);
      return;
    }
    if (record.pending) {
      if (record.callbacks.size() >= service::kMaxInFlightPerProfile) {
        std::move(callback).Run(std::nullopt);
        return;
      }
      record.callbacks.push_back(std::move(callback));
      return;
    }
    content::WebContents* web_contents = record.web_contents.get();
    auto* broker = web_contents
                       ? PageIntelligenceBroker::FromWebContents(web_contents)
                       : nullptr;
    const std::string tab_id = broker ? broker->tab_id().value : std::string();
    std::move(callback).Run(
        IsExactDiscoveryTab(task_id, tab_id, browser_session_id,
                            remaining_new_source_cap, web_contents)
            ? std::optional<std::string>(tab_id)
            : std::nullopt);
    return;
  }
  // A second effect is not a retry of the first. The durable task owns one
  // discovery document, so a different effect cannot manufacture another.
  if (std::any_of(discovery_bootstraps_.begin(), discovery_bootstraps_.end(),
                  [&task_id](const auto& entry) {
                    return entry.second.task_id == task_id;
                  }) ||
      active_windows_.size() != 1u) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  const WindowToken window_token = *active_windows_.begin();
  const auto window = windows_.find(window_token);
  if (window == windows_.end() || !window->second.browser_actions) {
    std::move(callback).Run(std::nullopt);
    return;
  }

  // Install the spent/idempotency record before crossing into Java. The
  // creator calls ClaimAssistantCreatedTab re-entrantly before publication;
  // no nested or repeated invocation may pass this point twice.
  auto [record_it, inserted] = discovery_bootstraps_.emplace(
      key, DiscoveryBootstrapRecord{
               .task_id = task_id,
               .effect_id = effect_id,
               .browser_session_id = browser_session_id,
               .remaining_new_source_cap = remaining_new_source_cap,
           });
  if (!inserted) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  record_it->second.callbacks.push_back(std::move(callback));
  TaskBrowserActionPlatform* platform = window->second.browser_actions;
  platform->OpenTaskDiscoveryTab(
      task_id, effect_id,
      base::BindOnce(&TaskSourceSelectionRegistry::OnDiscoveryTabCreated,
                     weak_factory_.GetWeakPtr(), key, window_token));
}

void TaskSourceSelectionRegistry::OnDiscoveryTabCreated(
    std::pair<std::string, std::string> bootstrap_key,
    WindowToken window_token,
    content::WebContents* created) {
  const auto record_it = discovery_bootstraps_.find(bootstrap_key);
  if (record_it == discovery_bootstraps_.end() || !record_it->second.pending) {
    return;
  }
  DiscoveryBootstrapRecord& record = record_it->second;
  record.pending = false;
  const auto window = windows_.find(window_token);
  TaskBrowserActionPlatform* platform =
      window != windows_.end() ? window->second.browser_actions : nullptr;
  auto* broker =
      created ? PageIntelligenceBroker::FromWebContents(created) : nullptr;
  const std::string tab_id = broker ? broker->tab_id().value : std::string();
  const bool exact =
      active_windows_.size() == 1u && active_windows_.contains(window_token) &&
      platform && created && created->GetBrowserContext() == browser_context_ &&
      HasExactTaskTabClaim(record.task_id, record.effect_id, created) &&
      IsTaskOwnedTab(record.task_id, created) && !tab_id.empty() &&
      ResolveTaskDiscoveryDocument(browser_context_, tab_id).has_value();
  if (exact) {
    record.web_contents = created->GetWeakPtr();
  } else {
    if (platform && created &&
        HasExactTaskTabClaim(record.task_id, record.effect_id, created)) {
      platform->CloseTaskTab(created);
    }
    ForgetFailedDiscoveryTab(record.task_id, record.effect_id, created);
  }
  std::vector<TaskDiscoveryPreparationCallback> callbacks =
      std::move(record.callbacks);
  const std::optional<std::string> result =
      exact ? std::optional<std::string>(tab_id) : std::nullopt;
  for (TaskDiscoveryPreparationCallback& callback : callbacks) {
    std::move(callback).Run(result);
  }
}

bool TaskSourceSelectionRegistry::IsExactDiscoveryTab(
    const std::string& task_id,
    const std::string& tab_id,
    const std::string& browser_session_id,
    uint32_t remaining_new_source_cap,
    content::WebContents* web_contents) const {
  if (!IsIdentifier(task_id) || !IsIdentifier(tab_id) ||
      !IsIdentifier(browser_session_id) || !web_contents ||
      remaining_new_source_cap == 0u ||
      remaining_new_source_cap > service::kMaxNewSourceCap ||
      web_contents->GetBrowserContext() != browser_context_ ||
      !IsTaskOwnedTab(task_id, web_contents)) {
    return false;
  }
  size_t matches = 0u;
  for (const auto& entry : discovery_bootstraps_) {
    const DiscoveryBootstrapRecord& record = entry.second;
    if (record.task_id == task_id &&
        record.browser_session_id == browser_session_id &&
        record.remaining_new_source_cap == remaining_new_source_cap &&
        record.web_contents.get() == web_contents) {
      ++matches;
    }
  }
  const auto live = ResolveTaskDiscoveryDocument(browser_context_, tab_id);
  auto* broker = PageIntelligenceBroker::FromWebContents(web_contents);
  return matches == 1u && broker && broker->tab_id().value == tab_id && live &&
         live->tab_id == tab_id;
}

void TaskSourceSelectionRegistry::ForgetFailedDiscoveryTab(
    const std::string& task_id,
    const std::string& effect_id,
    content::WebContents* web_contents) {
  EraseIssuedSourcesForContents(web_contents);
  std::erase_if(owned_task_tabs_, [&](const OwnedTaskTab& owned) {
    return owned.task_id == task_id && owned.action_id == effect_id &&
           owned.web_contents.get() == web_contents;
  });
}

IssuedSourceLiveness TaskSourceSelectionRegistry::IssuedSourceLivenessOf(
    const service::TaskConsentSource& source) const {
  const auto issued = issued_sources_.find(source.source_id);
  if (issued == issued_sources_.end() ||
      issued->second.tab_id != source.tab_id ||
      issued->second.normalized_origin != source.normalized_origin ||
      issued->second.canonical_locator != source.canonical_locator) {
    return IssuedSourceLiveness::kGone;
  }
  content::WebContents* web_contents = issued->second.web_contents.get();
  if (!web_contents || web_contents->GetBrowserContext() != browser_context_ ||
      !IsRegisteredProductContents(web_contents)) {
    return IssuedSourceLiveness::kGone;
  }
  TaffyPageIntelligenceHost* host =
      TaffyPageIntelligenceHost::FromWebContents(web_contents);
  const std::optional<DirectObservationContext> live =
      host ? host->BuildDirectObservationContext() : std::nullopt;
  // The only answer here that is not a verdict about the source. The absence
  // of an observation context is not by itself the middle state: a dead
  // renderer, a detached frame and a missing broker all reach it too, and none
  // of those is a tab a task may go on holding a source for. So the middle
  // state is confirmed positively rather than inferred from a nullopt, by the
  // one resolver decision 0176 added for exactly this document — a live,
  // top-level frame whose committed origin is opaque, which is what Chromium
  // writes when an address does not answer. Anything else stays `kGone`.
  if (!live) {
    const std::optional<TaskDepartureDocumentContext> departure =
        ResolveTaskDepartureDocument(browser_context_, source.tab_id);
    return departure && departure->tab_id == source.tab_id
               ? IssuedSourceLiveness::kTabHasNoDocumentOfItsOwn
               : IssuedSourceLiveness::kGone;
  }
  return live->tab_id == source.tab_id &&
                 TaskSourceOriginStillNamesTheSite(source.normalized_origin,
                                                   live->origin) &&
                 (issued->second.origin_scoped ||
                  CanonicalSourceLocator(web_contents) ==
                      source.canonical_locator)
             ? IssuedSourceLiveness::kLive
             : IssuedSourceLiveness::kGone;
}

bool TaskSourceSelectionRegistry::IsLiveIssuedSource(
    const service::TaskConsentSource& source) const {
  return IssuedSourceLivenessOf(source) == IssuedSourceLiveness::kLive;
}

std::optional<service::TaskConsentSourcePtr>
TaskSourceSelectionRegistry::IssueDiscoveredSourceForAction(
    const std::string& task_id,
    const std::string& action_id) {
  if (!IsIdentifier(task_id) || !IsIdentifier(action_id)) {
    return std::nullopt;
  }
  PruneOwnedTaskTabs();
  content::WebContents* match = nullptr;
  for (const OwnedTaskTab& owned : owned_task_tabs_) {
    if (owned.task_id != task_id || owned.action_id != action_id ||
        !owned.web_contents) {
      continue;
    }
    if (match) {
      return std::nullopt;
    }
    match = owned.web_contents.get();
  }
  return IssueOwnedTaskSource(task_id, match);
}

std::optional<service::TaskConsentSourcePtr>
TaskSourceSelectionRegistry::IssueDiscoveredSourceForTab(
    const std::string& task_id,
    const std::string& tab_id) {
  if (!IsIdentifier(task_id) || !IsIdentifier(tab_id)) {
    return std::nullopt;
  }
  PruneOwnedTaskTabs();
  content::WebContents* match = nullptr;
  // Counters for the refusal line: which of the task's tabs could be read at
  // all, and which of those is the one the move landed in. A move that left
  // its origin and was issued no source is refused, and the refusal used to
  // say only `no-source-issued` - which is five clauses (decision 0228).
  size_t owned_by_task = 0u;
  size_t live_documents = 0u;
  // The browser-minted tab ids of the task's live tabs, for the refusal line
  // alone. `owned=1 live=1` with no match said the one tab the task owns is
  // not the tab the move was made in, twice on a phone, and not which tab
  // that was. They are synthetic session ids, like the node ids the link
  // refusal already prints, and say nothing about a page.
  std::string live_tab_ids;
  for (const OwnedTaskTab& owned : owned_task_tabs_) {
    content::WebContents* web_contents = owned.web_contents.get();
    if (owned.task_id != task_id || !web_contents) {
      continue;
    }
    ++owned_by_task;
    TaffyPageIntelligenceHost* host =
        TaffyPageIntelligenceHost::FromWebContents(web_contents);
    const std::optional<DirectObservationContext> live =
        host ? host->BuildDirectObservationContext() : std::nullopt;
    live_documents += size_t{live.has_value()};
    if (live && live_tab_ids.size() < 256u) {
      live_tab_ids += live_tab_ids.empty() ? "" : ",";
      live_tab_ids += live->tab_id;
    }
    if (!live || live->tab_id != tab_id) {
      continue;
    }
    if (match) {
      LOG(WARNING) << "[taffy_task_source_not_issued] at=tab-ambiguous"
                   << " owned=" << owned_by_task << " live=" << live_documents;
      return std::nullopt;
    }
    match = web_contents;
  }
  if (!match) {
    LOG(WARNING) << "[taffy_task_source_not_issued] at=no-live-task-tab"
                 << " owned=" << owned_by_task << " live=" << live_documents
                 << " asked=" << tab_id << " live_tabs=" << live_tab_ids;
  }
  return IssueOwnedTaskSource(task_id, match);
}

std::optional<service::TaskConsentSourcePtr>
TaskSourceSelectionRegistry::IssueOwnedTaskSource(
    const std::string& task_id,
    content::WebContents* web_contents) {
  if (!web_contents) {
    return std::nullopt;
  }
  if (!IsTaskOwnedTab(task_id, web_contents) ||
      web_contents->GetBrowserContext() != browser_context_) {
    LOG(WARNING) << "[taffy_task_source_not_issued] at=not-task-owned"
                 << " registered="
                 << (IsRegisteredProductContents(web_contents) ? 1 : 0);
    return std::nullopt;
  }
  TaffyPageIntelligenceHost* host =
      TaffyPageIntelligenceHost::FromWebContents(web_contents);
  const std::optional<DirectObservationContext> live =
      host ? host->BuildDirectObservationContext() : std::nullopt;
  if (!live || !IsIdentifier(live->tab_id) ||
      !IsNormalizedHttpTupleOrigin(live->origin)) {
    LOG(WARNING) << "[taffy_task_source_not_issued] at=document-not-a-source"
                 << " live=" << (live ? 1 : 0);
    return std::nullopt;
  }

  PruneIssuedSources();
  const std::optional<std::string> locator =
      CanonicalSourceLocator(web_contents);
  for (const auto& [source_id, source] : issued_sources_) {
    if (source.web_contents.get() == web_contents &&
        source.tab_id == live->tab_id &&
        source.normalized_origin == live->origin &&
        source.canonical_locator == locator && source.origin_scoped) {
      return std::optional<service::TaskConsentSourcePtr>(
          service::TaskConsentSource::New(source_id, source.tab_id,
                                          source.normalized_origin,
                                          source.canonical_locator));
    }
  }
  if (issued_sources_.size() >= issued_source_limit_) {
    LOG(WARNING) << "[taffy_task_source_not_issued] at=issued-source-limit"
                 << " issued=" << issued_sources_.size();
    return std::nullopt;
  }

  const std::string uuid = base::Uuid::GenerateRandomV4().AsLowercaseString();
  std::string source_id;
  source_id.reserve(32u);
  std::copy_if(uuid.begin(), uuid.end(), std::back_inserter(source_id),
               [](char character) { return character != '-'; });
  if (source_id.size() != 32u || issued_sources_.contains(source_id)) {
    return std::nullopt;
  }
  // Only an exact task-owned tab reaches this issuance. The completed action
  // must still pass the ledger's discovery consent and cap before this source
  // is published. Retain the verified public landing locator for recording,
  // while permitting later same-origin navigation in this same tab.
  issued_sources_.emplace(
      source_id, IssuedSource{source_id, live->tab_id, live->origin, locator,
                              web_contents->GetWeakPtr(), true});
  return std::optional<service::TaskConsentSourcePtr>(
      service::TaskConsentSource::New(source_id, live->tab_id, live->origin,
                                      locator));
}

void TaskSourceSelectionRegistry::EraseIssuedSourcesForContents(
    content::WebContents* web_contents) {
  if (!web_contents) {
    return;
  }
  for (auto it = issued_sources_.begin(); it != issued_sources_.end();) {
    if (it->second.web_contents.get() == web_contents) {
      it = issued_sources_.erase(it);
    } else {
      ++it;
    }
  }
}

bool TaskSourceSelectionRegistry::IssuedSourceSurvivesPrune(
    const IssuedSource& issued) const {
  content::WebContents* web_contents = issued.web_contents.get();
  if (!web_contents || web_contents->GetBrowserContext() != browser_context_ ||
      !IsRegisteredProductContents(web_contents)) {
    return false;
  }
  TaffyPageIntelligenceHost* host =
      TaffyPageIntelligenceHost::FromWebContents(web_contents);
  const std::optional<DirectObservationContext> live =
      host ? host->BuildDirectObservationContext() : std::nullopt;
  if (!live) {
    // A tab between documents keeps the source that was issued for it, for the
    // same reason the consent record survives one (decision 0183) — and this
    // clause is what makes that rule hold, because pruning here would be the
    // same loss by a quieter route. `IssuedSourceLivenessOf` would then find
    // nothing in this map and answer `kGone`, which is the code for a source
    // that was never issued, and the record would go on the next publication
    // with the reason naming the wrong thing entirely. Any task's
    // `ResolveConsentPreview` runs this sweep, so an unrelated errand starting
    // while this tab sits on an error page is enough.
    const std::optional<TaskDepartureDocumentContext> departure =
        ResolveTaskDepartureDocument(browser_context_, issued.tab_id);
    return departure && departure->tab_id == issued.tab_id;
  }
  // The same rule `IssuedSourceLivenessOf` answers by, and no stricter. This
  // compared origins for equality while liveness compared sites, so a tab the
  // site itself moved to a sibling host kept a live source until the next
  // sweep erased it — and a source erased here is `kGone` there, and `kGone`
  // destroys the task's consent for good. On a phone, myaadhaar.uidai.gov.in
  // sent the tab to myaadhaarbeta.uidai.gov.in from its own script, and a few
  // moves later every search, navigate and read the errand proposed came back
  // `source-not-authorized` with `record=0` (decision 0190).
  return live->tab_id == issued.tab_id &&
         TaskSourceOriginStillNamesTheSite(issued.normalized_origin,
                                           live->origin);
}

void TaskSourceSelectionRegistry::PruneIssuedSources() {
  for (auto it = issued_sources_.begin(); it != issued_sources_.end();) {
    if (IssuedSourceSurvivesPrune(it->second)) {
      ++it;
    } else {
      it = issued_sources_.erase(it);
    }
  }
}

}  // namespace taffy
