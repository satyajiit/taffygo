// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/rust_core_state.h"

#include <cstdint>
#include <optional>
#include <string>

#include "base/test/mock_log.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core.h"
#include "taffy/services/core/service_bridge.rs.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

// What these cover: the guards `ToStatePublication` puts between a state the
// sandboxed core published and the browser bindings the manager trusts. A
// refusal here is not one bad state — it is the utility process answering
// "state-native-projection" and exiting, on every start, for as long as the
// journaled fact that tripped it survives. So the shape an errand's consent
// takes (decision 0087: zero named sources, discovery on) has to project, and
// each refusal has to name the loop it is actually in.

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;

constexpr uint64_t kGeneration = 3u;
constexpr uint8_t kNoModelRequiredRoute = 3u;

bridge::BridgeState StateWithOneTask() {
  bridge::BridgeState state{};
  state.service_generation = kGeneration;
  state.sequence = 7u;
  state.core_status_schema_version = 1u;
  state.payload.push_back(1u);
  bridge::BridgeTaskRevision revision{};
  revision.task_id = "task-1";
  revision.service_generation = kGeneration;
  revision.task_revision = 2u;
  state.task_revisions.push_back(revision);
  return state;
}

bridge::BridgeAcceptedTaskConsent ErrandConsent() {
  bridge::BridgeAcceptedTaskConsent consent{};
  consent.task_id = "task-1";
  consent.service_generation = kGeneration;
  consent.current_task_revision = 2u;
  consent.accepted_revision = 1u;
  consent.browser_session_id = "browser-session-1";
  consent.receipt_id = "receipt-1";
  consent.source_discovery_enabled = true;
  consent.new_source_cap = 1u;
  consent.provider_route = kNoModelRequiredRoute;
  return consent;
}

bridge::BridgeConsentSource Source(const char* source_id, const char* tab_id) {
  bridge::BridgeConsentSource source{};
  source.source_id = source_id;
  source.tab_id = tab_id;
  source.normalized_origin = "https://source.test";
  source.has_canonical_locator = false;
  return source;
}

// The consent an errand journals: no named site, discovery on, one new site
// allowed. This is exactly the shape a `sources.empty()` guard used to refuse,
// which took the core down on every start once such a consent had been
// journaled (verification report 2.25).
TEST(RustCoreStateTest, ZeroSourceErrandConsentProjects) {
  bridge::BridgeState state = StateWithOneTask();
  state.accepted_task_consents.push_back(ErrandConsent());

  const std::optional<CoreStatePublication> published =
      core_service_internal::ToStatePublication(state);

  ASSERT_TRUE(published);
  ASSERT_TRUE(published->browser_bindings);
  ASSERT_EQ(1u, published->browser_bindings->accepted_task_consents.size());
  const mojom::AcceptedTaskConsentBindingPtr& binding =
      published->browser_bindings->accepted_task_consents.front();
  ASSERT_TRUE(binding->consent_preview);
  EXPECT_TRUE(binding->consent_preview->sources.empty());
  EXPECT_TRUE(binding->consent_preview->source_discovery_enabled);
  EXPECT_EQ(1u, binding->consent_preview->new_source_cap);
  EXPECT_EQ(mojom::TaskProviderRoute::kNoModelRequired,
            binding->consent_preview->provider_route);
}

// The upper bound stays: a consent naming more sources than the contract
// allows is still refused, and the refusal names the consent clause.
TEST(RustCoreStateTest, TooManySourcesStillRefusesByName) {
  bridge::BridgeState state = StateWithOneTask();
  bridge::BridgeAcceptedTaskConsent consent = ErrandConsent();
  for (uint32_t index = 0; index <= mojom::kMaxTaskConsentSources; ++index) {
    const std::string suffix = std::to_string(index);
    consent.sources.push_back(
        Source(("source-" + suffix).c_str(), ("tab-" + suffix).c_str()));
  }
  state.accepted_task_consents.push_back(consent);

  base::test::MockLog log;
  EXPECT_CALL(log, Log(logging::LOGGING_ERROR, testing::_, testing::_,
                       testing::_,
                       testing::HasSubstr("[taffy_core_state_projection_refused] "
                                          "at=accepted-consent/too-many-sources")))
      .WillOnce(testing::Return(true));
  log.StartCapturingLogs();
  const std::optional<CoreStatePublication> published =
      core_service_internal::ToStatePublication(state);
  log.StopCapturingLogs();

  EXPECT_FALSE(published);
}

// A malformed source is refused as a source, not — as the label used to say —
// as a committed approval. The next two loops had the same off-by-one, so a
// bad approval read as a bad effect and a bad effect as "trailing".
TEST(RustCoreStateTest, MalformedSourceRefusesAsSource) {
  bridge::BridgeState state = StateWithOneTask();
  bridge::BridgeAcceptedTaskConsent consent = ErrandConsent();
  consent.sources.push_back(Source("source-1", "tab-1"));
  consent.sources.push_back(Source("source-1", "tab-2"));  // duplicate id
  state.accepted_task_consents.push_back(consent);

  base::test::MockLog log;
  EXPECT_CALL(log, Log(logging::LOGGING_ERROR, testing::_, testing::_,
                       testing::_,
                       testing::HasSubstr("[taffy_core_state_projection_refused] "
                                          "at=accepted-consent/source")))
      .WillOnce(testing::Return(true));
  log.StartCapturingLogs();
  const std::optional<CoreStatePublication> published =
      core_service_internal::ToStatePublication(state);
  log.StopCapturingLogs();

  EXPECT_FALSE(published);
}

}  // namespace
}  // namespace taffy
