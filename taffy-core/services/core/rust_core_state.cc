// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/rust_core_state.h"
#include "taffy/services/core/rust_core_state_permissions.h"

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include "base/logging.h"
#include "taffy/services/core/rust_core_model_registration.h"
#include "taffy/services/core/rust_core_task_effect.h"

namespace taffy::core_service_internal {
namespace {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;

bool IsValidIdentifier(const rust::String& value) {
  return !value.empty() && value.size() <= mojom::kMaxIdentifierBytes;
}

bool IsLowerHexSha256(const rust::String& value) {
  return value.size() == 64u &&
         std::all_of(value.begin(), value.end(), [](char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

std::optional<mojom::TaskProviderRoute> ProviderRouteFromWire(uint8_t value) {
  switch (value) {
    case 0:
      return mojom::TaskProviderRoute::kNotConfigured;
    case 1:
      return mojom::TaskProviderRoute::kDirectUserKey;
    case 2:
      return mojom::TaskProviderRoute::kManagedService;
    case 3:
      return mojom::TaskProviderRoute::kNoModelRequired;
    default:
      return std::nullopt;
  }
}

std::optional<mojom::TaskSettlementKind> SettlementKindFromWire(uint8_t value) {
  switch (value) {
    case 0:
      return mojom::TaskSettlementKind::kPause;
    case 1:
      return mojom::TaskSettlementKind::kCancel;
    default:
      return std::nullopt;
  }
}

std::optional<mojom::TerminalTaskKind> TerminalKindFromWire(uint8_t value) {
  switch (value) {
    case 0:
      return mojom::TerminalTaskKind::kCompleted;
    case 1:
      return mojom::TerminalTaskKind::kPartial;
    case 2:
      return mojom::TerminalTaskKind::kFailed;
    case 3:
      return mojom::TerminalTaskKind::kCancelled;
    default:
      return std::nullopt;
  }
}

}  // namespace

// A state that will not project is the difference between a browser with an
// AI plane and one without: `rust_core.cc` answers "state-native-projection"
// and the utility process exits, so every provider surface loads forever. One
// bare `std::nullopt` across a dozen guards cannot say which fact was wrong,
// and no gate can reach this path — only a running core on a real profile
// does. So the refusal names its own branch.
#define TAFFY_REFUSE_STATE(where)                                    \
  do {                                                               \
    LOG(ERROR) << "[taffy_core_state_projection_refused] at=" << (where); \
    return std::nullopt;                                             \
  } while (0)


std::optional<CoreStatePublication> ToStatePublication(
    const bridge::BridgeState& in) {
  if (in.service_generation == 0u || in.sequence == 0u ||
      in.core_status_schema_version == 0u || in.payload.empty() ||
      in.task_revisions.size() > mojom::kMaxTaskRevisionsPerProfile ||
      in.pending_approvals.size() > mojom::kMaxPendingApprovalsPerProfile ||
      in.task_settlements.size() > mojom::kMaxTaskRevisionsPerProfile ||
      in.pending_permissions.size() > mojom::kMaxPendingPermissionsPerProfile ||
      in.terminal_tasks.size() > mojom::kMaxTaskRevisionsPerProfile ||
      in.accepted_task_consents.size() > mojom::kMaxTaskRevisionsPerProfile ||
      in.committed_action_approvals.size() >
          mojom::kMaxPendingApprovalsPerProfile ||
      in.task_effects.size() > mojom::kMaxTaskEffectsPerState) {
    TAFFY_REFUSE_STATE("header");
  }
  CoreStatePublication publication;
  publication.state = mojom::CoreStateUpdate::New();
  publication.state->service_generation = in.service_generation;
  publication.state->sequence = in.sequence;
  publication.state->core_status_schema_version = in.core_status_schema_version;
  publication.state->payload.assign(in.payload.begin(), in.payload.end());
  if (!PopulateModelArtifactRegistrations(in.model_artifacts,
                                          publication.state.get())) {
    TAFFY_REFUSE_STATE("model-artifacts");
  }
  publication.browser_bindings = mojom::CoreStateBrowserBindings::New();
  publication.browser_bindings->service_generation = in.service_generation;
  publication.browser_bindings->state_sequence = in.sequence;
  std::map<std::string, uint64_t> revisions;
  for (const bridge::BridgeTaskRevision& task : in.task_revisions) {
    if (!IsValidIdentifier(task.task_id) ||
        task.service_generation != in.service_generation ||
        task.task_revision == 0u ||
        task.allowed_controls.size() > mojom::kMaxTaskControls ||
        !revisions.emplace(std::string(task.task_id), task.task_revision)
             .second) {
      TAFFY_REFUSE_STATE("task-revision");
    }
    auto binding = mojom::TaskRevisionBinding::New();
    binding->task_id = std::string(task.task_id);
    binding->service_generation = task.service_generation;
    binding->task_revision = task.task_revision;
    for (uint8_t control : task.allowed_controls) {
      switch (control) {
        case 0:
          binding->allowed_controls.push_back(mojom::TaskControlKind::kPause);
          break;
        case 1:
          binding->allowed_controls.push_back(mojom::TaskControlKind::kResume);
          break;
        case 2:
          binding->allowed_controls.push_back(
              mojom::TaskControlKind::kTakeOver);
          break;
        case 3:
          binding->allowed_controls.push_back(mojom::TaskControlKind::kStop);
          break;
        default:
          TAFFY_REFUSE_STATE("task-control");
      }
    }
    publication.browser_bindings->task_revisions.push_back(std::move(binding));
  }
  std::set<std::pair<std::string, std::string>> approval_keys;
  for (const bridge::BridgePendingApproval& approval : in.pending_approvals) {
    const auto revision = revisions.find(std::string(approval.task_id));
    if (!IsValidIdentifier(approval.task_id) ||
        !IsValidIdentifier(approval.action_id) ||
        approval.proposal_digest.size() != 64u ||
        approval.service_generation != in.service_generation ||
        revision == revisions.end() ||
        revision->second != approval.task_revision ||
        !approval_keys
             .emplace(std::string(approval.task_id),
                      std::string(approval.action_id))
             .second) {
      TAFFY_REFUSE_STATE("pending-approval");
    }
    auto binding = mojom::PendingApprovalBinding::New();
    binding->task_id = std::string(approval.task_id);
    binding->action_id = std::string(approval.action_id);
    binding->proposal_digest = std::string(approval.proposal_digest);
    binding->service_generation = approval.service_generation;
    binding->task_revision = approval.task_revision;
    publication.browser_bindings->pending_approvals.push_back(
        std::move(binding));
  }
  std::set<std::string> settlement_tasks;
  for (const bridge::BridgeTaskSettlement& settlement : in.task_settlements) {
    const auto kind = SettlementKindFromWire(settlement.kind);
    const auto revision = revisions.find(std::string(settlement.task_id));
    if (!kind || !IsValidIdentifier(settlement.task_id) ||
        settlement.service_generation != in.service_generation ||
        revision == revisions.end() ||
        revision->second != settlement.task_revision ||
        !settlement_tasks.emplace(std::string(settlement.task_id)).second) {
      TAFFY_REFUSE_STATE("task-settlement");
    }
    auto binding = mojom::TaskSettlementBinding::New();
    binding->task_id = std::string(settlement.task_id);
    binding->service_generation = settlement.service_generation;
    binding->task_revision = settlement.task_revision;
    binding->kind = *kind;
    publication.browser_bindings->task_settlements.push_back(
        std::move(binding));
  }
  std::set<std::string> permission_requests;
  std::set<std::string> permission_tasks;
  for (const bridge::BridgePendingPermission& pending :
       in.pending_permissions) {
    const auto permission = PermissionFromWire(pending.permission);
    const auto revision = revisions.find(std::string(pending.task_id));
    if (!permission || !IsValidIdentifier(pending.task_id) ||
        !IsValidIdentifier(pending.request_id) ||
        pending.service_generation != in.service_generation ||
        pending.deadline_monotonic_ms == 0u || pending.deadline_utc_ms == 0u ||
        !IsValidIdentifier(pending.browser_session_id) ||
        revision == revisions.end() ||
        revision->second != pending.task_revision ||
        !permission_requests.emplace(std::string(pending.request_id)).second ||
        !permission_tasks.emplace(std::string(pending.task_id)).second) {
      TAFFY_REFUSE_STATE("pending-permission");
    }
    auto binding = mojom::PendingPermissionBinding::New();
    binding->task_id = std::string(pending.task_id);
    binding->request_id = std::string(pending.request_id);
    binding->permission = *permission;
    binding->service_generation = pending.service_generation;
    binding->task_revision = pending.task_revision;
    binding->deadline_monotonic_ms = pending.deadline_monotonic_ms;
    binding->deadline_utc_ms = pending.deadline_utc_ms;
    binding->browser_session_id = std::string(pending.browser_session_id);
    publication.browser_bindings->pending_permissions.push_back(
        std::move(binding));
    publication.effects.push_back(ToPermissionEffect(pending, *permission));
  }
  std::set<std::string> terminal_tasks;
  for (const bridge::BridgeTerminalTask& terminal : in.terminal_tasks) {
    const auto kind = TerminalKindFromWire(terminal.kind);
    const auto revision = revisions.find(std::string(terminal.task_id));
    if (!kind || !IsValidIdentifier(terminal.task_id) ||
        terminal.service_generation != in.service_generation ||
        revision == revisions.end() ||
        revision->second != terminal.task_revision ||
        !terminal_tasks.emplace(std::string(terminal.task_id)).second) {
      TAFFY_REFUSE_STATE("terminal-task");
    }
    auto binding = mojom::TerminalTaskBinding::New();
    binding->task_id = std::string(terminal.task_id);
    binding->service_generation = terminal.service_generation;
    binding->task_revision = terminal.task_revision;
    binding->kind = *kind;
    publication.browser_bindings->terminal_tasks.push_back(std::move(binding));
  }
  std::set<std::string> consent_tasks;
  for (const bridge::BridgeAcceptedTaskConsent& consent :
       in.accepted_task_consents) {
    const auto revision = revisions.find(std::string(consent.task_id));
    const auto route = ProviderRouteFromWire(consent.provider_route);
    // Named rather than compounded. Every clause is a fail-closed guard over
    // data restored from a person's own journal, and refusing any one of them
    // takes the whole core service down with it — so a bare refusal here costs
    // a rebuild to localise. Naming the clause and printing the numbers that
    // disagree is the difference between a diagnosis and another build.
    const char* refused =
        !route                                              ? "route-unknown"
        : *route == mojom::TaskProviderRoute::kNotConfigured ? "route-unset"
        : !IsValidIdentifier(consent.task_id)               ? "task-id"
        : consent.service_generation != in.service_generation ? "generation"
        : consent.current_task_revision == 0u               ? "current-zero"
        : consent.accepted_revision == 0u                   ? "accepted-zero"
        : consent.accepted_revision > consent.current_task_revision
                                                            ? "accepted-ahead"
        : revision == revisions.end()                       ? "no-such-task"
        : revision->second != consent.current_task_revision ? "revision-differs"
        : !IsValidIdentifier(consent.browser_session_id)    ? "session-id"
        : !IsValidIdentifier(consent.receipt_id)            ? "receipt-id"
        // No minimum on sources: an errand (decision 0087) is accepted with
        // none named and discovery on, and a `sources.empty()` clause here
        // took the core down on every start once one had been journaled
        // (verification report 2.25).
        : consent.sources.size() > mojom::kMaxTaskConsentSources ? "too-many-sources"
        : consent.new_source_cap > mojom::kMaxNewSourceCap  ? "source-cap"
        : !consent_tasks.emplace(std::string(consent.task_id)).second
                                                            ? "duplicate-task"
                                                            : nullptr;
    if (refused) {
      LOG(ERROR) << "[taffy_core_state_projection_refused] at=accepted-consent/"
                 << refused << " accepted=" << consent.accepted_revision
                 << " current=" << consent.current_task_revision
                 << " sources=" << consent.sources.size();
      return std::nullopt;
    }
    auto preview = mojom::TaskConsentPreview::New();
    std::set<std::string> source_ids;
    std::set<std::string> tab_ids;
    for (const bridge::BridgeConsentSource& source : consent.sources) {
      if (!IsValidIdentifier(source.source_id) ||
          !IsValidIdentifier(source.tab_id) ||
          source.normalized_origin.empty() ||
          source.normalized_origin.size() > mojom::kMaxNormalizedOriginBytes ||
          (source.has_canonical_locator &&
           (source.canonical_locator.empty() ||
            source.canonical_locator.size() > mojom::kMaxSourceLocatorBytes)) ||
          (!source.has_canonical_locator && !source.canonical_locator.empty()) ||
          !source_ids.emplace(std::string(source.source_id)).second ||
          !tab_ids.emplace(std::string(source.tab_id)).second) {
        TAFFY_REFUSE_STATE("accepted-consent/source");
      }
      std::optional<std::string> canonical_locator;
      if (source.has_canonical_locator) {
        canonical_locator = std::string(source.canonical_locator);
      }
      preview->sources.push_back(mojom::TaskConsentSource::New(
          std::string(source.source_id), std::string(source.tab_id),
          std::string(source.normalized_origin),
          std::move(canonical_locator)));
    }
    preview->source_discovery_enabled = consent.source_discovery_enabled;
    preview->new_source_cap = consent.new_source_cap;
    preview->provider_route = *route;
    auto binding = mojom::AcceptedTaskConsentBinding::New();
    binding->task_id = std::string(consent.task_id);
    binding->service_generation = consent.service_generation;
    binding->current_task_revision = consent.current_task_revision;
    binding->accepted_revision = consent.accepted_revision;
    binding->browser_session_id = std::string(consent.browser_session_id);
    binding->receipt_id = std::string(consent.receipt_id);
    binding->consent_preview = std::move(preview);
    publication.browser_bindings->accepted_task_consents.push_back(
        std::move(binding));
  }
  std::set<std::pair<std::string, std::string>> committed_approval_keys;
  for (const bridge::BridgeCommittedActionApproval& approval :
       in.committed_action_approvals) {
    const auto revision = revisions.find(std::string(approval.task_id));
    if (!IsValidIdentifier(approval.task_id) ||
        !IsValidIdentifier(approval.action_id) ||
        approval.service_generation != in.service_generation ||
        approval.committed_revision == 0u || revision == revisions.end() ||
        approval.committed_revision > revision->second ||
        !IsValidIdentifier(approval.receipt_id) ||
        !IsLowerHexSha256(approval.proposal_digest) ||
        approval.expires_at_monotonic_ms == 0u ||
        approval.expires_at_utc_ms == 0u ||
        !IsValidIdentifier(approval.browser_session_id) ||
        !committed_approval_keys
             .emplace(std::string(approval.task_id),
                      std::string(approval.action_id))
             .second) {
      TAFFY_REFUSE_STATE("committed-approval");
    }
    auto binding = mojom::CommittedActionApprovalBinding::New();
    binding->task_id = std::string(approval.task_id);
    binding->action_id = std::string(approval.action_id);
    binding->service_generation = approval.service_generation;
    binding->committed_revision = approval.committed_revision;
    binding->receipt_id = std::string(approval.receipt_id);
    binding->proposal_digest = std::string(approval.proposal_digest);
    binding->expires_at_monotonic_ms = approval.expires_at_monotonic_ms;
    binding->expires_at_utc_ms = approval.expires_at_utc_ms;
    binding->browser_session_id = std::string(approval.browser_session_id);
    publication.browser_bindings->committed_action_approvals.push_back(
        std::move(binding));
  }
  std::set<std::string> task_effect_ids;
  for (size_t index = 0; index < in.task_effects.size(); ++index) {
    const bridge::BridgeTaskEffect& effect = in.task_effects[index];
    const auto revision = revisions.find(std::string(effect.task_id));
    std::optional<mojom::TaskEffectBindingPtr> projected =
        ToMojoTaskEffect(effect);
    // Six independent facts, and the one that was wrong is the whole of what
    // a reader needs. Under one shared word this clause could only say that a
    // core died somewhere near a task effect, which is what it did say on a
    // phone for a day. The numbers beside it are structural — a kind, two
    // positions and two revisions — and name no value, address or goal.
    const char* refused =
        !projected || !*projected                    ? "binding"
        : revision == revisions.end()                ? "unknown-task"
        : effect.operation.service_generation != in.service_generation
            ? "generation"
        : effect.operation.task_revision != revision->second ? "revision"
        : effect.ordinal != index                    ? "ordinal"
        : !task_effect_ids.emplace(std::string(effect.effect_id)).second
            ? "duplicate-id"
            : nullptr;
    if (refused) {
      LOG(ERROR) << "[taffy_core_state_projection_refused] at=task-effect/"
                 << refused << " kind=" << static_cast<unsigned>(effect.kind)
                 << " ordinal=" << effect.ordinal << " index=" << index
                 << " effect_revision=" << effect.operation.task_revision
                 << " state_revision="
                 << (revision == revisions.end() ? 0u : revision->second)
                 << " effects=" << in.task_effects.size();
      return std::nullopt;
    }
    publication.task_effects.push_back(std::move(*projected));
  }
  return publication;
}

}  // namespace taffy::core_service_internal
