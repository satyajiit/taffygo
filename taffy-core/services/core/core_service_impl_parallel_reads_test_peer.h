// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_CORE_CORE_SERVICE_IMPL_PARALLEL_READS_TEST_PEER_H_
#define TAFFY_SERVICES_CORE_CORE_SERVICE_IMPL_PARALLEL_READS_TEST_PEER_H_

#include <utility>
#include <vector>

#include "taffy/services/core/core_service_impl_task_effect_test_peer.h"

namespace taffy {

// The browser callbacks travel over a real Mojo pipe. Only the Rust commit
// boundary is driven explicitly, as in the existing task-effect suites.
class CoreServiceImplParallelReadsTestPeer final {
 public:
  static void Start(CoreServiceImpl& impl,
                    std::vector<mojom::TaskEffectBindingPtr> effects) {
    impl.active_task_effect_.reset();
    ASSERT_TRUE(impl.publication_queue_.TryPushTaskEffects(std::move(effects)));
    impl.DispatchNextTaskEffect();
  }
  static void HoldRegistration(CoreServiceImpl& impl, bool held) {
    impl.state_registration_in_flight_ = held;
  }
  static size_t Pending(const CoreServiceImpl& impl) {
    return impl.parallel_source_reads_.size();
  }
  static bool HasCompletion(const CoreServiceImpl& impl, size_t index) {
    return !!impl.parallel_source_reads_.at(index).completion;
  }
  static bool Synthesized(const CoreServiceImpl& impl, size_t index) {
    return impl.parallel_source_reads_.at(index).synthesized;
  }
  static uint64_t EarliestDeadline(const CoreServiceImpl& impl) {
    return impl.SourceReadDeadline(0u);
  }
  static void Expire(CoreServiceImpl& impl, uint64_t now) {
    impl.ExpireSourceReads(now);
  }
  static bool RememberRevision(CoreServiceImpl& impl, uint64_t revision) {
    auto batch = core_service_impl_task_effect_test::StateBatch(1u, revision);
    return impl.RememberTaskRevisions(*batch.states.front().browser_bindings);
  }
  static bool Acknowledge(CoreServiceImpl& impl, uint64_t revision) {
    auto batch = core_service_impl_task_effect_test::StateBatch(2u, revision);
    const auto& bindings = *batch.states.front().browser_bindings;
    return impl.AcknowledgeCommittedTaskEffect(true, bindings) &&
           impl.RememberTaskRevisions(bindings);
  }
  static mojom::TaskEffectCompletionPtr Activate(CoreServiceImpl& impl) {
    return impl.ActivateNextSourceRead();
  }
  static const mojom::TaskEffectBinding& Active(const CoreServiceImpl& impl) {
    return *impl.active_task_effect_;
  }
  static uint64_t Now(const CoreServiceImpl& impl) {
    return impl.NowMonotonicMillis();
  }
  static void Fail(CoreServiceImpl& impl) {
    impl.FailStatePublication("test-withdrawal");
  }
};

}  // namespace taffy

#endif  // TAFFY_SERVICES_CORE_CORE_SERVICE_IMPL_PARALLEL_READS_TEST_PEER_H_
