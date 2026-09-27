// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/support/audit_stream_assertions.h"

#include "base/strings/string_number_conversions.h"
#include "taffy/test/support/recording_audit_stream.h"

namespace taffy::test {

// static
::testing::AssertionResult AuditStreamAssertions::ActionRecorded(
    const RecordingAuditStream& stream,
    const std::string& action_id,
    ActionResultCode code) {
  const auto records = stream.ActionsFor(action_id);
  if (records.size() != 1) {
    return ::testing::AssertionFailure()
           << "Expected exactly one audit record for action " << action_id
           << "; found " << records.size()
           << ". One action produces one terminal result, so it produces one "
              "record.";
  }
  if (records.front().code != code) {
    return ::testing::AssertionFailure()
           << "Action " << action_id << " was audited with result code "
           << static_cast<int>(records.front().code) << ", expected "
           << static_cast<int>(code) << ".";
  }
  return ::testing::AssertionSuccess();
}

// static
::testing::AssertionResult
AuditStreamAssertions::NoVerifiedWithoutCorroboration(
    const RecordingAuditStream& stream) {
  for (const ActionRecord& record : stream.actions()) {
    if (record.code != ActionResultCode::kVerified) {
      continue;
    }
    if (!record.had_verified_postcondition) {
      return ::testing::AssertionFailure()
             << "An action was audited as verified with no verified "
                "postcondition. Verified is reachable only through the "
                "browser-side postcondition verifier.";
    }
    if (record.verifier_outcome != VerifierOutcome::kCorroborated) {
      return ::testing::AssertionFailure()
             << "An action was audited as verified while its verifier outcome "
                "was "
             << static_cast<int>(record.verifier_outcome)
             << " rather than corroborated.";
    }
    if (record.renderer_acknowledged && !record.had_verified_postcondition) {
      return ::testing::AssertionFailure()
             << "An action reached verified on renderer acknowledgement alone. "
                "Acknowledgement is a statement about the renderer, not about "
                "the page, and a compromised renderer would make it anyway.";
    }
  }
  return ::testing::AssertionSuccess();
}

// static
::testing::AssertionResult AuditStreamAssertions::StaleReasonRecorded(
    const RecordingAuditStream& stream,
    const std::string& action_id,
    StaleReason reason) {
  const auto records = stream.ActionsFor(action_id);
  if (records.size() != 1) {
    return ::testing::AssertionFailure()
           << "Expected exactly one audit record for action " << action_id
           << "; found " << records.size() << ".";
  }
  if (records.front().stale_reason != reason) {
    return ::testing::AssertionFailure()
           << "Action " << action_id << " recorded stale reason "
           << static_cast<int>(records.front().stale_reason) << ", expected "
           << static_cast<int>(reason)
           << ". The core service's next legal move differs between the reasons, so "
              "an unattributed refusal is one nobody can act on.";
  }
  return ::testing::AssertionSuccess();
}

// static
::testing::AssertionResult AuditStreamAssertions::SubscriptionEventOrder(
    const RecordingAuditStream& stream,
    const std::string& subscription_id,
    const std::vector<SubscriptionEventKind>& expected_order) {
  const auto records = stream.SubscriptionEventsFor(subscription_id);
  size_t expected_index = 0;
  for (const SubscriptionRecord& record : records) {
    if (expected_index < expected_order.size() &&
        record.event == expected_order[expected_index]) {
      ++expected_index;
    }
  }
  if (expected_index == expected_order.size()) {
    return ::testing::AssertionSuccess();
  }
  ::testing::AssertionResult failure = ::testing::AssertionFailure();
  failure << "Subscription " << subscription_id << " did not produce the "
          << "expected event order; matched " << expected_index << " of "
          << expected_order.size() << ". Recorded:";
  for (const SubscriptionRecord& record : records) {
    failure << "\n  " << base::NumberToString(static_cast<int>(record.event));
  }
  return failure;
}

// static
::testing::AssertionResult
AuditStreamAssertions::ObservationsCarryOnlyOriginCategories(
    const RecordingAuditStream& stream) {
  for (const ObservationRecord& record : stream.observations()) {
    if (record.origin_category == OriginCategory::kUnknown &&
        record.origin_diagnostic_hash != 0) {
      return ::testing::AssertionFailure()
             << "An observation record carries a diagnostic origin hash with "
                "no origin category. The hash is the optional part and the "
                "category is the required one; having only the hash means the "
                "record can be correlated but not understood.";
    }
  }
  return ::testing::AssertionSuccess();
}

}  // namespace taffy::test
