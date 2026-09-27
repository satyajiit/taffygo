// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "taffy/browser/core_task_effect.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr uint64_t kGeneration = 5u;
constexpr uint64_t kRevision = 9u;
constexpr uint64_t kNow = 10'000u;

void CanonicalField(std::vector<uint8_t>* out,
                    uint8_t tag,
                    base::span<const uint8_t> value) {
  out->push_back(tag);
  for (size_t index = 0; index < sizeof(uint64_t); ++index) {
    out->push_back(static_cast<uint8_t>(value.size() >> (index * 8u)));
  }
  out->insert(out->end(), value.begin(), value.end());
}

std::vector<uint8_t> TaskTabIntent(uint8_t operation) {
  constexpr std::string_view kPrefix = "taffy.action-intent.v1";
  std::vector<uint8_t> intent(kPrefix.begin(), kPrefix.end());
  const std::array<uint8_t, 1> operation_bytes = {operation};
  CanonicalField(&intent, 0u, operation_bytes);
  CanonicalField(&intent, 1u, base::as_byte_span(std::string_view("tab-1")));
  CanonicalField(&intent, 2u,
                 base::as_byte_span(std::string_view("browser-session-1")));
  if (operation == 6u || operation == 7u) {
    CanonicalField(&intent, 3u,
                   base::as_byte_span(std::string_view("owned-tab")));
    CanonicalField(&intent, 4u,
                   base::as_byte_span(std::string_view("owned-frame")));
    CanonicalField(&intent, 5u,
                   base::as_byte_span(std::string_view("owned-epoch")));
    const uint64_t revision = 9u;
    std::array<uint8_t, sizeof(revision)> bytes = {};
    for (size_t index = 0; index < bytes.size(); ++index) {
      bytes[index] = static_cast<uint8_t>(revision >> (index * 8u));
    }
    CanonicalField(&intent, 6u, bytes);
  }
  return intent;
}

mojom::TaskEffectBindingPtr TaskTabBinding(
    mojom::TaskActionOperationKind operation) {
  auto binding = mojom::TaskEffectBinding::New();
  binding->operation = mojom::OperationEnvelope::New(
      "task-effect-operation", kGeneration, kRevision, 13'000u,
      "task-action-idempotency");
  binding->effect_id = "task-effect-1";
  binding->task_id = "task-1";
  binding->ordinal = 0u;
  binding->kind = mojom::TaskReducerEffectKind::kDispatchAction;
  binding->action = mojom::TaskActionEffect::New();
  binding->action->action_id = "action-1";
  binding->action->proposal_digest = std::string(64u, 'a');
  binding->action->idempotency_key = "task-action-idempotency";
  binding->action->capability_id = "capability-1";
  binding->action->dispatch_id = "dispatch-1";
  binding->action->document = mojom::TaskFrozenDocument::New(
      "frame-1", "epoch-1", 7u, "https://example.test", std::nullopt);
  binding->action->executable = mojom::TaskExecutableAction::New();
  binding->action->executable->action_class =
      mojom::PolicyActionClass::kObservePage;
  binding->action->executable->operation_kind = operation;
  binding->action->executable->input = mojom::TaskActionInput::New();
  binding->action->executable->input->kind = mojom::TaskActionInputKind::kNone;
  binding->action->executable->tab_id = "tab-1";
  binding->action->executable->task_tab = mojom::TaskTabActionBinding::New();
  binding->action->executable->task_tab->browser_session_id =
      "browser-session-1";
  binding->action->preconditions = {
      mojom::TaskActionPrecondition::kDocumentUnchanged,
      mojom::TaskActionPrecondition::kGraphRevisionAtLeast,
  };
  switch (operation) {
    case mojom::TaskActionOperationKind::kTabsList:
      binding->action->executable->tool_name = "browser.tabs.list";
      binding->action->executable->canonical_intent = TaskTabIntent(5u);
      binding->action->postcondition =
          mojom::TaskActionPostcondition::kTaskTabsListed;
      break;
    case mojom::TaskActionOperationKind::kTabsActivate:
      binding->action->executable->action_class =
          mojom::PolicyActionClass::kMoveFocus;
      binding->action->executable->tool_name = "browser.tabs.activate";
      binding->action->executable->canonical_intent = TaskTabIntent(6u);
      binding->action->executable->task_tab->target =
          mojom::TaskTabDocumentTarget::New("owned-tab", "owned-frame",
                                            "owned-epoch", 9u);
      binding->action->postcondition =
          mojom::TaskActionPostcondition::kTaskTabActive;
      break;
    case mojom::TaskActionOperationKind::kTabsClose:
      binding->action->executable->action_class =
          mojom::PolicyActionClass::kCreateTaskTab;
      binding->action->executable->tool_name = "browser.tabs.close";
      binding->action->executable->canonical_intent = TaskTabIntent(7u);
      binding->action->executable->task_tab->target =
          mojom::TaskTabDocumentTarget::New("owned-tab", "owned-frame",
                                            "owned-epoch", 9u);
      binding->action->postcondition =
          mojom::TaskActionPostcondition::kTaskTabAbsent;
      break;
    default:
      break;
  }
  return binding;
}

TEST(CoreTaskEffectTabTest, BindingsAreExactAndOperationSpecific) {
  EXPECT_TRUE(IsStructurallyValidTaskEffectBinding(
      *TaskTabBinding(mojom::TaskActionOperationKind::kTabsList), kGeneration,
      kRevision, kNow));
  EXPECT_TRUE(IsStructurallyValidTaskEffectBinding(
      *TaskTabBinding(mojom::TaskActionOperationKind::kTabsActivate),
      kGeneration, kRevision, kNow));
  EXPECT_TRUE(IsStructurallyValidTaskEffectBinding(
      *TaskTabBinding(mojom::TaskActionOperationKind::kTabsClose), kGeneration,
      kRevision, kNow));

  auto changed_target =
      TaskTabBinding(mojom::TaskActionOperationKind::kTabsActivate);
  changed_target->action->executable->task_tab->target->graph_revision = 10u;
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(
      *changed_target, kGeneration, kRevision, kNow));

  auto changed_session =
      TaskTabBinding(mojom::TaskActionOperationKind::kTabsClose);
  changed_session->action->executable->task_tab->browser_session_id =
      "browser-session-2";
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(
      *changed_session, kGeneration, kRevision, kNow));

  auto listed_target =
      TaskTabBinding(mojom::TaskActionOperationKind::kTabsList);
  listed_target->action->executable->task_tab->target =
      mojom::TaskTabDocumentTarget::New("owned-tab", "owned-frame",
                                        "owned-epoch", 9u);
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(*listed_target, kGeneration,
                                                    kRevision, kNow));
}

}  // namespace
}  // namespace taffy
