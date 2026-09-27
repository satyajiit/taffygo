// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/support/recording_audit_stream.h"

#include "base/strings/strcat.h"

namespace taffy::test {

namespace {

std::string Text(const RecordIdentifier& identifier) {
  // The buffer is fixed capacity and null terminated by construction, so the
  // C-string read is bounded by the array itself.
  return std::string(identifier.chars.data());
}

}  // namespace

RecordingAuditStream::RecordingAuditStream() = default;
RecordingAuditStream::~RecordingAuditStream() = default;

void RecordingAuditStream::RecordObservation(const ObservationRecord& record) {
  sequence_.push_back(base::StrCat({"observation:", Text(record.request_id)}));
  observations_.push_back(record);
}

void RecordingAuditStream::RecordAction(const ActionRecord& record) {
  sequence_.push_back(base::StrCat({"action:", Text(record.action_id)}));
  actions_.push_back(record);
}

void RecordingAuditStream::RecordSubscription(
    const SubscriptionRecord& record) {
  sequence_.push_back(
      base::StrCat({"subscription:", Text(record.subscription_id)}));
  subscriptions_.push_back(record);
}

std::vector<ActionRecord> RecordingAuditStream::ActionsFor(
    const std::string& action_id) const {
  std::vector<ActionRecord> out;
  for (const ActionRecord& record : actions_) {
    if (Text(record.action_id) == action_id) {
      out.push_back(record);
    }
  }
  return out;
}

std::vector<SubscriptionRecord> RecordingAuditStream::SubscriptionEventsFor(
    const std::string& subscription_id) const {
  std::vector<SubscriptionRecord> out;
  for (const SubscriptionRecord& record : subscriptions_) {
    if (Text(record.subscription_id) == subscription_id) {
      out.push_back(record);
    }
  }
  return out;
}

void RecordingAuditStream::Clear() {
  observations_.clear();
  actions_.clear();
  subscriptions_.clear();
  sequence_.clear();
}

}  // namespace taffy::test
