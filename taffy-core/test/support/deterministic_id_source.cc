// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/support/deterministic_id_source.h"

#include <utility>

#include "base/check.h"
#include "base/check_op.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"

namespace taffy::test {

DeterministicIdSource::DeterministicIdSource(std::string prefix)
    : prefix_(std::move(prefix)) {
  CHECK(!prefix_.empty())
      << "A DeterministicIdSource with no prefix issues identifiers that "
         "cannot be told apart from another source's.";
}

DeterministicIdSource::~DeterministicIdSource() = default;

std::string DeterministicIdSource::Mint(const char* kind) {
  const std::string value = base::StrCat(
      {prefix_, ".", kind, ".", base::NumberToString(++counter_)});
  CHECK_LE(value.size(), kMaxIdentifierChars)
      << "Minted identifier " << value
      << " is longer than the contract's identifier bound. Shorten the source "
         "prefix: an over-long identifier is rejected at the boundary, and the "
         "refusal would look like the property under test failing.";
  return value;
}

TaskId DeterministicIdSource::NextTaskId() {
  return TaskId{Mint("task")};
}

ActionId DeterministicIdSource::NextActionId() {
  return ActionId{Mint("action")};
}

DispatchId DeterministicIdSource::NextDispatchId() {
  return DispatchId{Mint("dispatch")};
}

ActorLeaseId DeterministicIdSource::NextActorLeaseId() {
  return ActorLeaseId{Mint("lease")};
}

CapabilityReference DeterministicIdSource::NextCapabilityReference() {
  return CapabilityReference{Mint("capability")};
}

ApprovalReceiptReference
DeterministicIdSource::NextApprovalReceiptReference() {
  return ApprovalReceiptReference{Mint("approval")};
}

SensitivityPolicyId DeterministicIdSource::NextSensitivityPolicyId() {
  return SensitivityPolicyId{Mint("policy")};
}

RequestId DeterministicIdSource::NextRequestId() {
  return RequestId{Mint("request")};
}

SemanticNodeId DeterministicIdSource::NextSemanticNodeId() {
  return SemanticNodeId{Mint("node")};
}

SubscriptionId DeterministicIdSource::NextSubscriptionId() {
  return SubscriptionId{Mint("subscription")};
}

PageEpoch DeterministicIdSource::NextPageEpoch() {
  return PageEpoch{Mint("epoch")};
}

std::string DeterministicIdSource::NeverIssued(const std::string& kind) const {
  // "never" rather than a counter value: a counter value could one day be
  // reached, and a test that asserts a handle is unknown must not be one
  // increment away from asserting the opposite.
  return base::StrCat({prefix_, ".", kind, ".never-issued"});
}

}  // namespace taffy::test
