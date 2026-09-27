// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_api/profile_core_api_facade.h"

#include "taffy/browser/core_api/profile_core_api_facade_test_support.h"

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/run_loop.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "taffy/browser/core_effect_broker.h"
#include "taffy/browser/core_page_observation_broker.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_test_support.h"
#include "taffy/browser/model/custom_provider_endpoint_store.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;

// The response half of the credential rule, and it is a compile-time fact
// rather than a test. Every credential-bearing provider request answers only
// one closed status, so there is no field in which a key, a fragment of one,
// or its length could travel back to a surface. The exact-lifetime start reply
// intentionally also returns a browser-minted flow id; that request accepts
// only a provider id, so it has no credential value it could echo. The request
// half, where a handle does travel and reaches exactly one field, is asserted in
// core_api_command_factory_provider_unittest.cc.
using core_api_test::RecordedStatus;
using core_api_test::StatusOnly;
static_assert(
    std::is_same_v<api::TaffyProfileCoreApi::SaveProviderCredentialCallback,
                   StatusOnly>);
static_assert(
    std::is_same_v<api::TaffyProfileCoreApi::ForgetProviderCredentialCallback,
                   StatusOnly>);
static_assert(
    std::is_same_v<api::TaffyProfileCoreApi::SetProviderCredentialStateCallback,
                   StatusOnly>);
static_assert(std::is_same_v<api::TaffyProfileCoreApi::ProbeProviderKeyCallback,
                             StatusOnly>);
static_assert(
    std::is_same_v<api::TaffyProfileCoreApi::StartProviderAuthCallback,
                   StatusOnly>);
using StartProviderAuthFlowReply = base::OnceCallback<void(
    api::CoreApiSubmissionStatus,
    const std::optional<std::string>&)>;
static_assert(std::is_same_v<
              api::TaffyProfileCoreApi::StartProviderAuthFlowCallback,
              StartProviderAuthFlowReply>);
static_assert(
    std::is_same_v<api::TaffyProfileCoreApi::CancelProviderAuthCallback,
                   StatusOnly>);
static_assert(
    std::is_same_v<api::TaffyProfileCoreApi::SaveCustomProviderCallback,
                   StatusOnly>);
static_assert(
    std::is_same_v<api::TaffyProfileCoreApi::RemoveCustomProviderCallback,
                   StatusOnly>);
static_assert(
    std::is_same_v<api::TaffyProfileCoreApi::SetProviderModelPreferenceCallback,
                   StatusOnly>);
static_assert(
    std::is_same_v<api::TaffyProfileCoreApi::ProbeCustomEndpointCallback,
                   StatusOnly>);

class RecordedProviderAuthStart final {
 public:
  StartProviderAuthFlowReply Bind() {
    return base::BindOnce(&RecordedProviderAuthStart::Record,
                          base::Unretained(this));
  }

  bool answered() const { return status_.has_value(); }
  const std::optional<api::CoreApiSubmissionStatus>& status() const {
    return status_;
  }
  const std::optional<std::string>& flow_id() const { return flow_id_; }

 private:
  void Record(api::CoreApiSubmissionStatus status,
              const std::optional<std::string>& flow_id) {
    status_ = status;
    flow_id_ = flow_id;
  }

  std::optional<api::CoreApiSubmissionStatus> status_;
  std::optional<std::string> flow_id_;
};

// What a snapshot said, which is all this suite asserts on.
struct RecordedSnapshot {
  api::CoreAvailability availability;
  bool carried_payload;
};

struct RecordedTaskAnswer {
  std::string task_id;
  std::string call_id;
  uint32_t sequence;
  std::optional<std::string> text;
  bool terminal;
  bool complete;
};

class RecordingObserver final : public api::TaffyProfileCoreApiObserver {
 public:
  mojo::PendingRemote<api::TaffyProfileCoreApiObserver> Bind() {
    return receiver_.BindNewPipeAndPassRemote();
  }

  const std::vector<RecordedSnapshot>& snapshots() const { return snapshots_; }
  const std::vector<RecordedTaskAnswer>& task_answers() const {
    return task_answers_;
  }
  void Forget() {
    snapshots_.clear();
    task_answers_.clear();
  }

 private:
  void OnSnapshot(
      api::CoreAvailability availability,
      uint64_t service_generation,
      uint64_t state_sequence,
      uint32_t status_schema_version,
      const std::optional<std::vector<uint8_t>>& status_payload) override {
    snapshots_.push_back({availability, status_payload.has_value()});
  }
  void OnPermissionRequest(const std::string& request_id,
                           api::PlatformPermission permission) override {}
  void OnAssetProgress(const std::string& asset_id,
                       const std::string& asset_revision,
                       uint64_t written_bytes,
                       uint64_t total_bytes) override {}
  void OnComposerCompletion(const std::string& request_id,
                            const std::optional<std::string>& text) override {}
  void OnTaskAnswerDelta(const std::string& task_id,
                         const std::string& call_id,
                         uint32_t sequence,
                         const std::optional<std::string>& text,
                         bool terminal,
                         bool complete) override {
    task_answers_.push_back(
        {task_id, call_id, sequence, text, terminal, complete});
  }
  void OnTaskArtifactExport(const std::string& request_id,
                            const std::string& task_id,
                            const std::string& artifact_id,
                            api::TaskArtifactKind kind,
                            const std::vector<uint8_t>& content) override {}

  std::vector<RecordedSnapshot> snapshots_;
  std::vector<RecordedTaskAnswer> task_answers_;
  mojo::Receiver<api::TaffyProfileCoreApiObserver> receiver_{this};
};

class ProfileCoreApiFacadeTest : public testing::Test {
 protected:
  ProfileCoreApiFacadeTest()
      : browser_context_(std::make_unique<content::TestBrowserContext>()),
        manager_(core_api_test::MakeFacadeTestManager(
            tail_, browser_context_.get())),
        facade_(manager_.get()) {}

  // Attaches the surface the way Chromium does, then drops whatever the
  // attachment itself provoked. Only the transitions a test drives are read.
  void Attach() {
    static_cast<api::TaffyProfileCoreApi&>(facade_).Observe(observer_.Bind());
    base::RunLoop().RunUntilIdle();
    observer_.Forget();
  }

  void Announce(CoreServiceManager::Availability availability) {
    static_cast<CoreServiceManager::Observer&>(facade_)
        .OnCoreAvailabilityChanged(availability);
    base::RunLoop().RunUntilIdle();
  }

  void PublishTaskAnswer(const std::string& task_id,
                         const std::string& call_id,
                         uint32_t sequence,
                         std::optional<std::string> text,
                         bool terminal,
                         bool complete) {
    static_cast<CoreServiceManager::Observer&>(facade_).OnTaskAnswerDelta(
        task_id, call_id, sequence, text, terminal, complete);
  }

  content::BrowserTaskEnvironment task_environment_;
  // Before the manager: the filtering service the tail builds watches the
  // tail's pref service for the manager's whole lifetime.
  test::QuietManagerTail tail_;
  std::unique_ptr<content::TestBrowserContext> browser_context_;
  std::unique_ptr<CoreServiceManager> manager_;
  ProfileCoreApiFacade facade_;
  RecordingObserver observer_;
};

// The regression this file was added for.
//
// `status_payload` is contractually absent only when no complete state
// exists, so a `kReady` snapshot with nothing attached is a contradiction
// rather than a thin notification -- and the Android endpoint reads it as
// one, discarding the state it was holding. Because the payload-carrying
// snapshot arrives *first* and a ready core is then quiescent, that discard
// was permanent: every core-backed surface stayed empty for the life of the
// process, and the start page waited forever on a delivery view it had
// already been given.
TEST_F(ProfileCoreApiFacadeTest, ReadyIsNeverAnnouncedWithoutTheState) {
  Attach();

  Announce(CoreServiceManager::Availability::kReady);

  EXPECT_TRUE(observer_.snapshots().empty());
}

// The other half, so the fix above cannot be "publish nothing". An
// availability that is not ready is a fact about the core rather than about
// its contents, and there is no state it could be withholding.
TEST_F(ProfileCoreApiFacadeTest, EveryOtherAvailabilityIsStillPublished) {
  Attach();

  Announce(CoreServiceManager::Availability::kStarting);
  Announce(CoreServiceManager::Availability::kCircuitOpen);
  Announce(CoreServiceManager::Availability::kUnavailable);

  ASSERT_EQ(3u, observer_.snapshots().size());
  EXPECT_EQ(api::CoreAvailability::kStarting,
            observer_.snapshots()[0].availability);
  EXPECT_EQ(api::CoreAvailability::kCircuitOpen,
            observer_.snapshots()[1].availability);
  EXPECT_EQ(api::CoreAvailability::kUnavailable,
            observer_.snapshots()[2].availability);
  for (const RecordedSnapshot& snapshot : observer_.snapshots()) {
    EXPECT_FALSE(snapshot.carried_payload);
  }
}

TEST_F(ProfileCoreApiFacadeTest,
       TaskAnswerPushesOnlyCarryBoundedWellShapedResidency) {
  Attach();

  PublishTaskAnswer("task-1", "call-1", 0u,
                    std::optional<std::string>("answer"), false, false);
  PublishTaskAnswer("task-1", "call-1", 1u, std::nullopt, true, true);
  // Each malformed event is refused whole: a surface must never repair a
  // terminal carrying text or accept a delta past the generated byte bound.
  PublishTaskAnswer("task-1", "call-1", 2u,
                    std::optional<std::string>("not-terminal"), true, false);
  PublishTaskAnswer(
      "task-1", "call-1", 3u,
      std::optional<std::string>(
          std::string(api::kMaxTaskAnswerDeltaBytes + 1u, 'x')),
      false, false);
  PublishTaskAnswer("task-1", "call-1", 4u,
                    std::optional<std::string>("premature"), false, true);
  base::RunLoop().RunUntilIdle();

  ASSERT_EQ(2u, observer_.task_answers().size());
  ASSERT_TRUE(observer_.task_answers()[0].text.has_value());
  EXPECT_EQ("answer", *observer_.task_answers()[0].text);
  EXPECT_FALSE(observer_.task_answers()[0].terminal);
  EXPECT_EQ(1u, observer_.task_answers()[1].sequence);
  EXPECT_FALSE(observer_.task_answers()[1].text.has_value());
  EXPECT_TRUE(observer_.task_answers()[1].terminal);
  EXPECT_TRUE(observer_.task_answers()[1].complete);
}

// The refusal path of every provider handler.
//
// Each is answered here, before a factory has minted anything, and the answer
// arrives without the loop being run — which is the assertion that matters.
// A refusal that reached CoreServiceManager first would have spent an
// operation identity and an idempotency key on a request the core was never
// going to accept, and the browser journals an effect identity before it
// dispatches: a spent one is not recoverable by retrying.
TEST_F(ProfileCoreApiFacadeTest, EveryProviderHandlerRefusesBeforeSubmitting) {
  auto& surface = static_cast<api::TaffyProfileCoreApi&>(facade_);

  // Each handler is given a request only its own builder can refuse, so a
  // handler wired to the wrong builder would accept here rather than refuse:
  // an empty credential handle is checked by the credential builder alone, and
  // a cleartext address off the local network by the custom-provider builder
  // alone.
  //
  // That endpoint used to be a link-local one, and it is not any more: decision
  // 0096 admits every private address a person might run a server on, so
  // `https://169.254.169.254/...` is now a save this factory accepts and this
  // suite must not send — submitting it would launch the core service, which
  // no unit test in this directory does.
  RecordedStatus saved;
  surface.SaveProviderCredential(
      "anthropic", api::ProviderAuthMethodView::kApiKey, "", saved.Bind());
  RecordedStatus forgotten;
  surface.ForgetProviderCredential("Anthropic", forgotten.Bind());
  RecordedStatus reported;
  surface.SetProviderCredentialState("anthropic key",
                                     api::ProviderCredentialStateView::kUsable,
                                     reported.Bind());
  RecordedStatus probed;
  surface.ProbeProviderKey("anthropic", "", probed.Bind());
  RecordedStatus started;
  surface.StartProviderAuth("-anthropic", started.Bind());
  RecordedProviderAuthStart started_flow;
  surface.StartProviderAuthFlow("-anthropic", started_flow.Bind());
  RecordedStatus cancelled;
  surface.CancelProviderAuth("", cancelled.Bind());
  RecordedStatus custom;
  surface.SaveCustomProvider(
      "my-gateway", "My gateway", "http://gateway.example.com/v1",
      api::ProviderWireApiView::kOpenAiResponses, std::optional<std::string>(),
      std::vector<api::CustomModelSpecViewPtr>(), api::DetectedServerViewPtr(),
      custom.Bind());
  RecordedStatus removed;
  surface.RemoveCustomProvider("my gateway", removed.Bind());
  // The standing choice, refused on a model id past the contract's bound. It
  // is the one handler that also writes, and this is the case that says it did
  // not: a refused build must leave the preference file untouched, which the
  // store's own suite asserts from the other side.
  RecordedStatus preferred;
  surface.SetProviderModelPreference(
      "anthropic",
      std::optional<std::string>(std::string(api::kMaxModelIdBytes + 1u, 'm')),
      api::ThinkingPreferenceViewPtr(), preferred.Bind());
  // The endpoint probe applies the save's endpoint rule, so cleartext to
  // something that is not a literal local machine is refused before a call is
  // spent proving it answers.
  RecordedStatus endpoint;
  surface.ProbeCustomEndpoint(
      "http://gateway.example.com", api::ProviderWireApiView::kOpenAiResponses,
      std::optional<std::string>(), "my-gateway", endpoint.Bind());

  for (const RecordedStatus* recorded :
       {&saved, &forgotten, &reported, &probed, &started, &cancelled, &custom,
        &removed, &preferred, &endpoint}) {
    ASSERT_TRUE(recorded->value().has_value());
    EXPECT_EQ(*recorded->value(),
              api::CoreApiSubmissionStatus::kInvalidRequest);
  }
  ASSERT_TRUE(started_flow.answered());
  EXPECT_EQ(*started_flow.status(),
            api::CoreApiSubmissionStatus::kInvalidRequest);
  EXPECT_FALSE(started_flow.flow_id().has_value());
}

// The other half — that a well-formed request is *not* refused — is asserted
// in core_api_command_factory_provider_unittest.cc rather than here, and
// deliberately. Submitting one reaches CoreServiceManager::EnsureStarted,
// which launches the sandboxed core; no unit test in this directory does that,
// and a suite that started a service process to observe a callback not being
// run would be measuring the launch rather than the handler. What the facade
// adds above the builder is the refusal path and one exit, which is what this
// suite covers.

// What the two custom-provider handlers do to the browser's own file
// (decision 0096 section 1).
//
// The core is shut down first, in every case below, and that is not an attempt
// to test a shutdown: it is how a well-formed provider command can be
// submitted here at all, because a manager that has shut down answers Submit
// without reaching EnsureStarted. What is under test is what happened to the
// register *before* the command was forwarded, and that half is the same on
// either side of a shutdown — the handler writes, then forwards, and the write
// is not conditional on what the forward answers.
class ProfileCoreApiFacadeRegisterTest : public ProfileCoreApiFacadeTest {
 protected:
  void SetUp() override { manager_->Shutdown(); }

  api::TaffyProfileCoreApi& surface() {
    return static_cast<api::TaffyProfileCoreApi&>(facade_);
  }

  std::optional<std::string> Registered(const std::string& provider_id) {
    return ReadCustomProviderEndpoint(*manager_->profile_prefs(), provider_id);
  }

  void PreRegister(const std::string& provider_id,
                   const std::string& endpoint) {
    ASSERT_TRUE(WriteCustomProviderEndpoint(manager_->profile_prefs(),
                                            provider_id, endpoint));
  }
};

// The address decision 0096 exists to support, saved and registered exactly as
// it was typed. Anything else — a trailing slash added, a port dropped, an
// origin derived — and the byte comparison at send time would refuse the
// address the person is looking at on their own screen.
//
// The status is not asserted, and could not usefully be: a shut-down manager
// answers a perfectly good command with the same `INVALID_REQUEST` a refusal
// answers with. The register is what this case reads, and it is the thing that
// tells the two apart — a refused save leaves it empty, which is the case
// below.
TEST_F(ProfileCoreApiFacadeRegisterTest, ASaveRegistersTheAddressExactly) {
  RecordedStatus saved;
  surface().SaveCustomProvider(
      "my-gateway", "My gateway", "http://192.168.1.9:11434/v1",
      api::ProviderWireApiView::kOpenAiCompletions,
      std::optional<std::string>(), std::vector<api::CustomModelSpecViewPtr>(),
      api::DetectedServerViewPtr(), saved.Bind());

  EXPECT_EQ(Registered("my-gateway"),
            std::optional<std::string>("http://192.168.1.9:11434/v1"));
}

// A refused save leaves the file alone. The build comes first for exactly this
// reason: a register holding a row the factory would not have accepted is a
// row that outlives every attempt to correct it, because the save that would
// have replaced it is refused too.
TEST_F(ProfileCoreApiFacadeRegisterTest, ARefusedSaveRegistersNothing) {
  RecordedStatus refused;
  surface().SaveCustomProvider(
      "my-gateway", "My gateway", "http://gateway.example.com/v1",
      api::ProviderWireApiView::kOpenAiCompletions,
      std::optional<std::string>(), std::vector<api::CustomModelSpecViewPtr>(),
      api::DetectedServerViewPtr(), refused.Bind());

  ASSERT_TRUE(refused.value().has_value());
  EXPECT_EQ(*refused.value(), api::CoreApiSubmissionStatus::kInvalidRequest);
  EXPECT_EQ(Registered("my-gateway"), std::nullopt);
}

// Removing the provider withdraws its address with it. A registered address
// that outlived its provider would be an address the register goes on
// recognising for a provider nobody is configured to use.
TEST_F(ProfileCoreApiFacadeRegisterTest, ARemovalForgetsTheAddress) {
  PreRegister("my-gateway", "http://192.168.1.9:11434/v1");

  RecordedStatus removed;
  surface().RemoveCustomProvider("my-gateway", removed.Bind());

  EXPECT_EQ(Registered("my-gateway"), std::nullopt);
}

// The probe writes nothing at all.
//
// It is asked before a provider exists, so there is nothing for a row to
// belong to — and a probe that registered what it was asked about would be a
// way to put an address in the register without ever saving a provider. The
// row that is already there is left exactly as it was, which is the other half
// of the same property.
//
// Since Core API 3.18 the probe does carry a provider identity, and that makes
// this case sharper rather than weaker: the identity it names is a *draft* the
// verdict is filed under, not a record, and naming one must still not create a
// register row. So the probe below names `second-gateway`, a name no row
// exists under, and the register must still hold exactly the one row that was
// there before.
TEST_F(ProfileCoreApiFacadeRegisterTest, AProbeLeavesTheRegisterAlone) {
  PreRegister("my-gateway", "http://192.168.1.9:11434/v1");

  RecordedStatus probed;
  surface().ProbeCustomEndpoint(
      "http://10.0.0.5:8000/v1", api::ProviderWireApiView::kOpenAiCompletions,
      std::optional<std::string>(), "second-gateway", probed.Bind());

  EXPECT_EQ(Registered("my-gateway"),
            std::optional<std::string>("http://192.168.1.9:11434/v1"));
  // And no row appeared under the draft identity it did name, which is the
  // half that could regress: one row, the one that was pre-registered.
  EXPECT_EQ(Registered("second-gateway"), std::nullopt);
  EXPECT_EQ(ReadCustomProviderEndpoints(*manager_->profile_prefs()).size(), 1u);
}

}  // namespace
}  // namespace taffy
