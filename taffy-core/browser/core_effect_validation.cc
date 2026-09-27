// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_effect_validation.h"

#include <algorithm>
#include <string>
#include <string_view>

#include "taffy/browser/core_account_effect_validation.h"
#include "taffy/browser/core_asset_validation.h"
#include "taffy/browser/core_model_effect_validation.h"
#include "taffy/browser/core_storage_effect_validation.h"
#include "taffy/browser/core_tool_validation.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

bool IsIdentifier(const std::string& value, size_t maximum) {
  return !value.empty() && value.size() <= maximum;
}

bool IsLowerHexDigest(const std::string& value) {
  return value.size() == 64u &&
         std::all_of(value.begin(), value.end(), [](char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

bool IsDirectIntentId(std::string_view value) {
  return value.starts_with("direct-intent-") &&
         value.size() <= mojom::kMaxAuthoritySubjectIdBytes;
}

bool IsValidPageObservation(const mojom::EffectEnvelope& effect,
                            size_t max_identifier_bytes,
                            size_t max_effect_bytes) {
  const auto& body = *effect.page_observation;
  if (body.scope != mojom::ObservationScope::kCurrentDocument ||
      body.max_bytes == 0u || body.max_bytes > max_effect_bytes ||
      !IsIdentifier(body.tab_id, max_identifier_bytes) ||
      !IsIdentifier(body.frame_id, max_identifier_bytes) ||
      !IsIdentifier(body.page_epoch, max_identifier_bytes) ||
      !IsIdentifier(body.capability_id, max_identifier_bytes) ||
      !IsLowerHexDigest(body.proposal_digest) ||
      body.idempotency_key != effect.operation->idempotency_key ||
      !body.authority_subject ||
      !IsIdentifier(body.authority_subject->authority_subject_id,
                    mojom::kMaxAuthoritySubjectIdBytes)) {
    return false;
  }
  switch (body.authority_subject->kind) {
    case mojom::AuthoritySubjectKind::kTask:
      return IsIdentifier(body.task_id, max_identifier_bytes) &&
             !IsDirectIntentId(body.task_id) &&
             body.task_id == body.authority_subject->authority_subject_id &&
             IsIdentifier(body.action_id, max_identifier_bytes);
    case mojom::AuthoritySubjectKind::kDirectUserIntent:
      return IsDirectIntentId(body.authority_subject->authority_subject_id) &&
             body.task_id.empty() && body.action_id.empty() &&
             effect.operation->task_revision == 0u &&
             effect.retry_class == mojom::RetryClass::kIdempotent &&
             body.max_bytes == mojom::kMaxDirectObservationTotalBytes &&
             body.max_nodes == mojom::kMaxDirectObservationNodes &&
             body.max_text_bytes == mojom::kMaxDirectObservationTextBytes &&
             body.max_frames == mojom::kMaxDirectObservationFrames &&
             body.deadline_ms == mojom::kMaxDirectObservationDeadlineMs;
  }
  return false;
}

size_t EffectBodyCount(const mojom::EffectEnvelope& effect) {
  return static_cast<size_t>(!!effect.storage_commit) +
         static_cast<size_t>(!!effect.page_observation) +
         static_cast<size_t>(!!effect.model_request) +
         static_cast<size_t>(!!effect.network_request) +
         static_cast<size_t>(!!effect.browser_action) +
         static_cast<size_t>(!!effect.tool_job) +
         static_cast<size_t>(!!effect.secure_store) +
         static_cast<size_t>(!!effect.auth_surface) +
         static_cast<size_t>(!!effect.permission_request) +
         static_cast<size_t>(!!effect.asset_delivery) +
         static_cast<size_t>(!!effect.catalog_fetch) +
         static_cast<size_t>(!!effect.provider_listing_fetch) +
         static_cast<size_t>(!!effect.composer_completion) +
         static_cast<size_t>(!!effect.custom_endpoint_probe);
}

size_t ResultBodyCount(const mojom::EffectResult& result) {
  return static_cast<size_t>(!!result.storage) +
         static_cast<size_t>(!!result.observation) +
         static_cast<size_t>(!!result.model) +
         static_cast<size_t>(!!result.network) +
         static_cast<size_t>(!!result.browser_action) +
         static_cast<size_t>(!!result.tool) +
         static_cast<size_t>(!!result.secure_store) +
         static_cast<size_t>(!!result.auth_surface) +
         static_cast<size_t>(!!result.permission) +
         static_cast<size_t>(!!result.asset_delivery) +
         static_cast<size_t>(!!result.catalog) +
         static_cast<size_t>(!!result.provider_listing) +
         static_cast<size_t>(!!result.composer_completion) +
         static_cast<size_t>(!!result.custom_endpoint_probe);
}

bool HasMatchingEffectBody(const mojom::EffectEnvelope& effect) {
  if (EffectBodyCount(effect) != 1u) {
    return false;
  }
  switch (effect.kind) {
    case mojom::EffectKind::kStorageCommit:
      return !!effect.storage_commit;
    case mojom::EffectKind::kPageObservation:
      return !!effect.page_observation;
    case mojom::EffectKind::kModelRequest:
      return !!effect.model_request;
    case mojom::EffectKind::kNetworkRequest:
      return !!effect.network_request;
    case mojom::EffectKind::kBrowserAction:
      return !!effect.browser_action;
    case mojom::EffectKind::kToolJob:
      return !!effect.tool_job;
    case mojom::EffectKind::kSecureStore:
      return !!effect.secure_store;
    case mojom::EffectKind::kOpenAuthSurface:
      return !!effect.auth_surface;
    case mojom::EffectKind::kRequestPermission:
      return !!effect.permission_request;
    case mojom::EffectKind::kDeliverAsset:
      return !!effect.asset_delivery;
    case mojom::EffectKind::kFetchCatalog:
      return !!effect.catalog_fetch;
    case mojom::EffectKind::kFetchProviderListing:
      return !!effect.provider_listing_fetch;
    case mojom::EffectKind::kDeliverComposerCompletion:
      return !!effect.composer_completion;
    case mojom::EffectKind::kProbeCustomEndpoint:
      return !!effect.custom_endpoint_probe;
  }
  return false;
}

bool HasMatchingResultBody(const mojom::EffectResult& result) {
  if (ResultBodyCount(result) != 1u) {
    return false;
  }
  switch (result.kind) {
    case mojom::EffectKind::kStorageCommit:
      return !!result.storage;
    case mojom::EffectKind::kPageObservation:
      return !!result.observation;
    case mojom::EffectKind::kModelRequest:
      return !!result.model;
    case mojom::EffectKind::kNetworkRequest:
      return !!result.network;
    case mojom::EffectKind::kBrowserAction:
      return !!result.browser_action;
    case mojom::EffectKind::kToolJob:
      return !!result.tool;
    case mojom::EffectKind::kSecureStore:
      return !!result.secure_store;
    case mojom::EffectKind::kOpenAuthSurface:
      return !!result.auth_surface;
    case mojom::EffectKind::kRequestPermission:
      return !!result.permission;
    case mojom::EffectKind::kDeliverAsset:
      return !!result.asset_delivery;
    case mojom::EffectKind::kFetchCatalog:
      return !!result.catalog;
    case mojom::EffectKind::kFetchProviderListing:
      return !!result.provider_listing;
    case mojom::EffectKind::kDeliverComposerCompletion:
      return !!result.composer_completion;
    case mojom::EffectKind::kProbeCustomEndpoint:
      return !!result.custom_endpoint_probe;
  }
  return false;
}

bool ValidateTypedBody(const mojom::EffectEnvelope& effect,
                       size_t max_identifier_bytes,
                       size_t max_effect_bytes) {
  if (effect.storage_commit) {
    return IsValidCoreStorageCommit(*effect.storage_commit,
                                    max_identifier_bytes, max_effect_bytes);
  }
  if (effect.page_observation) {
    return IsValidPageObservation(effect, max_identifier_bytes,
                                  max_effect_bytes);
  }
  if (effect.model_request) {
    return IsValidCoreModelRequest(*effect.model_request, max_identifier_bytes,
                                   max_effect_bytes);
  }
  if (effect.network_request) {
    return IsValidCoreAccountEffectBody(effect, max_identifier_bytes,
                                        max_effect_bytes);
  }
  if (effect.browser_action) {
    return IsIdentifier(effect.browser_action->action_id,
                        max_identifier_bytes) &&
           IsIdentifier(effect.browser_action->grant_reference,
                        max_identifier_bytes) &&
           IsLowerHexDigest(effect.browser_action->origin_scope_digest);
  }
  if (effect.tool_job) {
    return IsValidCoreToolJob(*effect.tool_job, max_identifier_bytes,
                              max_effect_bytes);
  }
  if (effect.secure_store) {
    return IsValidCoreAccountEffectBody(effect, max_identifier_bytes,
                                        max_effect_bytes);
  }
  if (effect.auth_surface) {
    return IsValidCoreAccountEffectBody(effect, max_identifier_bytes,
                                        max_effect_bytes);
  }
  if (effect.asset_delivery) {
    return IsValidCoreAssetDelivery(*effect.asset_delivery,
                                    max_identifier_bytes);
  }
  if (effect.provider_listing_fetch) {
    // A person's own endpoint, bounded exactly as the command that stored it
    // was. The response ceiling is the core's own request rather than a
    // browser default, so it is checked against the contract's limit here
    // rather than substituted for one.
    const mojom::ProviderListingFetchEffect& listing =
        *effect.provider_listing_fetch;
    return IsIdentifier(listing.provider_id, mojom::kMaxProviderIdBytes) &&
           !listing.endpoint.empty() &&
           listing.endpoint.size() <= mojom::kMaxProviderEndpointBytes &&
           listing.max_response_bytes > 0u &&
           listing.max_response_bytes <= mojom::kMaxProviderListingBytes;
  }
  if (effect.custom_endpoint_probe) {
    // The same shape asked of an address no provider record exists for yet;
    // core_model_effect_validation.cc owns the rule.
    return IsValidCustomEndpointProbe(*effect.custom_endpoint_probe);
  }
  if (effect.composer_completion) {
    // Absent text is the honest answer for "there is no continuation", and is
    // not the same as an empty one; both are accepted and the surface decides
    // what to draw.
    const mojom::ComposerCompletionEffect& completion =
        *effect.composer_completion;
    return IsIdentifier(completion.request_id, max_identifier_bytes) &&
           (!completion.text ||
            completion.text->size() <= mojom::kMaxComposerCompletionBytes);
  }
  return effect.permission_request &&
         IsIdentifier(effect.permission_request->request_id,
                      max_identifier_bytes);
}

size_t ResultByteSize(const mojom::EffectResult& result) {
  if (result.observation) {
    return result.observation->graph_payload.size();
  }
  if (result.model) {
    return result.model->completion.size();
  }
  if (result.provider_listing) {
    return result.provider_listing->body.size();
  }
  return CoreAccountResultByteSize(result);
}

bool IsValidModelResult(const mojom::EffectEnvelope& effect,
                        const mojom::EffectResult& result) {
  if (!result.model) {
    return true;
  }
  if (!effect.model_request ||
      result.model->model_id != effect.model_request->model_id) {
    return false;
  }
  const mojom::ModelFailurePtr& failure = result.model->failure;
  if (!failure) {
    return true;
  }
  if (effect.model_request->task_id.empty() ||
      effect.model_request->wire_api == mojom::ProviderWireApi::kManaged ||
      effect.model_request->media_attachment_handle ||
      result.status == mojom::EffectStatus::kCompleted ||
      result.status == mojom::EffectStatus::kCancelled ||
      result.status == mojom::EffectStatus::kOutcomeUnknown ||
      !result.model->completion.empty() || result.model->streamed ||
      (!failure->has_retry_after && failure->retry_after_millis != 0u) ||
      result.model->provider_http_status > 599u) {
    return false;
  }
  const uint32_t status = result.model->provider_http_status;
  switch (failure->error_class) {
    case mojom::ModelErrorClass::kAuth:
      return result.status == mojom::EffectStatus::kDenied &&
             (status == 401u || status == 403u);
    case mojom::ModelErrorClass::kQuota:
      return result.status == mojom::EffectStatus::kDenied &&
             (status == 402u || status == 429u);
    case mojom::ModelErrorClass::kOverloaded:
      return result.status == mojom::EffectStatus::kUnavailable &&
             status >= 500u;
    case mojom::ModelErrorClass::kInvalidRequest:
      return result.status == mojom::EffectStatus::kInvalidResult &&
             (status == 0u || status >= 300u);
    case mojom::ModelErrorClass::kNetwork:
      return result.status == mojom::EffectStatus::kUnavailable &&
             status == 408u;
    case mojom::ModelErrorClass::kOverflow:
      return result.status == mojom::EffectStatus::kResourceLimit;
    case mojom::ModelErrorClass::kCanceled:
    case mojom::ModelErrorClass::kUnknown:
      return false;
  }
  return false;
}

}  // namespace

bool IsValidCoreEffectEnvelope(const mojom::EffectEnvelope& effect,
                               uint64_t active_generation,
                               size_t max_identifier_bytes,
                               size_t max_effect_bytes) {
  return effect.operation && active_generation != 0u &&
         effect.operation->service_generation == active_generation &&
         IsIdentifier(effect.operation->operation_id, max_identifier_bytes) &&
         IsIdentifier(effect.operation->idempotency_key,
                      max_identifier_bytes) &&
         IsIdentifier(effect.effect_id, max_identifier_bytes) &&
         (!effect.model_request || !effect.model_request->probe ||
          effect.retry_class == mojom::RetryClass::kNever) &&
         HasMatchingEffectBody(effect) &&
         ValidateTypedBody(effect, max_identifier_bytes, max_effect_bytes);
}

bool IsValidCoreEffectResult(const mojom::EffectEnvelope& effect,
                             const mojom::EffectResult& result,
                             size_t max_effect_bytes) {
  return result.operation && result.effect_id == effect.effect_id &&
         result.kind == effect.kind && HasMatchingResultBody(result) &&
         result.operation->operation_id == effect.operation->operation_id &&
         result.operation->service_generation ==
             effect.operation->service_generation &&
         result.operation->task_revision == effect.operation->task_revision &&
         result.operation->idempotency_key ==
             effect.operation->idempotency_key &&
         (!result.tool || (effect.tool_job &&
                           IsValidCoreToolResult(*result.tool, *effect.tool_job,
                                                 256u, max_effect_bytes))) &&
         (!result.permission || (effect.permission_request &&
                                 result.permission->request_id ==
                                     effect.permission_request->request_id &&
                                 result.permission->permission ==
                                     effect.permission_request->permission)) &&
         (!result.asset_delivery ||
          (effect.asset_delivery &&
           IsValidCoreAssetDeliveryResult(*result.asset_delivery,
                                          *effect.asset_delivery))) &&
         HasMatchingCoreAccountResult(effect, result) &&
         IsValidModelResult(effect, result) &&
         ResultByteSize(result) <= max_effect_bytes;
}

}  // namespace taffy
