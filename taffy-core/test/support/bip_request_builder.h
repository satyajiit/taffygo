// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_SUPPORT_BIP_REQUEST_BUILDER_H_
#define TAFFY_TEST_SUPPORT_BIP_REQUEST_BUILDER_H_

#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "taffy/common/public/bip_action.h"
#include "taffy/common/public/bip_budget.h"
#include "taffy/common/public/bip_identity.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/common/public/bip_subscription.h"
#include "taffy/test/support/deterministic_id_source.h"

// Builds the requests and authorized envelopes a policy engine would have
// produced, correctly, so that a test's failure is about the property it meant
// to test.
//
// This is not convenience. An AuthorizedActionEnvelope has eleven fields that
// have to agree with each other before the dispatcher will look at the action
// at all: the digest has to match the envelope it is attached to, the
// capability has to name a lease that exists on the right tab, the expiry and
// the absolute deadline have to be on the monotonic scale the browser reads,
// and at least one postcondition has to be declared because an unverifiable
// action is not an authorized action. Every one of those is a real refusal with
// its own result code. A test that got one wrong would see a refusal, and the
// refusal would look exactly like the property under test failing.
//
// So the builder produces envelopes that are correct by construction, and every
// hostile variation a suite needs is an explicit, named mutation of one — see
// the With* methods, each of which says which refusal it is provoking.

namespace taffy::test {

class BipRequestBuilder {
 public:
  BipRequestBuilder(DeterministicIdSource* ids,
                    TaskId task_id,
                    TabId tab_id,
                    ActorLeaseId lease_id);
  BipRequestBuilder(const BipRequestBuilder&) = delete;
  BipRequestBuilder& operator=(const BipRequestBuilder&) = delete;
  ~BipRequestBuilder();

  const TaskId& task_id() const { return task_id_; }
  const TabId& tab_id() const { return tab_id_; }
  const ActorLeaseId& lease_id() const { return lease_id_; }

  // A grant wide enough for the read-oriented workflow on one origin, and no
  // wider. Suites that need child frames or a second origin say so explicitly,
  // because a default that quietly admitted cross-origin frames would make the
  // frame-policy assertions meaningless.
  ObservationPolicyGrant Grant(ObservationScope max_scope,
                               std::vector<Origin> allowed_origins,
                               bool may_include_child_frames) const;

  ObservationRequest Observation(const FrameId& root_frame_id,
                                 ObservationScope scope) const;

  // The exact form-section projection the shipping browser uses. It carries
  // the layout evidence required by the renderer's independent write
  // preconditions without making every document read pay for geometry and
  // occlusion probes.
  ObservationRequest FormObservation(const FrameId& root_frame_id,
                                     FormObservationRoot form_root) const;

  SubscriptionRequest Subscription(const FrameId& frame_id,
                                   const PageEpoch& expected_page_epoch) const;

  // An activate on one node, with the postcondition the protocol requires and
  // the preconditions the stale-node algorithm checks. Correct by
  // construction: the digest is computed last, over the finished envelope.
  AuthorizedActionEnvelope Activate(
      const NodeHandle& handle,
      GraphRevision required_graph_revision,
      std::vector<Postcondition> expected_postconditions) const;

  // A browser-owned navigation command with a committed-navigation
  // postcondition.
  AuthorizedBrowserCommand Navigate(
      const std::string& normalized_destination,
      std::vector<Postcondition> expected_postconditions) const;

  // --- deliberate corruptions, each naming the refusal it provokes ----------

  // Leaves the digest stale after a field changed. Provokes the digest-mismatch
  // admission failure, which is what an envelope edited after authorization
  // looks like.
  static AuthorizedActionEnvelope WithTamperedTarget(
      AuthorizedActionEnvelope envelope,
      const SemanticNodeId& replacement_node);

  // Points the capability at a lease that was never issued. Provokes
  // kActorLeaseMissing before anything else happens.
  static AuthorizedActionEnvelope WithUnknownLease(
      AuthorizedActionEnvelope envelope,
      const ActorLeaseId& unknown_lease);

  // Sets an expiry already in the past. Provokes kCapabilityExpired.
  static AuthorizedActionEnvelope WithExpiredCapability(
      AuthorizedActionEnvelope envelope);

  // Declares no expected effect. Provokes the refusal that an unverifiable
  // action is not an authorized action.
  static AuthorizedActionEnvelope WithNoPostcondition(
      AuthorizedActionEnvelope envelope);

  // Recomputes the digest so a mutation is presented as if it had been
  // authorized. Used by the tests that must prove a check other than the digest
  // is doing the refusing.
  static AuthorizedActionEnvelope Reseal(AuthorizedActionEnvelope envelope);

 private:
  const raw_ptr<DeterministicIdSource> ids_;
  const TaskId task_id_;
  const TabId tab_id_;
  const ActorLeaseId lease_id_;
  const SensitivityPolicyId policy_id_;
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_SUPPORT_BIP_REQUEST_BUILDER_H_
