// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_ACCEPTED_APPROVAL_LEDGER_SHAPES_H_
#define TAFFY_BROWSER_ACCEPTED_APPROVAL_LEDGER_SHAPES_H_

#include <stdint.h>

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "url/gurl.h"
#include "url/origin.h"

// The shapes this ledger admits, shared by the three commands it stages.
//
// They are here rather than in one implementation file because the start
// command's staging owns enough clauses to be its own file (decision 0151) and
// asks the same questions the decision command does. Nothing here reads or
// writes ledger state: each is a question about one submitted value.

namespace taffy {
namespace accepted_approval_ledger_shapes {

namespace mojom = core_service::mojom;

inline bool IsIdentifier(const std::string& value) {
  return !value.empty() && value.size() <= mojom::kMaxIdentifierBytes &&
         std::none_of(value.begin(), value.end(), [](char character) {
           return static_cast<unsigned char>(character) < 0x20u;
         });
}

// Whether a submitted tool allowlist is a list somebody could have composed on
// purpose. It asks nothing about which names exist (decision 0057): the tool
// vocabulary lives in the sandboxed core, the milestone and action-class
// surfaces already refuse a name this build has not reached, and a copy of the
// table here would be a second authority that drifts from the first with no
// gate comparing them.
//
// Empty is the clause that matters. Reducer guard evaluation reads an empty
// allowlist as *everything the milestone has reached*, so a gate that accepted
// `{}` would admit a strictly wider task than any explicit list could ask
// for — and it would look like a working gate, because a gate that accepts
// everything accepts every well-formed command.
inline bool IsWellFormedToolAllowlist(const std::vector<std::string>& tools) {
  if (tools.empty() || tools.size() > mojom::kMaxToolAllowlistEntries) {
    return false;
  }
  std::set<std::string_view> seen;
  for (const std::string& tool : tools) {
    if (!IsIdentifier(tool) || !seen.insert(tool).second) {
      return false;
    }
  }
  return true;
}

// Whether the template names a member of the closed enumeration this build's
// contract defines — every member, not the one workflow M3 happens to run
// (decision 0057). Which task a person may start is decided by what the
// interface offers and by what the core agrees to run; pinning one member here
// made a gate about consent into the product's list of buildable tasks, and it
// refused every task this runtime is being built for.
//
// The switch carries no default arm on purpose: a template added to the
// contract stops this file compiling until somebody has decided it belongs.
inline bool IsKnownTaskTemplate(mojom::TaskTemplateId template_id) {
  switch (template_id) {
    case mojom::TaskTemplateId::kCompareProducts:
    case mojom::TaskTemplateId::kSummarizeEvidence:
    case mojom::TaskTemplateId::kBuildSourceTable:
    case mojom::TaskTemplateId::kWebErrand:
      return true;
  }
  // Reachable: an in-process caller can construct a value outside the
  // enumeration, and reading an unvalidated integer as a template would be
  // reading it as consent.
  return false;
}

// Whether the submitted provider route names something at all. This gate does
// not name *which* route, because the route the person accepted travels beside
// it in the consent preview, and the core's decoder is where the two are made
// to agree — `decode_start_task` refuses a command whose route identifier is
// not the one its accepted route projects to. A literal here re-checked
// nothing the core does not re-check, and pinned this gate to the single M3
// workflow while doing it.
inline bool IsWellFormedProviderRoute(const std::optional<std::string>& route_id) {
  return route_id.has_value() && IsIdentifier(*route_id);
}

inline bool IsSha256Digest(const std::string& value) {
  return value.size() == 64u &&
         std::all_of(value.begin(), value.end(), [](char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

inline bool IsNormalizedTupleOrigin(const std::string& value) {
  if (value.empty() || value.size() > mojom::kMaxNormalizedOriginBytes) {
    return false;
  }
  const GURL parsed(value);
  const url::Origin origin = url::Origin::Create(parsed);
  return parsed.is_valid() && parsed.SchemeIsHTTPOrHTTPS() &&
         !origin.opaque() && origin.Serialize() == value;
}

}  // namespace accepted_approval_ledger_shapes
}  // namespace taffy

#endif  // TAFFY_BROWSER_ACCEPTED_APPROVAL_LEDGER_SHAPES_H_
