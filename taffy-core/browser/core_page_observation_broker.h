// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_PAGE_OBSERVATION_BROKER_H_
#define TAFFY_BROWSER_CORE_PAGE_OBSERVATION_BROKER_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/components/security/browser/action_authority.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/contracts/core-service/core_service.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace content {
class BrowserContext;
}

namespace taffy {

class CoreServiceManager;
class ProfilePageMediaStore;

// Profile-partitioned adapter from a typed Core Service observation effect to
// the existing per-WebContents BIP host. It never exposes a WebContents or a
// renderer pipe to the utility process.
class CorePageObservationBroker
    : public base::RefCounted<CorePageObservationBroker> {
 public:
  using CompletionCallback =
      base::OnceCallback<void(core_service::mojom::EffectResultPtr)>;

  explicit CorePageObservationBroker(content::BrowserContext* browser_context);
  CorePageObservationBroker(
      content::BrowserContext* browser_context,
      scoped_refptr<ProfilePageMediaStore> page_media_store);
  CorePageObservationBroker(const CorePageObservationBroker&) = delete;
  CorePageObservationBroker& operator=(const CorePageObservationBroker&) =
      delete;
  void Dispatch(core_service::mojom::EffectEnvelopePtr effect,
                CompletionCallback callback);
  // Direct export keeps the graph opaque in C++ and therefore skips the
  // inspector DTO projection performed for the visible inspector.
  void DispatchForExport(core_service::mojom::EffectEnvelopePtr effect,
                         CompletionCallback callback);
  void CancelEffect(std::string_view effect_id, uint64_t generation);
  void CancelTask(std::string_view task_id, uint64_t generation);
  void CancelGeneration(uint64_t generation);

  // Shared only with the profile's model transport. The store itself enforces
  // one-use task/generation/disclosure/MIME binding, so holding this reference
  // grants no ability to resolve a media handle without the exact claim.
  scoped_refptr<ProfilePageMediaStore> page_media_store() const;

  // Claims the one UI-safe projection created for a completed direct effect.
  // Raw BIP bytes are not returned by this API and the entry is erased even
  // when the caller arrives after a competing completion.
  core_api::mojom::PageInspectorSnapshotViewPtr TakeDirectProjection(
      const std::string& effect_id);

 private:
  using ManagerLookup =
      base::RepeatingCallback<CoreServiceManager*(content::BrowserContext*)>;

  friend class CorePageObservationBrokerTestPeer;
  friend class base::RefCounted<CorePageObservationBroker>;
  CorePageObservationBroker(content::BrowserContext* browser_context,
                            ManagerLookup manager_lookup);
  CorePageObservationBroker(
      content::BrowserContext* browser_context,
      scoped_refptr<ProfilePageMediaStore> page_media_store,
      ManagerLookup manager_lookup);
  ~CorePageObservationBroker();

  CoreServiceManager* LookupManager() const;

  void OnObservation(core_service::mojom::EffectEnvelopePtr effect,
                     CompletionCallback callback,
                     bool create_direct_projection,
                     std::optional<ObservationEnvelope> observation);
  void OnMediaObservation(core_service::mojom::EffectEnvelopePtr effect,
                          CompletionCallback callback,
                          bool create_direct_projection,
                          AuthorizedObservationTarget authorized_target,
                          ObservationEnvelope observation,
                          core_service::mojom::MediaObservationResultPtr media,
                          uint32_t suppressed_secret_value_count);
  void FinishObservation(core_service::mojom::EffectEnvelopePtr effect,
                         CompletionCallback callback,
                         bool create_direct_projection,
                         AuthorizedObservationTarget authorized_target,
                         ObservationEnvelope observation,
                         core_service::mojom::MediaObservationResultPtr media);
  core_service::mojom::EffectResultPtr MakeUnavailable(
      const core_service::mojom::EffectEnvelope& effect) const;
  void DispatchInternal(core_service::mojom::EffectEnvelopePtr effect,
                        CompletionCallback callback,
                        bool create_direct_projection);

  struct PendingObservation {
    std::string task_id;
    uint64_t generation = 0u;
    std::string tab_id;
    RequestId request_id;
    std::optional<AuthorizedObservationTarget> authorized_target;
  };

  struct PendingMediaObservation {
    std::string task_id;
    uint64_t generation = 0u;
    base::OnceClosure cancel;
  };

  const raw_ptr<content::BrowserContext> browser_context_;
  const scoped_refptr<ProfilePageMediaStore> page_media_store_;
  const ManagerLookup manager_lookup_;
  std::map<std::string, PendingObservation> pending_observations_;
  std::map<std::string, PendingMediaObservation> pending_media_observations_;
  std::map<std::string, core_api::mojom::PageInspectorSnapshotViewPtr>
      direct_projections_;
  base::WeakPtrFactory<CorePageObservationBroker> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_PAGE_OBSERVATION_BROKER_H_
