// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <vector>

#include "content/public/browser/browser_thread.h"
#include "taffy/components/security/browser/action_authority.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

bool DigestMatches(const std::array<uint8_t, 32>& local,
                   const std::vector<uint8_t>& wire) {
  return wire.size() == local.size() &&
         std::equal(local.begin(), local.end(), wire.begin());
}

bool CommandTypeMatchesOperation(BrowserCommandType command_type,
                                 mojom::TaskActionOperationKind operation) {
  switch (command_type) {
    case BrowserCommandType::kNavigate:
      return operation == mojom::TaskActionOperationKind::kNavigate;
    case BrowserCommandType::kGoBack:
      return operation == mojom::TaskActionOperationKind::kHistoryBack;
    case BrowserCommandType::kGoForward:
      return operation == mojom::TaskActionOperationKind::kHistoryForward;
    case BrowserCommandType::kReload:
      return operation == mojom::TaskActionOperationKind::kReload;
    case BrowserCommandType::kStopLoading:
      return operation == mojom::TaskActionOperationKind::kStopLoading;
    case BrowserCommandType::kOpenTaskTab:
      return operation == mojom::TaskActionOperationKind::kTabsOpen;
    case BrowserCommandType::kSearch:
      return operation == mojom::TaskActionOperationKind::kSearch;
    case BrowserCommandType::kOpenObservedLink:
      return operation == mojom::TaskActionOperationKind::kLinkOpen;
    case BrowserCommandType::kListTaskTabs:
    case BrowserCommandType::kActivateTaskTab:
    case BrowserCommandType::kCloseTaskTab:
    case BrowserCommandType::kStartDownload:
    case BrowserCommandType::kListDownloads:
    case BrowserCommandType::kCancelDownload:
      return false;
  }
  return false;
}

bool CommandSourceMatchesGrant(const AuthorizedBrowserCommand& command,
                               const mojom::MintedCapabilityGrant& grant) {
  const bool opens_observed_link =
      command.command_type == BrowserCommandType::kOpenObservedLink;
  if (!grant.scope || !grant.scope->origin ||
      opens_observed_link != command.source_handle.has_value() ||
      opens_observed_link != grant.scope->node_id.has_value()) {
    return false;
  }
  if (!opens_observed_link) {
    return true;
  }
  if (!grant.scope->origin->serialization) {
    return false;
  }
  const NodeHandle& source = *command.source_handle;
  return grant.operation_kind == mojom::TaskActionOperationKind::kLinkOpen &&
         grant.action_class == mojom::PolicyActionClass::kOpenLink &&
         source.tab_id.value == grant.scope->tab_id &&
         source.frame_id.value == grant.scope->frame_id &&
         source.page_epoch.value == grant.scope->page_epoch &&
         source.graph_revision == grant.scope->required_graph_revision &&
         source.node_id.value == *grant.scope->node_id &&
         source.expected_origin.kind == OriginKind::kTuple &&
         source.expected_origin.serialization ==
             *grant.scope->origin->serialization;
}

bool CommandDiscoveryMatchesGrant(const AuthorizedBrowserCommand& command,
                                  const mojom::MintedCapabilityGrant& grant) {
  const std::optional<TaskDiscoveryCapabilityBinding>& command_discovery =
      command.capability.task_discovery;
  if (!grant.discovery) {
    if (command_discovery || !grant.scope || !grant.scope->origin) {
      return false;
    }
    // A grant with no discovery authority ordinarily stands on a page, and a
    // page's origin is a tuple. There is one other document a task may act
    // from: one with no site of its own - the error document Chromium writes
    // when an address does not answer - and the only thing it may do from
    // there is leave. Policy decided that move from its destination, and
    // nothing is inferred from the opaque identity here beyond its shape
    // (decision 0176).
    if (grant.scope->origin->kind == mojom::PolicyOriginKind::kOpaque) {
      return (command.command_type == BrowserCommandType::kNavigate ||
              command.command_type == BrowserCommandType::kSearch) &&
             grant.scope->origin->opaque_id.has_value() &&
             !grant.scope->origin->opaque_id->empty() &&
             !grant.scope->origin->serialization.has_value() &&
             !grant.scope->node_id.has_value() &&
             grant.scope->required_graph_revision == 0u &&
             grant.scope->destination_address.has_value();
    }
    return grant.scope->origin->kind == mojom::PolicyOriginKind::kTuple &&
           grant.scope->origin->serialization.has_value() &&
           !grant.scope->origin->opaque_id.has_value();
  }
  const bool discovery_operation =
      (command.command_type == BrowserCommandType::kSearch ||
       command.command_type == BrowserCommandType::kNavigate) &&
      CommandTypeMatchesOperation(command.command_type, grant.operation_kind);
  if (!command_discovery || !grant.scope || !grant.scope->origin ||
      !discovery_operation ||
      grant.scope->origin->kind != mojom::PolicyOriginKind::kOpaque ||
      !grant.scope->origin->opaque_id || grant.scope->origin->serialization) {
    return false;
  }
  return command_discovery->tab_id.value == grant.discovery->discovery_tab_id &&
         command_discovery->tab_id == command.tab_id &&
         command_discovery->frame_id.value == grant.scope->frame_id &&
         command_discovery->page_epoch.value == grant.scope->page_epoch &&
         command_discovery->opaque_origin_id ==
             *grant.scope->origin->opaque_id &&
         command_discovery->browser_session_id ==
             grant.discovery->browser_session_id &&
         command_discovery->remaining_new_source_cap ==
             grant.discovery->remaining_new_source_cap;
}

bool CommandClassMatchesOperation(BrowserCommandType command_type,
                                  mojom::PolicyActionClass action_class) {
  if (command_type == BrowserCommandType::kListTaskTabs ||
      command_type == BrowserCommandType::kActivateTaskTab ||
      command_type == BrowserCommandType::kCloseTaskTab) {
    return false;
  }
  if (command_type == BrowserCommandType::kOpenTaskTab) {
    return action_class == mojom::PolicyActionClass::kCreateTaskTab;
  }
  if (command_type == BrowserCommandType::kGoBack ||
      command_type == BrowserCommandType::kGoForward ||
      command_type == BrowserCommandType::kReload ||
      command_type == BrowserCommandType::kStopLoading) {
    return action_class == mojom::PolicyActionClass::kControlTab;
  }
  return action_class == mojom::PolicyActionClass::kOpenLink;
}

bool CommandDestinationMatchesGrant(const AuthorizedBrowserCommand& command,
                                    const mojom::PolicyCapabilityScope& scope) {
  const bool controls_tab =
      command.command_type == BrowserCommandType::kGoBack ||
      command.command_type == BrowserCommandType::kGoForward ||
      command.command_type == BrowserCommandType::kReload ||
      command.command_type == BrowserCommandType::kStopLoading;
  if (controls_tab) {
    return command.argument.empty() && !scope.destination_scope &&
           !scope.destination_address;
  }
  return scope.destination_scope && scope.destination_address &&
         scope.destination_scope->serialization &&
         scope.destination_scope->kind == mojom::PolicyOriginKind::kTuple &&
         *scope.destination_address == command.argument &&
         !command.argument.empty();
}

bool CommandMatchesGrant(const AuthorizedBrowserCommand& command,
                         const mojom::MintedCapabilityGrant& grant) {
  return grant.scope && grant.scope->origin &&
         CommandDestinationMatchesGrant(command, *grant.scope) &&
         command.action_digest.value == grant.proposal_digest &&
         DigestMatches(command.canonical_intent_digest,
                       grant.canonical_intent_digest) &&
         command.task_id.value == grant.task_id &&
         command.action_id.value == grant.action_id &&
         command.idempotency_key == grant.idempotency_key &&
         command.capability.capability_reference.value == grant.capability_id &&
         command.capability.actor_lease_id.value == grant.actor_lease_id &&
         command.capability.policy_version == grant.policy_version &&
         command.capability.expires_at_monotonic_ms ==
             grant.expires_at_monotonic_ms &&
         command.tab_id.value == grant.scope->tab_id &&
         CommandTypeMatchesOperation(command.command_type,
                                     grant.operation_kind) &&
         CommandClassMatchesOperation(command.command_type,
                                      grant.action_class) &&
         CommandSourceMatchesGrant(command, grant) &&
         CommandDiscoveryMatchesGrant(command, grant) &&
         (command.command_type == BrowserCommandType::kSearch) ==
             command.transient_search_query.has_value() &&
         (!command.transient_search_query ||
          (!command.transient_search_query->empty() &&
           command.transient_search_query->size() <=
               mojom::kMaxTransientSearchQueryBytes));
}

}  // namespace

CapabilityAdmission CapabilityLedger::AdmitTaskBrowserCommand(
    const AuthorizedBrowserCommand& command,
    const ActorLeaseRegistry& leases,
    const TabId& tab_id,
    base::TimeTicks now) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!command.capability.capability_reference.is_valid() ||
      !command.action_digest.is_valid()) {
    return CapabilityAdmission::kMalformed;
  }

  auto record_it = records_.find(command.capability.capability_reference);
  if (record_it == records_.end() || !record_it->second.grant) {
    return CapabilityAdmission::kMalformed;
  }
  Record& record = record_it->second;
  if (record.state != State::kRegistered) {
    return CapabilityAdmission::kAlreadySpent;
  }
  if (record.expires_at <= now) {
    return CapabilityAdmission::kExpired;
  }
  const mojom::MintedCapabilityGrant& registered = *record.grant;
  if (!CommandMatchesGrant(command, registered)) {
    return CapabilityAdmission::kDigestMismatch;
  }
  if (registered.scope->tab_id != tab_id.value) {
    return CapabilityAdmission::kLeaseNotForThisTab;
  }
  if (!leases.IsValidForGrant(command.capability.actor_lease_id,
                              mojom::AuthoritySubjectKind::kTask,
                              registered.task_id, tab_id, active_profile_id_,
                              active_generation_, record.expires_at, now)) {
    return CapabilityAdmission::kLeaseNotForThisTab;
  }
  record.state = State::kInFlight;
  return CapabilityAdmission::kAdmitted;
}

}  // namespace taffy
