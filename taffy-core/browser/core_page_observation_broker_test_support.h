// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_PAGE_OBSERVATION_BROKER_TEST_SUPPORT_H_
#define TAFFY_BROWSER_CORE_PAGE_OBSERVATION_BROKER_TEST_SUPPORT_H_

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/ref_counted.h"
#include "taffy/browser/core_page_observation_broker.h"
#include "taffy/common/public/bip_observation.h"

namespace taffy {

// One test-only access seam for constructing the broker without the global
// profile manager registry and for settling observations at exact boundaries.
class CorePageObservationBrokerTestPeer {
 public:
  using ManagerLookup =
      base::RepeatingCallback<CoreServiceManager*(content::BrowserContext*)>;

  static scoped_refptr<CorePageObservationBroker> Create(
      content::BrowserContext* browser_context) {
    return Create(browser_context,
                  base::BindRepeating(
                      [](content::BrowserContext*) -> CoreServiceManager* {
                        return nullptr;
                      }));
  }

  static scoped_refptr<CorePageObservationBroker> Create(
      content::BrowserContext* browser_context,
      ManagerLookup manager_lookup) {
    return base::WrapRefCounted(new CorePageObservationBroker(
        browser_context, std::move(manager_lookup)));
  }

  static void Complete(CorePageObservationBroker& broker,
                       core_service::mojom::EffectEnvelopePtr effect,
                       CorePageObservationBroker::CompletionCallback callback,
                       std::optional<ObservationEnvelope> observation,
                       AuthorizedObservationTarget authorized_target = {}) {
    if (effect) {
      broker.pending_observations_.emplace(
          effect->effect_id,
          CorePageObservationBroker::PendingObservation{
              effect->page_observation ? effect->page_observation->task_id
                                       : std::string(),
              effect->operation ? effect->operation->service_generation : 0u,
              effect->page_observation ? effect->page_observation->tab_id
                                       : std::string(),
              RequestId{}, std::move(authorized_target)});
    }
    broker.OnObservation(std::move(effect), std::move(callback), true,
                         std::move(observation));
  }

  static void Finish(CorePageObservationBroker& broker,
                     core_service::mojom::EffectEnvelopePtr effect,
                     CorePageObservationBroker::CompletionCallback callback,
                     AuthorizedObservationTarget authorized_target,
                     ObservationEnvelope observation,
                     core_service::mojom::MediaObservationResultPtr media) {
    broker.FinishObservation(std::move(effect), std::move(callback), true,
                             std::move(authorized_target),
                             std::move(observation), std::move(media));
  }

  static void CompleteMedia(
      CorePageObservationBroker& broker,
      core_service::mojom::EffectEnvelopePtr effect,
      CorePageObservationBroker::CompletionCallback callback,
      AuthorizedObservationTarget authorized_target,
      ObservationEnvelope observation,
      core_service::mojom::MediaObservationResultPtr media,
      uint32_t suppressed_secret_value_count) {
    broker.OnMediaObservation(std::move(effect), std::move(callback), true,
                              std::move(authorized_target),
                              std::move(observation), std::move(media),
                              suppressed_secret_value_count);
  }
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_PAGE_OBSERVATION_BROKER_TEST_SUPPORT_H_
