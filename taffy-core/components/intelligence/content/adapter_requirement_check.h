// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_ADAPTER_REQUIREMENT_CHECK_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_ADAPTER_REQUIREMENT_CHECK_H_

#include <stdint.h>

#include <vector>

#include "taffy/common/public/bip_budget.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/common/public/bip_protocol_support.h"
#include "taffy/common/public/bip_result.h"

// Whether a result may be labelled complete (protocol sections 6.2 and 15).
//
// Two sentences, and the distance between them is the entire file:
//
//   "Snapshot/action requests name required capabilities. Missing required
//    support returns UNSUPPORTED, not a degraded result that looks complete."
//
//   "Adapter failure is isolated and named; one missing optional adapter need
//    not discard all other evidence."
//
// So a missing *required* adapter is fatal to the request and a missing
// *optional* one is not — but it is never silent either. A finding and the
// per-adapter report name which optional adapter was absent or failed. The
// aggregate code remains kOk when all required evidence is complete, because
// OPTIONAL means that evidence was welcome rather than necessary for a usable
// result.
//
// "Missing" is read strictly, and the strictness is the point. An adapter is
// missing when it is not there to run: the endpoint does not implement it, this
// document cannot serve it, the grant withheld it, or the reply never mentioned
// it. An adapter that ran, walked the document and returned partial evidence is
// not missing — it is present and honest about its edges, and the evidence it
// produced is in the graph. Collapsing the two would answer UNSUPPORTED to a
// caller holding a real graph, and would make the ordinary cases that produce
// partial evidence — a shadow boundary the DOM walk stops at
// (protocol section 8.2), a budget the walk exhausted — indistinguishable from
// a capability the browser does not have. Those are different facts and they
// take different codes: a partial *required* adapter degrades the result to
// INCOMPLETE, never to UNSUPPORTED.
//
// The failure of one adapter is isolated by construction here: the check reads
// each adapter's own report and forms no opinion that spans them. An adapter
// that failed removes nothing from what the others produced.
//
// Pure functions over value types, so every rule is provable without a
// renderer.

namespace taffy {

// What one adapter contributed, or did not.
enum class AdapterDisposition : uint8_t {
  kSatisfied = 0,
  // The endpoint does not implement it at all. Learned at negotiation.
  kUnsupportedByEndpoint = 1,
  // The endpoint implements it but produced nothing for this document.
  kUnsupportedForDocument = 2,
  // It ran and produced partial evidence.
  kIncomplete = 3,
  // It ran and produced evidence that disagrees with another source. Not a
  // failure: preserved disagreement is the point of provenance
  // (protocol section 7.5).
  kConflicted = 4,
  // It failed. Isolated and named.
  kFailed = 5,
  // The task's grant did not include it, so it was never requested.
  kNotGranted = 6,
};

struct AdapterFinding {
  AdapterKind adapter = AdapterKind::kDom;
  AdapterRequirementLevel requirement = AdapterRequirementLevel::kOptional;
  AdapterDisposition disposition = AdapterDisposition::kSatisfied;
};

struct AdapterRequirementOutcome {
  // kOk, kUnsupported, kIncomplete or kConflicted. Never kOk when required
  // support is missing or any adapter returned partial evidence. A missing
  // optional adapter may retain kOk only alongside its named finding/report.
  ObservationResultCode code = ObservationResultCode::kOk;
  std::vector<AdapterFinding> findings;
  // Adapters that were not there to run. Counted regardless of requirement
  // level, and only the required ones make the request UNSUPPORTED.
  uint32_t required_missing_count = 0;
  uint32_t optional_missing_count = 0;
  uint32_t failed_count = 0;
  uint32_t conflicted_count = 0;
  // Adapters that ran and returned partial evidence. Kept apart from the
  // missing counts because a partial adapter is present: it contributed to the
  // graph, and the result it degrades is INCOMPLETE rather than UNSUPPORTED
  // whatever its requirement level.
  uint32_t incomplete_count = 0;
  // Warning codes to attach to the envelope, as the contract's numeric member
  // values. Stable codes, never renderer-authored prose.
  std::vector<uint8_t> warning_codes;
};

// Decides before the request is sent, from negotiation alone. A required
// adapter the endpoint does not implement makes the whole request UNSUPPORTED,
// and finding that out here saves an extraction that could not have produced a
// usable answer.
AdapterRequirementOutcome CheckAdaptersAgainstSupport(
    const std::vector<AdapterRequirement>& requested,
    const ProtocolSupportEnvelope& support,
    const std::vector<AdapterKind>& granted_adapters);

// Decides after the reply, from what each adapter actually reported. The
// endpoint may implement an adapter and still fail to run it on this document,
// which negotiation cannot predict.
//
// `observation_was_truncated` is the reply's own truncation flag, and it
// decides what an unmentioned adapter means. Collection runs the adapters in
// order against one shared budget ledger, so a budget exhausted part-way
// leaves the adapters after it unrun and therefore unmentioned - silence that
// the reply has already explained. Reading that as an absent capability is the
// collapse this file's header forbids: a phone's search results page produced
// a 195-node graph, a DOM adapter that never got to run, and UNSUPPORTED,
// which ended the task with "Taffy could not read enough to answer" over
// evidence it was holding. An untruncated reply is unchanged - there, silence
// really is the endpoint declining to serve this document.
AdapterRequirementOutcome CheckAdapterReports(
    const std::vector<AdapterRequirement>& requested,
    const std::vector<AdapterReport>& reports,
    bool observation_was_truncated);

// Combines an adapter outcome with the code a snapshot otherwise deserved.
// Strictness wins: a snapshot that was going to be kOk becomes kIncomplete
// when an adapter returned partial evidence, and a snapshot that was already
// kBudgetExceeded stays that way rather than being softened. A named missing
// optional adapter produces kOk above and therefore does not degrade the base.
ObservationResultCode CombineObservationCodes(ObservationResultCode base,
                                              ObservationResultCode adapter);

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_ADAPTER_REQUIREMENT_CHECK_H_
