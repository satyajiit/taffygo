// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_state_binding_registry.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "taffy/browser/core_api/task_consent_shape.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

mojom::TaskRevisionBindingPtr Task(
    uint64_t generation,
    uint64_t revision,
    std::string task_id,
    std::vector<mojom::TaskControlKind> controls = {}) {
  auto task = mojom::TaskRevisionBinding::New();
  task->task_id = std::move(task_id);
  task->service_generation = generation;
  task->task_revision = revision;
  task->allowed_controls = std::move(controls);
  return task;
}

mojom::PendingApprovalBindingPtr Approval(uint64_t generation,
                                          uint64_t revision,
                                          std::string task_id,
                                          std::string action_id) {
  auto approval = mojom::PendingApprovalBinding::New();
  approval->task_id = std::move(task_id);
  approval->action_id = std::move(action_id);
  approval->proposal_digest = std::string(64u, 'a');
  approval->service_generation = generation;
  approval->task_revision = revision;
  return approval;
}

mojom::CoreStateBrowserBindingsPtr Snapshot(uint64_t generation,
                                            uint64_t sequence) {
  auto snapshot = mojom::CoreStateBrowserBindings::New();
  snapshot->service_generation = generation;
  snapshot->state_sequence = sequence;
  return snapshot;
}

mojom::PendingPermissionBindingPtr Permission(uint64_t generation,
                                              uint64_t revision,
                                              std::string task_id,
                                              std::string request_id) {
  auto permission = mojom::PendingPermissionBinding::New();
  permission->task_id = std::move(task_id);
  permission->request_id = std::move(request_id);
  permission->permission = mojom::PlatformPermission::kCamera;
  permission->service_generation = generation;
  permission->task_revision = revision;
  permission->deadline_monotonic_ms = 9'000u;
  permission->deadline_utc_ms = 1'800'000'009'000u;
  permission->browser_session_id = "browser-session-1";
  return permission;
}

mojom::TaskSettlementBindingPtr Settlement(uint64_t generation,
                                           uint64_t revision,
                                           std::string task_id) {
  auto settlement = mojom::TaskSettlementBinding::New();
  settlement->task_id = std::move(task_id);
  settlement->service_generation = generation;
  settlement->task_revision = revision;
  settlement->kind = mojom::TaskSettlementKind::kCancel;
  return settlement;
}

mojom::TerminalTaskBindingPtr Terminal(uint64_t generation,
                                       uint64_t revision,
                                       std::string task_id) {
  return mojom::TerminalTaskBinding::New(std::move(task_id), generation,
                                         revision,
                                         mojom::TerminalTaskKind::kCompleted);
}

mojom::AcceptedTaskConsentBindingPtr Consent(uint64_t generation,
                                             uint64_t revision,
                                             mojom::TaskProviderRoute route) {
  auto consent = mojom::AcceptedTaskConsentBinding::New();
  consent->task_id = "task-1";
  consent->service_generation = generation;
  consent->current_task_revision = revision;
  consent->accepted_revision = revision;
  consent->browser_session_id = "browser-session-1";
  consent->receipt_id = "receipt-1";
  consent->consent_preview = mojom::TaskConsentPreview::New();
  consent->consent_preview->sources.push_back(mojom::TaskConsentSource::New(
      "source-1", "tab-1", "https://example.test", std::nullopt));
  consent->consent_preview->provider_route = route;
  return consent;
}

TEST(CoreStateBindingRegistryTest, ReplacesBothMapsAndEmptyClears) {
  CoreStateBindingRegistry registry;
  auto first = Snapshot(7u, 1u);
  first->task_revisions.push_back(Task(7u, 3u, "task-1"));
  first->pending_approvals.push_back(Approval(7u, 3u, "task-1", "action-1"));
  ASSERT_EQ(mojom::PendingApprovalRegistrationStatus::kRegistered,
            registry.Replace(std::move(first)));
  EXPECT_EQ(3u, registry.FindTaskRevision("task-1"));
  const std::optional<PendingApprovalLookup> found =
      registry.FindPendingApproval("task-1", "action-1");
  ASSERT_TRUE(found);
  EXPECT_EQ(std::string(64u, 'a'), found->proposal_digest);

  ASSERT_EQ(mojom::PendingApprovalRegistrationStatus::kRegistered,
            registry.Replace(Snapshot(7u, 2u)));
  EXPECT_FALSE(registry.FindTaskRevision("task-1"));
  EXPECT_FALSE(registry.FindPendingApproval("task-1", "action-1"));
  EXPECT_EQ(2u, registry.state_sequence());
}

TEST(CoreStateBindingRegistryTest, InvalidReplacementPreservesPriorSnapshot) {
  CoreStateBindingRegistry registry;
  auto first = Snapshot(4u, 1u);
  first->task_revisions.push_back(Task(4u, 2u, "task-1"));
  ASSERT_EQ(mojom::PendingApprovalRegistrationStatus::kRegistered,
            registry.Replace(std::move(first)));

  auto invalid = Snapshot(4u, 2u);
  invalid->task_revisions.push_back(Task(5u, 3u, "task-2"));
  EXPECT_EQ(mojom::PendingApprovalRegistrationStatus::kInvalidBinding,
            registry.Replace(std::move(invalid)));
  EXPECT_EQ(2u, registry.FindTaskRevision("task-1"));
  EXPECT_FALSE(registry.FindTaskRevision("task-2"));
  EXPECT_EQ(1u, registry.state_sequence());
}

TEST(CoreStateBindingRegistryTest,
     ManagedProviderRouteRestoresExactSourceAuthorityBinding) {
  CoreStateBindingRegistry registry;
  auto snapshot = Snapshot(5u, 1u);
  snapshot->task_revisions.push_back(Task(5u, 2u, "task-1"));
  snapshot->accepted_task_consents.push_back(
      Consent(5u, 2u, mojom::TaskProviderRoute::kManagedService));

  EXPECT_EQ(mojom::PendingApprovalRegistrationStatus::kRegistered,
            registry.Replace(std::move(snapshot)));
  EXPECT_EQ(1u, registry.task_count());
}

TEST(CoreStateBindingRegistryTest,
     MultiSourceAndZeroSourceErrandBindingsAreAdmittedExactly) {
  CoreStateBindingRegistry multi_registry;
  auto multi = Snapshot(6u, 1u);
  multi->task_revisions.push_back(Task(6u, 2u, "task-1"));
  auto multi_consent =
      Consent(6u, 2u, mojom::TaskProviderRoute::kDirectUserKey);
  multi_consent->consent_preview->sources.push_back(
      mojom::TaskConsentSource::New("source-2", "tab-2",
                                    "https://second.test", std::nullopt));
  multi->accepted_task_consents.push_back(std::move(multi_consent));
  EXPECT_EQ(mojom::PendingApprovalRegistrationStatus::kRegistered,
            multi_registry.Replace(std::move(multi)));

  CoreStateBindingRegistry errand_registry;
  auto errand = Snapshot(6u, 1u);
  errand->task_revisions.push_back(Task(6u, 2u, "task-1"));
  auto errand_consent =
      Consent(6u, 2u, mojom::TaskProviderRoute::kManagedService);
  errand_consent->consent_preview->sources.clear();
  errand_consent->consent_preview->source_discovery_enabled = true;
  errand_consent->consent_preview->new_source_cap = 8u;
  errand->accepted_task_consents.push_back(std::move(errand_consent));
  EXPECT_EQ(mojom::PendingApprovalRegistrationStatus::kRegistered,
            errand_registry.Replace(std::move(errand)));
}

TEST(CoreStateBindingRegistryTest,
     MalformedDiscoveryBindingPreservesPriorAtomicSnapshot) {
  CoreStateBindingRegistry registry;
  auto valid = Snapshot(8u, 1u);
  valid->task_revisions.push_back(Task(8u, 2u, "task-1"));
  valid->accepted_task_consents.push_back(
      Consent(8u, 2u, mojom::TaskProviderRoute::kManagedService));
  ASSERT_EQ(mojom::PendingApprovalRegistrationStatus::kRegistered,
            registry.Replace(std::move(valid)));

  auto invalid = Snapshot(8u, 2u);
  invalid->task_revisions.push_back(Task(8u, 3u, "task-1"));
  auto consent = Consent(8u, 3u, mojom::TaskProviderRoute::kManagedService);
  consent->consent_preview->sources.clear();
  consent->consent_preview->source_discovery_enabled = true;
  consent->consent_preview->new_source_cap = kMaxErrandNewSourceCap + 1u;
  invalid->accepted_task_consents.push_back(std::move(consent));
  EXPECT_EQ(mojom::PendingApprovalRegistrationStatus::kInvalidBinding,
            registry.Replace(std::move(invalid)));
  EXPECT_EQ(2u, registry.FindTaskRevision("task-1"));
  EXPECT_EQ(1u, registry.state_sequence());
}

TEST(CoreStateBindingRegistryTest, ApprovalMustMatchCanonicalTaskRevision) {
  CoreStateBindingRegistry registry;
  auto invalid = Snapshot(9u, 1u);
  invalid->task_revisions.push_back(Task(9u, 4u, "task-1"));
  invalid->pending_approvals.push_back(Approval(9u, 3u, "task-1", "action-1"));
  EXPECT_EQ(mojom::PendingApprovalRegistrationStatus::kInvalidBinding,
            registry.Replace(std::move(invalid)));
  EXPECT_EQ(0u, registry.task_count());
}

TEST(CoreStateBindingRegistryTest, PermissionAndSettlementUseExactTaskFacts) {
  CoreStateBindingRegistry registry;
  auto snapshot = Snapshot(12u, 1u);
  snapshot->task_revisions.push_back(Task(12u, 8u, "task-1"));
  snapshot->pending_permissions.push_back(
      Permission(12u, 8u, "task-1", "request-1"));
  snapshot->task_settlements.push_back(Settlement(12u, 8u, "task-1"));
  ASSERT_EQ(mojom::PendingApprovalRegistrationStatus::kRegistered,
            registry.Replace(std::move(snapshot)));
  const auto permission = registry.FindPendingPermission("request-1");
  ASSERT_TRUE(permission);
  EXPECT_EQ("task-1", permission->task_id);
  EXPECT_EQ(mojom::PlatformPermission::kCamera, permission->permission);
  EXPECT_EQ(8u, permission->task_revision);
  EXPECT_EQ(1'800'000'009'000u, permission->deadline_utc_ms);
  EXPECT_EQ("browser-session-1", permission->browser_session_id);
  ASSERT_EQ(1u, registry.task_settlements().size());
  EXPECT_EQ(mojom::TaskSettlementKind::kCancel,
            registry.task_settlements().front().kind);
  const auto settlement = registry.FindTaskSettlement("task-1");
  ASSERT_TRUE(settlement);
  EXPECT_EQ(12u, settlement->service_generation);
  EXPECT_EQ(8u, settlement->task_revision);
  EXPECT_EQ(mojom::TaskSettlementKind::kCancel, settlement->kind);

  registry.Reset();
  EXPECT_FALSE(registry.FindPendingPermission("request-1"));
  EXPECT_FALSE(registry.FindTaskSettlement("task-1"));
  EXPECT_TRUE(registry.task_settlements().empty());
}

TEST(CoreStateBindingRegistryTest, StalePermissionPreservesPriorSnapshot) {
  CoreStateBindingRegistry registry;
  auto first = Snapshot(14u, 1u);
  first->task_revisions.push_back(Task(14u, 3u, "task-1"));
  first->pending_permissions.push_back(
      Permission(14u, 3u, "task-1", "request-1"));
  ASSERT_EQ(mojom::PendingApprovalRegistrationStatus::kRegistered,
            registry.Replace(std::move(first)));

  auto stale = Snapshot(14u, 2u);
  stale->task_revisions.push_back(Task(14u, 4u, "task-1"));
  stale->pending_permissions.push_back(
      Permission(13u, 4u, "task-1", "request-2"));
  EXPECT_EQ(mojom::PendingApprovalRegistrationStatus::kInvalidBinding,
            registry.Replace(std::move(stale)));
  EXPECT_TRUE(registry.FindPendingPermission("request-1"));
  EXPECT_FALSE(registry.FindPendingPermission("request-2"));
}

TEST(CoreStateBindingRegistryTest,
     TerminalTaskIsExactAndCannotRetainPendingAuthority) {
  CoreStateBindingRegistry registry;
  auto terminal = Snapshot(15u, 1u);
  terminal->task_revisions.push_back(Task(15u, 6u, "task-1"));
  terminal->terminal_tasks.push_back(Terminal(15u, 6u, "task-1"));
  ASSERT_EQ(mojom::PendingApprovalRegistrationStatus::kRegistered,
            registry.Replace(std::move(terminal)));
  ASSERT_EQ(registry.terminal_tasks().size(), 1u);
  EXPECT_EQ(registry.terminal_tasks().front().task_id, "task-1");
  const auto found = registry.FindTerminalTask("task-1");
  ASSERT_TRUE(found);
  EXPECT_EQ(found->service_generation, 15u);
  EXPECT_EQ(found->task_revision, 6u);

  auto conflicting = Snapshot(15u, 2u);
  conflicting->task_revisions.push_back(Task(15u, 7u, "task-1"));
  conflicting->terminal_tasks.push_back(Terminal(15u, 7u, "task-1"));
  conflicting->pending_approvals.push_back(
      Approval(15u, 7u, "task-1", "action-1"));
  EXPECT_EQ(mojom::PendingApprovalRegistrationStatus::kInvalidBinding,
            registry.Replace(std::move(conflicting)));
  EXPECT_EQ(registry.terminal_tasks().front().task_revision, 6u);
  EXPECT_FALSE(registry.FindTerminalTask("missing-task"));
}

TEST(CoreStateBindingRegistryTest, TaskControlLookupIsExactAndRestartClearsIt) {
  CoreStateBindingRegistry registry;
  auto snapshot = Snapshot(21u, 4u);
  snapshot->task_revisions.push_back(
      Task(21u, 9u, "task-1",
           {mojom::TaskControlKind::kPause, mojom::TaskControlKind::kTakeOver,
            mojom::TaskControlKind::kStop}));
  ASSERT_EQ(mojom::PendingApprovalRegistrationStatus::kRegistered,
            registry.Replace(std::move(snapshot)));

  const auto pause =
      registry.FindTaskControl("task-1", mojom::TaskControlKind::kPause);
  ASSERT_TRUE(pause);
  EXPECT_EQ(21u, pause->service_generation);
  EXPECT_EQ(9u, pause->task_revision);
  EXPECT_FALSE(
      registry.FindTaskControl("task-1", mojom::TaskControlKind::kResume));

  registry.Reset();
  EXPECT_FALSE(
      registry.FindTaskControl("task-1", mojom::TaskControlKind::kPause));
}

TEST(CoreStateBindingRegistryTest,
     InvalidControlListsPreserveThePriorAtomicSnapshot) {
  CoreStateBindingRegistry registry;
  auto first = Snapshot(22u, 1u);
  first->task_revisions.push_back(
      Task(22u, 3u, "task-1",
           {mojom::TaskControlKind::kResume, mojom::TaskControlKind::kStop}));
  ASSERT_EQ(mojom::PendingApprovalRegistrationStatus::kRegistered,
            registry.Replace(std::move(first)));

  const std::vector<std::vector<mojom::TaskControlKind>> invalid_lists = {
      {mojom::TaskControlKind::kPause, mojom::TaskControlKind::kPause},
      {mojom::TaskControlKind::kStop, mojom::TaskControlKind::kPause},
      {mojom::TaskControlKind::kPause, mojom::TaskControlKind::kResume,
       mojom::TaskControlKind::kTakeOver, mojom::TaskControlKind::kStop,
       mojom::TaskControlKind::kStop},
      {static_cast<mojom::TaskControlKind>(99)},
  };
  uint64_t sequence = 2u;
  for (const auto& controls : invalid_lists) {
    auto invalid = Snapshot(22u, sequence++);
    invalid->task_revisions.push_back(Task(22u, 4u, "task-1", controls));
    EXPECT_EQ(mojom::PendingApprovalRegistrationStatus::kInvalidBinding,
              registry.Replace(std::move(invalid)));
  }

  const auto resume =
      registry.FindTaskControl("task-1", mojom::TaskControlKind::kResume);
  ASSERT_TRUE(resume);
  EXPECT_EQ(3u, resume->task_revision);
  EXPECT_EQ(1u, registry.state_sequence());
}

TEST(CoreStateBindingRegistryTest,
     SettlingAndTerminalBindingsCannotOfferControls) {
  CoreStateBindingRegistry registry;
  auto settling = Snapshot(23u, 1u);
  settling->task_revisions.push_back(
      Task(23u, 5u, "task-1", {mojom::TaskControlKind::kResume}));
  settling->task_settlements.push_back(Settlement(23u, 5u, "task-1"));
  EXPECT_EQ(mojom::PendingApprovalRegistrationStatus::kInvalidBinding,
            registry.Replace(std::move(settling)));

  auto terminal = Snapshot(23u, 2u);
  terminal->task_revisions.push_back(
      Task(23u, 6u, "task-1", {mojom::TaskControlKind::kStop}));
  terminal->terminal_tasks.push_back(Terminal(23u, 6u, "task-1"));
  EXPECT_EQ(mojom::PendingApprovalRegistrationStatus::kInvalidBinding,
            registry.Replace(std::move(terminal)));
}

}  // namespace
}  // namespace taffy
