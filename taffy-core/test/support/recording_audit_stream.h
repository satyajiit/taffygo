// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_SUPPORT_RECORDING_AUDIT_STREAM_H_
#define TAFFY_TEST_SUPPORT_RECORDING_AUDIT_STREAM_H_

#include <string>
#include <vector>

#include "taffy/components/intelligence/content/observability_recorder.h"

// The local audit and observability stream, as a test can inspect it.
//
// Observability records are content free by construction: the record structs
// are asserted trivially copyable so that an owning member such as a string
// cannot be added to them, which makes "no page content in analytics" a compile
// error rather than a review convention. That protects the records' shape. It
// does not, on its own, prove that the right records were emitted, in the right
// order, with the right decision facts — and those are what an exit review
// reads.
//
// So this sink collects them and offers the two questions a suite actually
// asks: what happened to this action, and did anything reach the stream that
// should not have. It stores the records by value, which is free: they are
// trivially copyable, which is the same property that keeps page content out.
//
// Named "audit stream" rather than "observability sink" because that is what
// the suites are asserting against — the record of what the runtime did. The
// interface it implements keeps its own name.

namespace taffy::test {

class RecordingAuditStream : public ObservabilitySink {
 public:
  RecordingAuditStream();
  RecordingAuditStream(const RecordingAuditStream&) = delete;
  RecordingAuditStream& operator=(const RecordingAuditStream&) = delete;
  ~RecordingAuditStream() override;

  // ObservabilitySink:
  void RecordObservation(const ObservationRecord& record) override;
  void RecordAction(const ActionRecord& record) override;
  void RecordSubscription(const SubscriptionRecord& record) override;

  const std::vector<ObservationRecord>& observations() const {
    return observations_;
  }
  const std::vector<ActionRecord>& actions() const { return actions_; }
  const std::vector<SubscriptionRecord>& subscriptions() const {
    return subscriptions_;
  }

  // The kinds of record in arrival order, as short strings. Used when the
  // ORDER is the property under test.
  const std::vector<std::string>& sequence() const { return sequence_; }

  // Every action record whose action identifier matches. Empty means the action
  // never reached the stream, and for an action that was submitted that is a
  // defect rather than a shape a refusal takes: step 10 records one terminal
  // result for every action whatever the outcome, and the stale-rate metrics
  // are made of the records refusals write. Exactly one is the healthy answer.
  std::vector<ActionRecord> ActionsFor(const std::string& action_id) const;

  // Every subscription record for one subscription identifier, in order.
  std::vector<SubscriptionRecord> SubscriptionEventsFor(
      const std::string& subscription_id) const;

  void Clear();

 private:
  std::vector<ObservationRecord> observations_;
  std::vector<ActionRecord> actions_;
  std::vector<SubscriptionRecord> subscriptions_;
  std::vector<std::string> sequence_;
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_SUPPORT_RECORDING_AUDIT_STREAM_H_
