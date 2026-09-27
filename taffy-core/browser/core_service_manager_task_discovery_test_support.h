// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_SERVICE_MANAGER_TASK_DISCOVERY_TEST_SUPPORT_H_
#define TAFFY_BROWSER_CORE_SERVICE_MANAGER_TASK_DISCOVERY_TEST_SUPPORT_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_test_support.h"
#include "taffy/common/public/bip_action.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {

namespace service_mojom = core_service::mojom;

// The zero-source errand fixture, and the records that stand one up.
//
// Extracted because the file that held it went over the source line cap when
// the landing-refusal suite was added, and the seam is the obvious one: the
// walk that stands an errand up is not a test of anything, it is what every
// test of the errand needs to exist.
namespace discovery_test {

constexpr uint64_t kGeneration = 1u;
constexpr uint64_t kAcceptedRevision = 3u;
constexpr uint32_t kNewSourceCap = 3u;
constexpr char kProfileId[] = "profile-1";
constexpr char kTaskId[] = "task-1";

uint64_t NowMonotonicMillis();

uint64_t NowUtcMillis();

service_mojom::CoreStateBrowserBindingsPtr EmptyBindings();

service_mojom::CoreStateUpdatePtr PublishedState(uint64_t sequence);

service_mojom::CoreServiceCommandPtr StartErrand(
    const std::string& browser_session_id,
    uint64_t deadline);

service_mojom::EffectEnvelopePtr StartStorageEffect(
    const service_mojom::CoreServiceCommand& command);

service_mojom::EffectResultPtr SuccessfulStorageResult(
    const service_mojom::EffectEnvelope& effect);

service_mojom::CoreStateBrowserBindingsPtr ErrandBindings(
    const std::string& browser_session_id);

service_mojom::TaskPolicyEffectPtr DiscoveryPolicyEffect(
    uint64_t deadline,
    const std::string& browser_session_id,
    uint32_t remaining_new_source_cap);

service_mojom::TaskEffectBindingPtr DiscoveryEffect(
    uint64_t deadline,
    const std::string& browser_session_id);

service_mojom::TaskEffectBindingPtr LandedNavigateBinding(
    const std::string& document_origin,
    const std::string& destination_origin);

ActionResult VerifiedResultFor(const service_mojom::TaskEffectBinding& binding);

// Walks the errand through the browser ledger's own two-part proof — the
// staged start, its storage commit, then the state that binds the accepted
// consent — because that ledger trusts no consent it did not witness.
class CoreServiceManagerTaskDiscoveryTest : public testing::Test {
 public:
  CoreServiceManagerTaskDiscoveryTest();
  ~CoreServiceManagerTaskDiscoveryTest() override;

 protected:
  void SetUp() override;
  void TearDown() override;

  // Hands the bootstrap to the executor; every completion lands in
  // `completions_` in arrival order.
  void ExecuteBootstrap();
  void Execute(service_mojom::TaskEffectBindingPtr effect);

  size_t DeferredCount() const;
  size_t PendingCount() const;

  content::BrowserTaskEnvironment task_environment_;
  std::unique_ptr<content::TestBrowserContext> context_;
  test::QuietManagerTail tail_;
  std::unique_ptr<CoreServiceManager> manager_;
  mojo::PendingReceiver<service_mojom::CoreSession> session_;
  std::vector<service_mojom::TaskEffectCompletionStatus> completions_;
};

}  // namespace discovery_test

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_SERVICE_MANAGER_TASK_DISCOVERY_TEST_SUPPORT_H_
