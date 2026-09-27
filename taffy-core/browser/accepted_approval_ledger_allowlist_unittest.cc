// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/accepted_approval_ledger.h"

#include <optional>
#include <string>
#include <utility>

#include "base/strings/string_number_conversions.h"
#include "taffy/browser/core_state_binding_registry.h"
#include "testing/gtest/include/gtest/gtest.h"

// One rule, in its own file: what a submitted tool allowlist must look like.
//
// It sits apart from the rest of the ledger's tests because it is the one
// clause of this gate that stopped naming tools (decision 0057). Everything
// else there is about a command matching the consent behind it; this is about
// the list being a list at all, and the two are worth being able to read
// separately — not least because the wrong reading of this gate, that it is
// where the tool ceiling lives, is the thing the record exists to correct.
//
// The fixture below is deliberately its own rather than shared. It is the
// smallest command this gate accepts, and a test file about refusals should be
// able to say what it started from without a reader following a helper into
// another file to find out.

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr uint64_t kGeneration = 3u;
constexpr uint64_t kNow = 10'000u;
constexpr uint64_t kNowUtc = 1'800'000'000'000u;
constexpr char kProfileId[] = "profile-1";
constexpr char kBrowserSessionId[] = "browser-session-1";
constexpr char kTaskId[] = "task-1";
constexpr char kTabId[] = "tab-1";
constexpr char kOrigin[] = "https://example.test";

// A core with no task bindings yet, which is what a brand-new task is
// submitted against. `state_sequence` is not incidental: the registry refuses a
// state that does not advance it, so a zero here registers nothing and every
// test below would fail for a reason that has nothing to do with allowlists.
void RegisterEmptyState(CoreStateBindingRegistry* registry) {
  auto state = mojom::CoreStateBrowserBindings::New();
  state->service_generation = kGeneration;
  state->state_sequence = 1u;
  ASSERT_EQ(registry->Replace(std::move(state)),
            mojom::PendingApprovalRegistrationStatus::kRegistered);
}

mojom::CoreServiceCommandPtr StartTask() {
  auto command = mojom::CoreServiceCommand::New();
  command->operation = mojom::OperationEnvelope::New(
      "start-operation", kGeneration, 0u, 25'000u, "start-idempotency");
  command->kind = mojom::CoreServiceCommandKind::kStartTask;
  command->start_task = mojom::StartTaskCommand::New();
  command->start_task->task_id = kTaskId;
  command->start_task->browser_profile_id = kProfileId;
  command->start_task->browser_session_id = kBrowserSessionId;
  command->start_task->template_id = mojom::TaskTemplateId::kBuildSourceTable;
  command->start_task->provider_route_id = "no_model_required";
  command->start_task->tool_allowlist = {"browser.dom.read"};
  command->start_task->initial_consent_receipt_id = "initial-receipt-1";
  command->start_task->consent_preview = mojom::TaskConsentPreview::New();
  command->start_task->consent_preview->sources.push_back(
      mojom::TaskConsentSource::New("source-1", kTabId, kOrigin, std::nullopt));
  command->start_task->consent_preview->provider_route =
      mojom::TaskProviderRoute::kNoModelRequired;
  return command;
}

AuthoritySubmissionStage Stage(AcceptedApprovalLedger* ledger,
                               const mojom::CoreServiceCommand& command) {
  CoreStateBindingRegistry registry;
  RegisterEmptyState(&registry);
  return ledger->StageSubmittedCommand(command, kProfileId, kBrowserSessionId,
                                       registry, kNow, kNowUtc);
}

// The tool allowlist, which this gate checks structurally and never against a
// table of names (decision 0057). Three shapes are refused and one wider-but-
// well-formed list is accepted, because the existing fixture constructs the one
// narrow shape and would stay green through any widening at all — so without
// these it proves nothing about the rule.
TEST(AcceptedApprovalLedgerTest, AnEmptyToolAllowlistIsRefused) {
  // The single most consequential case. An empty allowlist is read downstream
  // as *everything the milestone has reached*, so admitting one here would
  // open a wider task than any explicit list could ask for — and it would look
  // like a working gate, because a gate that accepts everything accepts every
  // well-formed command.
  AcceptedApprovalLedger ledger;
  auto command = StartTask();
  command->start_task->tool_allowlist.clear();

  EXPECT_EQ(Stage(&ledger, *command), AuthoritySubmissionStage::kInvalid);
  EXPECT_EQ(ledger.staged_consent_count_for_testing(), 0u);
}

TEST(AcceptedApprovalLedgerTest, ARepeatedToolNameIsRefused) {
  AcceptedApprovalLedger ledger;
  auto command = StartTask();
  command->start_task->tool_allowlist = {"browser.dom.read",
                                         "browser.dom.read"};

  EXPECT_EQ(Stage(&ledger, *command), AuthoritySubmissionStage::kInvalid);
}

TEST(AcceptedApprovalLedgerTest, AnUnboundedToolAllowlistIsRefused) {
  AcceptedApprovalLedger ledger;
  auto command = StartTask();
  command->start_task->tool_allowlist.clear();
  for (uint64_t index = 0; index <= mojom::kMaxToolAllowlistEntries; ++index) {
    command->start_task->tool_allowlist.push_back(
        "browser.dom.read." + base::NumberToString(index));
  }

  EXPECT_EQ(Stage(&ledger, *command), AuthoritySubmissionStage::kInvalid);
}

TEST(AcceptedApprovalLedgerTest, AWiderWellFormedToolAllowlistIsAccepted) {
  // The positive half, and the reason this gate stopped naming tools: which
  // names exist is decided in the sandboxed core and in the compiled-in
  // action-class join, both of which still refuse anything this build has not
  // reached. This gate's answer is only that the list is a list.
  AcceptedApprovalLedger ledger;
  auto command = StartTask();
  command->start_task->tool_allowlist = {"browser.dom.read",
                                         "browser.dom.query"};

  EXPECT_EQ(Stage(&ledger, *command), AuthoritySubmissionStage::kStaged);
}

}  // namespace
}  // namespace taffy
