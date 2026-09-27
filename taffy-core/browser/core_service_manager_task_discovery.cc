// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "content/public/browser/web_contents.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_task_effect.h"
#include "taffy/browser/taffy_page_intelligence_host.h"

namespace taffy {

namespace service_mojom = core_service::mojom;

namespace {

// Every refusal on the discovery bootstrap path names its branch and nothing
// else: no task, tab, session or page identity reaches the log. A refusal
// here is a durable verdict on the task — the core ends it under one closed
// reason — so the branch that reached it must be readable from a logcat.
void LogDiscoveryRefused(const char* at) {
  LOG(ERROR) << "[taffy_discovery_refused] at=" << at;
}

}  // namespace

void CoreServiceManager::ExecuteTaskDiscoveryBootstrap(
    service_mojom::TaskEffectBindingPtr effect,
    ExecuteTaskEffectCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!effect || !effect->operation || !effect->discovery_bootstrap ||
      effect->kind !=
          service_mojom::TaskReducerEffectKind::kPrepareDiscoveryTab) {
    LogDiscoveryRefused("bootstrap/shape");
    std::move(callback).Run(MakeTaskEffectCompletion(
        effect.get(), service_mojom::TaskEffectCompletionStatus::kRefused));
    return;
  }

  const service_mojom::TaskDiscoveryBootstrapEffect& bootstrap =
      *effect->discovery_bootstrap;
  if (bootstrap.browser_session_id != browser_session_id_) {
    LogDiscoveryRefused("bootstrap/session");
    std::move(callback).Run(MakeTaskEffectCompletion(
        effect.get(), service_mojom::TaskEffectCompletionStatus::kRefused));
    return;
  }
  if (!accepted_approvals_.HasTaskSourceDiscoveryBootstrapAuthority(
          effect->task_id, *effect->operation, service_generation_,
          browser_session_id_, bootstrap.remaining_new_source_cap)) {
    LogDiscoveryRefused("bootstrap/authority");
    std::move(callback).Run(MakeTaskEffectCompletion(
        effect.get(), service_mojom::TaskEffectCompletionStatus::kRefused));
    return;
  }

  // A restored errand's bootstrap reaches here at a cold start, before the
  // activity has resumed and activated its window. The registry would refuse
  // it — it has nowhere to open a tab — and that refusal is a verdict on the
  // task, reached over a window that is a moment away. So an authorized
  // bootstrap waits for the first activation instead. The bound is the
  // service's own in-flight cap, so a core that keeps asking cannot grow the
  // queue without limit.
  if (!HasActiveTaskSourceWindow()) {
    if (deferred_discovery_bootstraps_.size() >=
        service_mojom::kMaxInFlightPerProfile) {
      LogDiscoveryRefused("bootstrap/deferred-capacity");
      std::move(callback).Run(MakeTaskEffectCompletion(
          effect.get(), service_mojom::TaskEffectCompletionStatus::kRefused));
      return;
    }
    LOG(WARNING) << "[taffy_discovery_deferred] waiting=for-window";
    deferred_discovery_bootstraps_.push_back(
        DeferredDiscoveryBootstrap{std::move(effect), std::move(callback)});
    return;
  }

  const uint64_t generation = service_generation_;
  service_mojom::TaskEffectBindingPtr retained = effect.Clone();
  PrepareTaskDiscoveryTab(
      effect->task_id, effect->effect_id, browser_session_id_,
      bootstrap.remaining_new_source_cap,
      base::BindOnce(&CoreServiceManager::OnTaskDiscoveryTabPrepared,
                     weak_factory_.GetWeakPtr(), generation,
                     browser_session_id_, std::move(retained),
                     std::move(callback)));
}

void CoreServiceManager::OnTaskDiscoveryTabPrepared(
    uint64_t generation,
    std::string browser_session_id,
    service_mojom::TaskEffectBindingPtr effect,
    ExecuteTaskEffectCallback callback,
    std::optional<std::string> tab_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!effect || !effect->operation || !effect->discovery_bootstrap) {
    LogDiscoveryRefused("prepared/shape");
    std::move(callback).Run(MakeTaskEffectCompletion(
        effect.get(), service_mojom::TaskEffectCompletionStatus::kRefused));
    return;
  }

  const service_mojom::TaskDiscoveryBootstrapEffect& bootstrap =
      *effect->discovery_bootstrap;
  const char* refused_at = nullptr;
  if (generation != service_generation_) {
    refused_at = "prepared/generation";
  } else if (browser_session_id != browser_session_id_) {
    refused_at = "prepared/session";
  } else if (!tab_id) {
    // The registry answered without a tab: no window active, a second
    // effect for a task that already owns a discovery document, a platform
    // that could not open one, or a task-tab claim that did not publish.
    refused_at = "prepared/no-tab";
  } else if (!ResolveTaskDiscoveryDocument(browser_context_.get(), *tab_id)) {
    refused_at = "prepared/document";
  } else if (!accepted_approvals_.HasTaskSourceDiscoveryBootstrapAuthority(
                 effect->task_id, *effect->operation, generation,
                 browser_session_id, bootstrap.remaining_new_source_cap)) {
    refused_at = "prepared/authority";
  } else {
    TaffyPageIntelligenceHost* host =
        FindPageIntelligenceHost(browser_context_.get(), *tab_id);
    content::WebContents* web_contents =
        host ? host->observed_web_contents() : nullptr;
    if (!IsExactTaskDiscoveryTab(effect->task_id, *tab_id, browser_session_id,
                                 bootstrap.remaining_new_source_cap,
                                 web_contents)) {
      refused_at = "prepared/exact-tab";
    }
  }
  if (refused_at) {
    LogDiscoveryRefused(refused_at);
    std::move(callback).Run(MakeTaskEffectCompletion(
        effect.get(), service_mojom::TaskEffectCompletionStatus::kRefused));
    return;
  }

  auto result = service_mojom::EffectResult::New();
  result->operation = effect->operation.Clone();
  result->effect_id = effect->effect_id;
  result->status = service_mojom::EffectStatus::kCompleted;
  result->kind = service_mojom::EffectKind::kBrowserAction;
  result->browser_action = service_mojom::BrowserActionEffectResult::New();
  result->browser_action->outcome =
      service_mojom::BrowserActionOutcome::kCompleted;
  result->browser_action->discovery_tab_id = *tab_id;
  result->browser_action->browser_session_id = browser_session_id;
  auto completion = MakeTaskEffectCompletion(
      effect.get(), service_mojom::TaskEffectCompletionStatus::kSucceeded);
  completion->effect_result = std::move(result);
  std::move(callback).Run(std::move(completion));
}

void CoreServiceManager::DrainDeferredTaskDiscoveryBootstraps() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Taken off the member first: each bootstrap re-enters the executor, and
  // one that finds no active window after all parks itself again rather than
  // being lost or run twice.
  std::vector<DeferredDiscoveryBootstrap> deferred =
      std::move(deferred_discovery_bootstraps_);
  deferred_discovery_bootstraps_.clear();
  for (DeferredDiscoveryBootstrap& bootstrap : deferred) {
    ExecuteTaskDiscoveryBootstrap(std::move(bootstrap.effect),
                                  std::move(bootstrap.callback));
  }
}

void CoreServiceManager::ResolveDeferredTaskDiscoveryBootstrapsUnavailable() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<DeferredDiscoveryBootstrap> deferred =
      std::move(deferred_discovery_bootstraps_);
  deferred_discovery_bootstraps_.clear();
  for (DeferredDiscoveryBootstrap& bootstrap : deferred) {
    LogDiscoveryRefused("deferred/unavailable");
    std::move(bootstrap.callback)
        .Run(MakeTaskEffectCompletion(
            bootstrap.effect.get(),
            service_mojom::TaskEffectCompletionStatus::kUnavailable));
  }
}

}  // namespace taffy
