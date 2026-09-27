// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdint.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/test/bind.h"
#include "base/time/time.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "taffy/browser/core_effect_broker.h"
#include "taffy/browser/core_page_observation_broker.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_task_effect_test_peer.h"
#include "taffy/browser/core_service_manager_test_support.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr uint64_t kGeneration = 1u;
constexpr uint64_t kRevision = 4u;

mojom::CoreStateBrowserBindingsPtr Bindings() {
  auto bindings = mojom::CoreStateBrowserBindings::New();
  bindings->service_generation = kGeneration;
  bindings->state_sequence = 1u;
  bindings->task_revisions.push_back(mojom::TaskRevisionBinding::New(
      "task-1", kGeneration, kRevision,
      std::vector<mojom::TaskControlKind>()));
  return bindings;
}

mojom::TaskEffectBindingPtr InlineDocumentExport() {
  const int64_t deadline =
      (base::TimeTicks::Now() + base::Seconds(30)).since_origin().InMilliseconds();
  auto effect = mojom::TaskEffectBinding::New();
  effect->operation = mojom::OperationEnvelope::New(
      "artifact-operation", kGeneration, kRevision,
      deadline < 0 ? 0u : static_cast<uint64_t>(deadline),
      "artifact-idempotency");
  effect->effect_id = "artifact-effect";
  effect->task_id = "task-1";
  effect->kind = mojom::TaskReducerEffectKind::kExportArtifact;
  effect->export_artifact = mojom::TaskArtifactEffect::New(
      mojom::TaskArtifactKind::kDocx, "artifact-inline", kRevision,
      std::vector<uint8_t>{'P', 'K', 0x03u, 0x04u});
  return effect;
}

class ArtifactObserver final : public CoreServiceManager::Observer {
 public:
  bool OnTaskArtifactExport(const std::string& task_id,
                            const std::string& artifact_id,
                            mojom::TaskArtifactKind kind,
                            const std::vector<uint8_t>& content) override {
    seen_task = task_id;
    seen_artifact = artifact_id;
    seen_kind = kind;
    seen_content = content;
    return true;
  }

  std::string seen_task;
  std::string seen_artifact;
  mojom::TaskArtifactKind seen_kind = mojom::TaskArtifactKind::kMarkdown;
  std::vector<uint8_t> seen_content;
};

TEST(CoreServiceManagerArtifactRoutingTest,
     InlineDocumentBytesBypassIsolatedToolCustody) {
  content::BrowserTaskEnvironment task_environment;
  auto context = std::make_unique<content::TestBrowserContext>();
  auto tools = std::make_unique<ProfileToolSupervisor>(
      kGeneration, ProfileToolSupervisor::PythonPorts::Unsupported(),
      ProfileToolSupervisor::LocalModelPorts::Unsupported(),
      ProfileToolSupervisor::MediaPorts::Unsupported());
  auto observation =
      base::MakeRefCounted<CorePageObservationBroker>(context.get());
  test::QuietManagerTail tail;
  auto manager = tail.MakeManager(
      context.get(), /*storage_broker=*/nullptr, std::move(tools), observation,
      std::make_unique<CoreEffectBroker>(CoreEffectBroker::Handlers{}));
  mojo::PendingReceiver<mojom::CoreSession> session =
      CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager);
  ASSERT_EQ(mojom::PendingApprovalRegistrationStatus::kRegistered,
            CoreServiceManagerTaskEffectTestPeer::RegisterBindings(*manager,
                                                                    Bindings()));

  ArtifactObserver observer;
  manager->AddObserver(&observer);
  mojom::TaskEffectCompletionStatus status =
      mojom::TaskEffectCompletionStatus::kRefused;
  CoreServiceManagerTaskEffectTestPeer::Execute(
      *manager, InlineDocumentExport(),
      base::BindLambdaForTesting([&](mojom::TaskEffectCompletionPtr result) {
        ASSERT_TRUE(result);
        status = result->status;
      }));

  EXPECT_EQ(mojom::TaskEffectCompletionStatus::kSucceeded, status);
  EXPECT_EQ("task-1", observer.seen_task);
  EXPECT_EQ("artifact-inline", observer.seen_artifact);
  EXPECT_EQ(mojom::TaskArtifactKind::kDocx, observer.seen_kind);
  EXPECT_EQ((std::vector<uint8_t>{'P', 'K', 0x03u, 0x04u}),
            observer.seen_content);

  manager->RemoveObserver(&observer);
  manager->Shutdown();
  static_cast<void>(session);
}

}  // namespace
}  // namespace taffy
