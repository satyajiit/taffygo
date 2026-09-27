// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/taffy_page_intelligence_host.h"

#include <string>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/no_destructor.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_factory.h"
#include "taffy/browser/navigation_lifecycle_tracker.h"
#include "taffy/browser/taffy_browser_effect_source.h"
#include "taffy/components/intelligence/content/frame_observation_endpoint.h"
#include "taffy/components/intelligence/content/graph_payload_encoder.h"
#include "taffy/components/intelligence/content/page_intelligence_broker.h"
#include "url/gurl.h"

namespace taffy {
namespace {

// Every host in the process, in creation order. A raw list because a host is
// owned by its WebContents and this is a directory of them, not an owner.
//
// It exists because the question "which page is the person looking at" has no
// answer inside one tab. //taffy/DEPS forbids naming //chrome,
// where the tab model lives, so the browser-side answer is the one content
// itself can give: of the tabs that carry a host, the visible one.
std::vector<TaffyPageIntelligenceHost*>& AllHosts() {
  static base::NoDestructor<std::vector<TaffyPageIntelligenceHost*>> hosts;
  return *hosts;
}

}  // namespace

TaffyPageIntelligenceHost* FindExactlyOneTabHost(
    content::BrowserContext* browser_context,
    const std::string& tab_id) {
  if (!browser_context || tab_id.empty()) {
    return nullptr;
  }
  TaffyPageIntelligenceHost* match = nullptr;
  for (TaffyPageIntelligenceHost* host : AllHosts()) {
    content::WebContents* web_contents = host->observed_web_contents();
    if (!web_contents || web_contents->GetBrowserContext() != browser_context) {
      continue;
    }
    auto* broker = PageIntelligenceBroker::FromWebContents(web_contents);
    if (!broker || broker->tab_id().value != tab_id) {
      continue;
    }
    // A duplicate browser identity is a corruption, not a reason to pick the
    // first WebContents in process order.
    if (match) {
      return nullptr;
    }
    match = host;
  }
  return match;
}

TaffyPageIntelligenceHost* FindPageIntelligenceHost(
    content::BrowserContext* browser_context,
    const std::string& tab_id) {
  if (!browser_context || tab_id.empty()) {
    return nullptr;
  }
  for (TaffyPageIntelligenceHost* host : AllHosts()) {
    content::WebContents* web_contents = host->observed_web_contents();
    if (!web_contents || web_contents->GetBrowserContext() != browser_context) {
      continue;
    }
    auto* broker = PageIntelligenceBroker::FromWebContents(web_contents);
    if (broker && broker->tab_id().value == tab_id) {
      return host;
    }
  }
  return nullptr;
}

std::optional<std::string> ResolveTaskObservedLink(
    content::BrowserContext* browser_context,
    const CanonicalLinkOpenHandle& handle) {
  auto* host = FindExactlyOneTabHost(browser_context, handle.tab_id);
  return host ? host->ResolveObservedLink(handle) : std::nullopt;
}

std::optional<std::string> ResolveTaskObservedDownload(
    content::BrowserContext* browser_context,
    const CanonicalObservedNodeHandle& handle) {
  auto* host = FindExactlyOneTabHost(browser_context, handle.tab_id);
  return host ? host->ResolveObservedDownload(handle) : std::nullopt;
}

void InvalidateTaskObservedLinks(content::BrowserContext* browser_context) {
  if (!browser_context) {
    return;
  }
  for (TaffyPageIntelligenceHost* host : AllHosts()) {
    content::WebContents* web_contents = host->observed_web_contents();
    if (web_contents && web_contents->GetBrowserContext() == browser_context) {
      host->observed_links_.Clear();
    }
  }
}

RequestId ObserveTabForCore(
    content::BrowserContext* browser_context,
    const core_service::mojom::PageObservationEffect& effect,
    ActorLeaseRegistry& actor_leases,
    CapabilityLedger& capabilities,
    std::optional<AuthorizedObservationTarget>* authorized_target,
    ObservationCompletion callback) {
  if (authorized_target) {
    authorized_target->reset();
  }
  if (!browser_context || effect.tab_id.empty() || effect.frame_id.empty() ||
      effect.page_epoch.empty() || !authorized_target || !callback) {
    if (callback) {
      std::move(callback).Run(std::nullopt);
    }
    return RequestId{};
  }
  for (TaffyPageIntelligenceHost* host : AllHosts()) {
    content::WebContents* web_contents = host->observed_web_contents();
    if (!web_contents || web_contents->GetBrowserContext() != browser_context) {
      continue;
    }
    auto* broker = PageIntelligenceBroker::FromWebContents(web_contents);
    if (broker && broker->tab_id().value == effect.tab_id) {
      return host->RequestObservation(effect, actor_leases, capabilities,
                                      authorized_target, std::move(callback));
    }
  }
  std::move(callback).Run(std::nullopt);
  return RequestId{};
}

bool CancelTabObservationForCore(content::BrowserContext* browser_context,
                                 const std::string& tab_id,
                                 RequestId request_id) {
  if (!browser_context || tab_id.empty() || !request_id.is_valid()) {
    return false;
  }
  for (TaffyPageIntelligenceHost* host : AllHosts()) {
    content::WebContents* web_contents = host->observed_web_contents();
    if (!web_contents || web_contents->GetBrowserContext() != browser_context) {
      continue;
    }
    auto* broker = PageIntelligenceBroker::FromWebContents(web_contents);
    if (broker && broker->tab_id().value == tab_id) {
      return host->CancelObservation(std::move(request_id));
    }
  }
  return false;
}

// static
bool TaffyPageIntelligenceHost::IsEligible(content::WebContents* web_contents) {
  if (!web_contents) {
    return false;
  }
  // An inner WebContents — a guest view, an embedded contents — is not a page
  // a person is browsing, and its outer contents already carries a host. The
  // check is on the content-level relationship rather than on a //chrome type,
  // because //taffy/DEPS forbids naming one.
  if (web_contents->GetOuterWebContents()) {
    return false;
  }
  return true;
}

// static
void TaffyPageIntelligenceHost::AttachIfEligible(
    content::WebContents* web_contents) {
  AttachIfEligible(web_contents, {});
}

// static
void TaffyPageIntelligenceHost::AttachIfEligible(
    content::WebContents* web_contents,
    DownloadDirectoryResolver default_download_directory) {
  if (!IsEligible(web_contents)) {
    return;
  }
  // The one place that looks a dependency up rather than being handed one.
  // It is here, at the embedder's entry point, because the profile-keyed
  // service is an embedder concept and this is the only function in this file
  // that runs inside one. Everything below is handed what it needs.
  CoreServiceManager* profile_core =
      CoreServiceManagerFactory::GetForBrowserContext(
          web_contents->GetBrowserContext());
  CHECK(profile_core);
  AttachWithAuthority(
      web_contents, profile_core->actor_leases(), profile_core->capabilities(),
      profile_core->value_references(), profile_core,
      profile_core->task_journal_sink(), std::move(default_download_directory));
}

void TaffyPageIntelligenceHost::AttachWithAuthority(
    content::WebContents* web_contents,
    ActorLeaseRegistry* actor_leases,
    CapabilityLedger* capabilities,
    ValueReferenceVault* value_references,
    BrowserActionDelegate* browser_actions,
    TaskJournalSink* task_journal,
    DownloadDirectoryResolver default_download_directory) {
  if (!IsEligible(web_contents)) {
    return;
  }
  CHECK(actor_leases);
  CHECK(capabilities);
  // Idempotent: CreateForWebContents is a no-op when one already exists, and
  // the upstream call site is inside AttachTabHelpers' own adoption guard, so
  // a second call is a defect somewhere else rather than something to fail on.
  CreateForWebContents(web_contents, actor_leases, capabilities,
                       value_references, browser_actions, task_journal,
                       std::move(default_download_directory));
}

TaffyPageIntelligenceHost::TaffyPageIntelligenceHost(
    content::WebContents* web_contents,
    ActorLeaseRegistry* actor_leases,
    CapabilityLedger* capabilities,
    ValueReferenceVault* value_references,
    BrowserActionDelegate* browser_actions,
    TaskJournalSink* task_journal,
    DownloadDirectoryResolver default_download_directory)
    : content::WebContentsUserData<TaffyPageIntelligenceHost>(*web_contents),
      content::WebContentsObserver(web_contents) {
  // The broker first: it owns identity and lifetime, and the service asks it
  // what is true. Creating it here is what makes this tab's frames reachable
  // at all — it is a WebContentsObserver, so from this moment navigation mints
  // page epochs and a frame's endpoint can acquire the renderer's remote.
  PageIntelligenceBroker::CreateForWebContents(web_contents);
  auto* broker = PageIntelligenceBroker::FromWebContents(web_contents);
  NavigationLifecycleTracker::CreateForWebContents(web_contents);

  service_ = std::make_unique<PageIntelligenceServiceImpl>(
      web_contents, broker, /*sink=*/this, /*journal=*/task_journal,
      /*observability_sink=*/this, actor_leases, capabilities);

  // Every eligible tab gets one exact-scope, content-free download witness.
  // It observes Chromium's profile manager but emits only for items whose
  // immutable original WebContents is this tab. Installing it here makes the
  // source available before any task action can be requested from this host;
  // each dispatcher still attaches its verifier before initiating a flow.
  browser_effects_ = std::make_unique<TaffyBrowserEffectSource>(web_contents);
  if (default_download_directory) {
    browser_effects_->RegisterDirectoryClassResolver(
        DownloadDestinationKind::kDefaultDownloadsDirectory,
        std::move(default_download_directory));
  }
  if (content::DownloadManager* manager =
          web_contents->GetBrowserContext()->GetDownloadManager()) {
    browser_effects_->Observe(manager);
    service_->SetBrowserEffectSource(browser_effects_.get());
  }

  // The encoder that actually carries a graph. Installing it is what turns
  // GraphPayloadEncoding::kBipContract from a declared enumerator into
  // something a result reports; without it every observation would report
  // kNone and the isolated core would receive an honest description of nothing.
  service_->SetGraphPayloadEncoder(MakeBipGraphPayloadEncoder());

  // The profile's holder of values a person entered into Taffy's own controls
  // (decision 0063). This is the first production caller of the forwarder:
  // without it the dispatcher has no vault, and an envelope naming a held
  // value is refused rather than dispatched with nothing in it.
  service_->SetValueReferenceVault(value_references);
  service_->SetBrowserActionDelegate(browser_actions);
  service_->SetObservedLinkResolver(&observed_links_);

  AllHosts().push_back(this);
}

TaffyPageIntelligenceHost::~TaffyPageIntelligenceHost() {
  // Stop process-wide lookup before tearing down anything a reentrant
  // completion could try to find.
  std::vector<TaffyPageIntelligenceHost*>& hosts = AllHosts();
  hosts.erase(std::remove(hosts.begin(), hosts.end(), this), hosts.end());

  // The service observes the separately attached broker. Destroy it while the
  // broker is alive on the ordinary WebContentsDestroyed path; the broker's
  // explicit destruction callback covers an independently removed broker.
  service_.reset();

  auto pending = std::move(pending_observations_);
  pending_observations_.clear();
  for (auto& pending_entry : pending) {
    std::move(pending_entry.second).Run(std::nullopt);
  }
}

void TaffyPageIntelligenceHost::WebContentsDestroyed() {
  // WebContentsUserData entries are destroyed in an unspecified order after
  // WebContents itself has finished. Remove this entry now, while observer
  // sources and the browser-owned profile authority are still alive. This
  // deletes `this`; no statement may follow the removal.
  GetWebContents().RemoveUserData(UserDataKey());
}

content::WebContents* TaffyPageIntelligenceHost::observed_web_contents() {
  return &GetWebContents();
}

bool TaffyPageIntelligenceHost::HasOpenAssistantDispatch() const {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  return browser_effects_ && browser_effects_->HasOpenDispatch();
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(TaffyPageIntelligenceHost);

}  // namespace taffy
