// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_SUPPORT_AUDIT_STREAM_ASSERTIONS_H_
#define TAFFY_TEST_SUPPORT_AUDIT_STREAM_ASSERTIONS_H_

#include <string>
#include <vector>

#include "taffy/components/intelligence/content/observability_recorder.h"
#include "taffy/common/public/bip_result.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::test {

class RecordingAuditStream;

// The audit-stream properties a suite asserts, written once.
//
// The record structs already cannot carry page content: they are asserted
// trivially copyable, so an owning member is a compile error. That is a
// guarantee about shape. These helpers are about substance — whether the right
// record was emitted, whether it recorded the decision that was actually made,
// and whether anything reached the stream claiming a success the verifier never
// granted.
//
// The last of those is the one worth naming. Verified is reachable only through
// the browser-side postcondition verifier; renderer acknowledgement alone means
// dispatched and nothing more. NoVerifiedWithoutCorroboration() is that
// sentence as an assertion, and it is cheap enough that every action test
// should run it.

class AuditStreamAssertions {
 public:
  AuditStreamAssertions() = delete;

  // Exactly one action record exists for `action_id`, and it carries `code`.
  //
  // This is the assertion a refused action wants too. Every submitted action
  // reaches step 10 and step 10 records one terminal result whatever the
  // outcome, so there is no such thing as an action the stream never heard of —
  // and there deliberately is not: the stale-rate metrics protocol section 16
  // names are made of the records refusals write, and half the stale reasons
  // are earned before a capability is ever admitted. An assertion that a
  // refusal left no record would be asserting a hole in the audit trail.
  [[nodiscard]] static ::testing::AssertionResult ActionRecorded(
      const RecordingAuditStream& stream,
      const std::string& action_id,
      ActionResultCode code);

  // No record in the stream reports a verified action without a corroborated
  // postcondition observed by something other than the renderer's own
  // acknowledgement.
  [[nodiscard]] static ::testing::AssertionResult
  NoVerifiedWithoutCorroboration(const RecordingAuditStream& stream);

  // The action record attributes the refusal to `reason`. A stale-handle
  // refusal that recorded no reason is a refusal nobody can act on: the
  // core service's next legal move differs between the reasons.
  [[nodiscard]] static ::testing::AssertionResult StaleReasonRecorded(
      const RecordingAuditStream& stream,
      const std::string& action_id,
      StaleReason reason);

  // The subscription's events appear in the given order, as a subsequence of
  // what was recorded. A subsequence rather than an exact match, because the
  // stream also carries the deliveries between them and a test that had to list
  // every one would be asserting on volume rather than on order.
  [[nodiscard]] static ::testing::AssertionResult SubscriptionEventOrder(
      const RecordingAuditStream& stream,
      const std::string& subscription_id,
      const std::vector<SubscriptionEventKind>& expected_order);

  // Every observation record reports the origin as a category and never as an
  // origin. Cheap to run everywhere, and it is the assertion that catches a
  // well-meaning change adding "just the hostname, for debugging".
  [[nodiscard]] static ::testing::AssertionResult
  ObservationsCarryOnlyOriginCategories(const RecordingAuditStream& stream);
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_SUPPORT_AUDIT_STREAM_ASSERTIONS_H_
