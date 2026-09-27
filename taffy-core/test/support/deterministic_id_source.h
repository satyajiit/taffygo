// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_SUPPORT_DETERMINISTIC_ID_SOURCE_H_
#define TAFFY_TEST_SUPPORT_DETERMINISTIC_ID_SOURCE_H_

#include <stdint.h>

#include <string>

#include "taffy/common/public/bip_identity.h"

// Every opaque identifier a test needs to mint, generated so that the same test
// produces the same identifiers on every run.
//
// The browser process mints its own identifiers — tab, frame, page epoch,
// request, dispatch, subscription — from monotonic counters, so those are
// already reproducible. What a test has to supply is the other half: the task,
// the action, the actor lease and the capability reference that a policy engine
// would have issued. Generating those from a real random source would make a
// failure message different on every run and a recorded audit stream impossible
// to compare against a previous one, which is the thing an evidence artifact
// exists to allow.
//
// Each instance carries a prefix so that two sources in one test cannot collide
// and so a failure message says which one an identifier came from. The prefix
// is part of the identifier rather than a side table, because an identifier
// that needs a side table to be understood is one nobody reads.

namespace taffy::test {

class DeterministicIdSource {
 public:
  // `prefix` appears at the start of every identifier this source issues, for
  // example "t1" giving "t1.task.1".
  explicit DeterministicIdSource(std::string prefix);
  DeterministicIdSource(const DeterministicIdSource&) = delete;
  DeterministicIdSource& operator=(const DeterministicIdSource&) = delete;
  ~DeterministicIdSource();

  TaskId NextTaskId();
  ActionId NextActionId();
  DispatchId NextDispatchId();
  ActorLeaseId NextActorLeaseId();
  CapabilityReference NextCapabilityReference();
  ApprovalReceiptReference NextApprovalReceiptReference();
  SensitivityPolicyId NextSensitivityPolicyId();
  RequestId NextRequestId();
  SemanticNodeId NextSemanticNodeId();
  SubscriptionId NextSubscriptionId();
  PageEpoch NextPageEpoch();

  // A value guaranteed not to have been issued by this source and not to
  // collide with the browser's own counters, for the tests that need a handle
  // the broker has never heard of.
  std::string NeverIssued(const std::string& kind) const;

 private:
  std::string Mint(const char* kind);

  const std::string prefix_;
  uint64_t counter_ = 0;
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_SUPPORT_DETERMINISTIC_ID_SOURCE_H_
