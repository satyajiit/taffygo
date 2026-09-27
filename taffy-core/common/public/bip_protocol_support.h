// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_PUBLIC_BIP_PROTOCOL_SUPPORT_H_
#define TAFFY_PUBLIC_BIP_PROTOCOL_SUPPORT_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "taffy/common/public/bip_action.h"
#include "taffy/common/public/bip_budget.h"
#include "taffy/common/public/bip_identity.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/common/public/bip_result.h"

// What one endpoint says it supports (protocol section 6.2,
// taffy-core/contracts/bip/schema/protocol-info.schema.json).
//
// Negotiation exists so a task can plan against real support rather than
// discovering UNSUPPORTED after it has already told a user what it was going
// to do. Two rules follow from protocol section 6.2 and both are visible in
// this header:
//
//   * Advertising is not authorization. A listed action type still needs a
//     capability, an actor lease and, where policy says so, an approval.
//     Nothing in this struct grants anything.
//   * An unknown value is unsupported, never the least restrictive known
//     value. The broker drops values it cannot map instead of guessing, and
//     the counts below say how many it dropped so a version skew is visible
//     rather than silent.
//
// This is a terminal result in its own right rather than a snapshot with the
// graph left empty. Reusing the observation envelope for negotiation would
// have meant a caller could not tell "this endpoint supports nothing" from
// "this page has nothing on it".

namespace taffy {

// One adapter and whether this endpoint can actually run it. Present for every
// adapter the caller asked about, so an absent answer is never mistaken for a
// negative one.
struct AdapterSupport {
  AdapterKind adapter = AdapterKind::kDom;
  bool supported = false;
  uint32_t adapter_version = 0;
  uint32_t extraction_rule_version = 0;

  friend bool operator==(const AdapterSupport&, const AdapterSupport&) = default;
};

// The single terminal result of one QueryProtocolSupport call.
struct ProtocolSupportEnvelope {
  std::string schema_version;
  RequestId request_id;
  TabId tab_id;
  FrameId frame_id;
  ObservationResultCode code = ObservationResultCode::kInternalError;

  // What the endpoint reported. Empty when the endpoint could not answer,
  // which is kUnsupported rather than a set of assumed defaults.
  std::string endpoint_protocol_version;
  // Opaque build identifier of the endpoint implementation. Not a version
  // number and not a device identifier.
  std::string implementation_id;

  std::vector<AdapterSupport> adapters;
  std::vector<ObservationScope> supported_scopes;
  // Implementation support only. Inclusion never authorizes an action; live
  // node, policy, capability, and journal gates still decide whether it runs.
  std::vector<ActionType> supported_action_types;

  // What the endpoint says it enforces.
  ProcessBudgetLimits endpoint_limits;
  // The endpoint's limits narrowed by the process ceiling. This, not
  // endpoint_limits, is what a request is actually measured against, and a
  // caller planning a budget should read this field.
  ProcessBudgetLimits effective_limits;

  // How many wire values the broker could not map to a known member. Non-zero
  // means the endpoint is running a newer minor version than this build knows
  // about; the unmapped values were dropped, never coerced.
  uint32_t unmapped_adapter_count = 0;
  uint32_t unmapped_scope_count = 0;
  uint32_t unmapped_action_type_count = 0;

  MonotonicMillis observed_at_monotonic_ms = 0;
};

// True when `envelope` reports support for every adapter the request marked
// required. A missing required adapter is UNSUPPORTED (protocol section 6.2);
// a missing optional adapter is named without degrading otherwise complete
// required evidence. This predicate decides only required support, and
// adapter_requirement_check.cc in //taffy/components/intelligence/content
// owns the aggregate decision that uses it.
inline bool SupportsAllRequiredAdapters(
    const ProtocolSupportEnvelope& envelope,
    const std::vector<AdapterRequirement>& requirements) {
  for (const AdapterRequirement& requirement : requirements) {
    if (requirement.requirement != AdapterRequirementLevel::kRequired) {
      continue;
    }
    bool found = false;
    for (const AdapterSupport& support : envelope.adapters) {
      if (support.adapter == requirement.adapter && support.supported) {
        found = true;
        break;
      }
    }
    if (!found) {
      return false;
    }
  }
  return true;
}

}  // namespace taffy

#endif  // TAFFY_PUBLIC_BIP_PROTOCOL_SUPPORT_H_
