// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/adapter_requirement_check.h"

#include <algorithm>

#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"

namespace taffy {

namespace {

// How severe a result code is, so that combining two of them is a maximum
// rather than a chain of if-statements that has to be read in order. The
// ranking is this file's, not the contract's member order.
uint8_t Severity(ObservationResultCode code) {
  switch (code) {
    case ObservationResultCode::kOk:
      return 0;
    case ObservationResultCode::kConflicted:
      return 1;
    case ObservationResultCode::kIncomplete:
      return 2;
    case ObservationResultCode::kBudgetExceeded:
      return 3;
    case ObservationResultCode::kDeadlineExceeded:
      return 4;
    case ObservationResultCode::kResourcePressure:
      return 5;
    case ObservationResultCode::kStalePageEpoch:
      return 6;
    case ObservationResultCode::kDocumentInactive:
      return 7;
    case ObservationResultCode::kCancelled:
      return 8;
    case ObservationResultCode::kUnsupported:
      return 9;
    case ObservationResultCode::kInternalError:
      return 10;
  }
  // An unrecognized code is the most severe thing there is. Fail closed.
  return 255;
}

const AdapterReport* FindReport(const std::vector<AdapterReport>& reports,
                                AdapterKind adapter) {
  for (const AdapterReport& report : reports) {
    if (report.adapter == adapter) {
      return &report;
    }
  }
  return nullptr;
}

bool SupportsAdapter(const ProtocolSupportEnvelope& support,
                     AdapterKind adapter) {
  for (const AdapterSupport& entry : support.adapters) {
    if (entry.adapter == adapter) {
      return entry.supported;
    }
  }
  return false;
}

void Tally(AdapterRequirementOutcome* outcome, const AdapterFinding& finding) {
  outcome->findings.push_back(finding);
  switch (finding.disposition) {
    case AdapterDisposition::kSatisfied:
      return;
    case AdapterDisposition::kConflicted:
      ++outcome->conflicted_count;
      outcome->warning_codes.push_back(
          static_cast<uint8_t>(mojom::WarningCode::kConflictingEvidence));
      return;
    case AdapterDisposition::kIncomplete:
      // It ran and produced partial evidence, so it is not missing and the
      // requirement level does not enter into it. A required adapter that
      // stopped at a shadow boundary or exhausted a budget has still put a
      // graph in front of the caller; answering UNSUPPORTED would deny the
      // evidence it did produce and would hide the budget or boundary that
      // actually bounded it behind a capability verdict. Named all the same,
      // so the result cannot be read as complete.
      ++outcome->incomplete_count;
      outcome->warning_codes.push_back(
          static_cast<uint8_t>(mojom::WarningCode::kAdapterUnavailable));
      return;
    case AdapterDisposition::kFailed:
      ++outcome->failed_count;
      outcome->warning_codes.push_back(
          static_cast<uint8_t>(mojom::WarningCode::kAdapterFailed));
      break;
    case AdapterDisposition::kUnsupportedByEndpoint:
    case AdapterDisposition::kUnsupportedForDocument:
    case AdapterDisposition::kNotGranted:
      outcome->warning_codes.push_back(
          static_cast<uint8_t>(mojom::WarningCode::kAdapterUnavailable));
      break;
  }
  if (finding.requirement == AdapterRequirementLevel::kRequired) {
    ++outcome->required_missing_count;
  } else {
    ++outcome->optional_missing_count;
  }
}

void Settle(AdapterRequirementOutcome* outcome) {
  if (outcome->required_missing_count > 0) {
    // Not a degraded result that looks complete. The whole request is
    // unsupported, and the findings say which adapter made it so. Only an
    // adapter that was never there to run reaches this counter — a required
    // adapter that ran partially is counted below, because a partial answer
    // and an absent capability are different facts.
    outcome->code = ObservationResultCode::kUnsupported;
    return;
  }
  if (outcome->incomplete_count > 0) {
    // An adapter that ran partially can have contributed a graph whose
    // omissions change the answer, regardless of whether it was required or
    // optional. Missing or failed OPTIONAL adapters are different: their
    // evidence was welcome but not necessary for a usable result, and their
    // named findings/reports preserve that absence without mislabelling the
    // required evidence as incomplete.
    outcome->code = ObservationResultCode::kIncomplete;
    return;
  }
  if (outcome->conflicted_count > 0) {
    // Disagreement between sources is preserved, not resolved here. It is
    // reported so that a consumer knows the graph carries competing
    // candidates, which is exactly what protocol section 7.5 asks for.
    outcome->code = ObservationResultCode::kConflicted;
    return;
  }
  outcome->code = ObservationResultCode::kOk;
}

}  // namespace

AdapterRequirementOutcome CheckAdaptersAgainstSupport(
    const std::vector<AdapterRequirement>& requested,
    const ProtocolSupportEnvelope& support,
    const std::vector<AdapterKind>& granted_adapters) {
  AdapterRequirementOutcome outcome;
  for (const AdapterRequirement& requirement : requested) {
    AdapterFinding finding;
    finding.adapter = requirement.adapter;
    finding.requirement = requirement.requirement;

    if (!std::ranges::contains(granted_adapters, requirement.adapter)) {
      // The grant, not the endpoint, is why this one is absent. Named
      // separately so a reviewer can tell a policy decision from a capability
      // gap without reading the grant.
      finding.disposition = AdapterDisposition::kNotGranted;
    } else if (!SupportsAdapter(support, requirement.adapter)) {
      finding.disposition = AdapterDisposition::kUnsupportedByEndpoint;
    } else {
      finding.disposition = AdapterDisposition::kSatisfied;
    }
    Tally(&outcome, finding);
  }
  Settle(&outcome);
  return outcome;
}

AdapterRequirementOutcome CheckAdapterReports(
    const std::vector<AdapterRequirement>& requested,
    const std::vector<AdapterReport>& reports,
    bool observation_was_truncated) {
  AdapterRequirementOutcome outcome;
  for (const AdapterRequirement& requirement : requested) {
    AdapterFinding finding;
    finding.adapter = requirement.adapter;
    finding.requirement = requirement.requirement;

    const AdapterReport* report = FindReport(reports, requirement.adapter);
    if (!report) {
      // Asked for, and not mentioned in the reply at all. Silence is not
      // success: an adapter that ran would have said so. But a truncated
      // reply has already said why it stopped, and the adapters it never
      // reached are exactly the ones a budget cut off - partial evidence,
      // which this file's header rules is INCOMPLETE and never UNSUPPORTED.
      finding.disposition = observation_was_truncated
                                ? AdapterDisposition::kIncomplete
                                : AdapterDisposition::kUnsupportedForDocument;
      Tally(&outcome, finding);
      continue;
    }
    switch (report->status) {
      case AdapterStatus::kOk:
        finding.disposition = AdapterDisposition::kSatisfied;
        break;
      case AdapterStatus::kIncomplete:
        finding.disposition = AdapterDisposition::kIncomplete;
        break;
      case AdapterStatus::kConflicted:
        finding.disposition = AdapterDisposition::kConflicted;
        break;
      case AdapterStatus::kUnsupported:
        finding.disposition = AdapterDisposition::kUnsupportedForDocument;
        break;
      case AdapterStatus::kFailed:
        finding.disposition = AdapterDisposition::kFailed;
        break;
    }
    Tally(&outcome, finding);
  }
  Settle(&outcome);
  return outcome;
}

ObservationResultCode CombineObservationCodes(ObservationResultCode base,
                                              ObservationResultCode adapter) {
  return Severity(adapter) > Severity(base) ? adapter : base;
}

}  // namespace taffy
