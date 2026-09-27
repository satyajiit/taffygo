// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_task_effect.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "taffy/browser/core_task_action.h"
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

std::vector<uint8_t> ExactReadIntent(uint8_t operation,
                                     std::optional<std::string_view> node) {
  constexpr std::string_view kPrefix = "taffy.action-intent.v1";
  std::vector<uint8_t> intent(kPrefix.begin(), kPrefix.end());
  const std::array<uint8_t, 1> operation_bytes = {operation};
  CanonicalField(&intent, 0u, operation_bytes);
  CanonicalField(&intent, 1u, base::as_byte_span(std::string_view("tab-1")));
  if (node) {
    CanonicalField(&intent, 2u, base::as_byte_span(*node));
  }
  return intent;
}

std::vector<uint8_t> DomQueryIntent(std::optional<std::string_view> within) {
  constexpr std::string_view kPrefix = "taffy.action-intent.v1";
  std::vector<uint8_t> intent(kPrefix.begin(), kPrefix.end());
  const std::array<uint8_t, 1> operation = {8u};
  CanonicalField(&intent, 0u, operation);
  CanonicalField(&intent, 1u, base::as_byte_span(std::string_view("tab-1")));
  std::vector<uint8_t> optional_within = {static_cast<uint8_t>(within ? 1 : 0)};
  if (within) {
    optional_within.insert(optional_within.end(), within->begin(),
                           within->end());
  }
  CanonicalField(&intent, 2u, optional_within);
  const std::array<uint8_t, 1> absent = {0u};
  CanonicalField(&intent, 3u, absent);
  CanonicalField(&intent, 4u, absent);
  CanonicalField(&intent, 5u, absent);
  return intent;
}

std::vector<uint8_t> DomActivationIntent(std::optional<bool> expected_expanded) {
  constexpr std::string_view kPrefix = "taffy.action-intent.v1";
  std::vector<uint8_t> intent(kPrefix.begin(), kPrefix.end());
  const std::array<uint8_t, 1> operation = {10u};
  CanonicalField(&intent, 0u, operation);
  CanonicalField(&intent, 1u, base::as_byte_span(std::string_view("tab-1")));
  CanonicalField(&intent, 2u, base::as_byte_span(std::string_view("frame-1")));
  CanonicalField(&intent, 3u, base::as_byte_span(std::string_view("epoch-1")));
  const std::array<uint8_t, 8> revision = {7u};
  CanonicalField(&intent, 4u, revision);
  CanonicalField(&intent, 5u, base::as_byte_span(std::string_view("node-1")));
  const std::array<uint8_t, 1> tuple_origin = {0u};
  CanonicalField(&intent, 6u, tuple_origin);
  CanonicalField(&intent, 7u,
                 base::as_byte_span(std::string_view("https://example.test")));
  // 0 expanded, 1 collapsed, 2 no disclosure claim — an ordinary press.
  const std::array<uint8_t, 1> state = {
      expected_expanded ? static_cast<uint8_t>(*expected_expanded ? 0u : 1u)
                        : uint8_t{2u}};
  CanonicalField(&intent, 8u, state);
  return intent;
}

mojom::TaskEffectBindingPtr ObservationBinding() {
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
  binding->action->executable->operation_kind =
      mojom::TaskActionOperationKind::kDomRead;
  binding->action->executable->tool_name = "browser.dom.read";
  binding->action->executable->canonical_intent = {0x01u, 0x02u, 0x03u};
  binding->action->executable->input = mojom::TaskActionInput::New();
  binding->action->executable->input->kind = mojom::TaskActionInputKind::kNone;
  binding->action->executable->tab_id = "tab-1";
  binding->action->preconditions = {
      mojom::TaskActionPrecondition::kDocumentUnchanged,
      mojom::TaskActionPrecondition::kGraphRevisionAtLeast,
  };
  binding->action->postcondition =
      mojom::TaskActionPostcondition::kObservationCaptured;
  binding->action->observation = mojom::TaskObservationBounds::New(
      mojom::ObservationScope::kCurrentDocument,
      mojom::kMaxTaskObservationTotalBytes, mojom::kMaxTaskObservationNodes,
      mojom::kMaxTaskObservationTextBytes, mojom::kMaxTaskObservationFrames,
      mojom::kMaxTaskObservationDeadlineMs);
  return binding;
}

mojom::TaskEffectBindingPtr ClickBinding() {
  auto binding = ObservationBinding();
  binding->action->executable->action_class =
      mojom::PolicyActionClass::kSyntheticClick;
  binding->action->executable->operation_kind =
      mojom::TaskActionOperationKind::kDomClick;
  binding->action->executable->tool_name = "browser.dom.click";
  binding->action->executable->canonical_intent = DomActivationIntent(true);
  binding->action->executable->node_id = "node-1";
  binding->action->preconditions = {
      mojom::TaskActionPrecondition::kDocumentUnchanged,
      mojom::TaskActionPrecondition::kGraphRevisionAtLeast,
      mojom::TaskActionPrecondition::kNodePresent,
  };
  binding->action->postcondition =
      mojom::TaskActionPostcondition::kNodeStateChanged;
  binding->action->observation.reset();
  return binding;
}

mojom::TaskEffectBindingPtr FormInspectBinding() {
  auto binding = ObservationBinding();
  binding->action->executable->operation_kind =
      mojom::TaskActionOperationKind::kFormInspect;
  binding->action->executable->tool_name = "browser.form.inspect";
  binding->action->executable->node_id = "form-1";
  binding->action->executable->canonical_intent =
      ExactReadIntent(12u, std::string_view("form-1"));
  binding->action->preconditions.push_back(
      mojom::TaskActionPrecondition::kNodePresent);
  return binding;
}

mojom::TaskEffectBindingPtr SelectionReadBinding() {
  auto binding = ObservationBinding();
  binding->action->executable->operation_kind =
      mojom::TaskActionOperationKind::kSelectionRead;
  binding->action->executable->tool_name = "browser.selection.read";
  binding->action->executable->canonical_intent =
      ExactReadIntent(17u, std::nullopt);
  return binding;
}

mojom::TaskEffectBindingPtr ScrollBinding() {
  auto binding = ClickBinding();
  binding->action->executable->action_class =
      mojom::PolicyActionClass::kScrollIntoView;
  binding->action->executable->operation_kind =
      mojom::TaskActionOperationKind::kDomScroll;
  binding->action->executable->tool_name = "browser.dom.scroll";
  return binding;
}

mojom::TaskEffectBindingPtr NavigateBinding() {
  auto binding = ObservationBinding();
  binding->action->executable->action_class =
      mojom::PolicyActionClass::kOpenLink;
  binding->action->executable->operation_kind =
      mojom::TaskActionOperationKind::kNavigate;
  binding->action->executable->tool_name = "browser.navigate";
  binding->action->executable->destination_origin = "https://example.test";
  binding->action->executable->destination_address =
      "https://example.test/next";
  binding->action->preconditions = {
      mojom::TaskActionPrecondition::kDocumentUnchanged,
      mojom::TaskActionPrecondition::kGraphRevisionAtLeast,
      mojom::TaskActionPrecondition::kDestinationUnchanged,
  };
  binding->action->postcondition =
      mojom::TaskActionPostcondition::kDocumentNavigated;
  binding->action->observation.reset();
  return binding;
}

mojom::TaskEffectBindingPtr TabControlBinding(
    mojom::TaskActionOperationKind operation,
    std::string_view tool_name,
    uint8_t canonical_tag,
    mojom::TaskActionPostcondition postcondition) {
  auto binding = ObservationBinding();
  binding->action->executable->action_class =
      mojom::PolicyActionClass::kControlTab;
  binding->action->executable->operation_kind = operation;
  binding->action->executable->tool_name = tool_name;
  binding->action->executable->canonical_intent =
      ExactReadIntent(canonical_tag, std::nullopt);
  binding->action->postcondition = postcondition;
  binding->action->observation.reset();
  return binding;
}

TEST(CoreTaskEffectTest, ExactReadOnlyActionBindingIsAccepted) {
  const auto binding = ObservationBinding();
  EXPECT_TRUE(IsStructurallyValidTaskEffectBinding(*binding, kGeneration,
                                                   kRevision, kNow));
}

TEST(CoreTaskEffectTest, ExactFormAndSelectionReadsKeepTheirTargetShape) {
  EXPECT_TRUE(IsStructurallyValidTaskEffectBinding(
      *FormInspectBinding(), kGeneration, kRevision, kNow));
  EXPECT_TRUE(IsStructurallyValidTaskEffectBinding(
      *SelectionReadBinding(), kGeneration, kRevision, kNow));

  auto wrong_form = FormInspectBinding();
  wrong_form->action->executable->node_id = "form-2";
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(*wrong_form, kGeneration,
                                                    kRevision, kNow));

  auto widened_selection = SelectionReadBinding();
  widened_selection->action->executable->node_id = "invented-node";
  widened_selection->action->preconditions.push_back(
      mojom::TaskActionPrecondition::kNodePresent);
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(
      *widened_selection, kGeneration, kRevision, kNow));
}

TEST(CoreTaskEffectTest, ExactScrollAndTypedDisclosureActivationAreAccepted) {
  EXPECT_TRUE(IsStructurallyValidTaskEffectBinding(
      *ScrollBinding(), kGeneration, kRevision, kNow));
  EXPECT_TRUE(IsStructurallyValidTaskEffectBinding(*ClickBinding(), kGeneration,
                                                   kRevision, kNow));
  auto generic = ClickBinding();
  generic->action->executable->canonical_intent.pop_back();
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(*generic, kGeneration,
                                                    kRevision, kNow));
}

TEST(CoreTaskEffectTest, AScrollWithoutANodeIsRefused) {
  auto missing_node = ScrollBinding();
  missing_node->action->executable->node_id.reset();
  missing_node->action->preconditions = {
      mojom::TaskActionPrecondition::kDocumentUnchanged,
      mojom::TaskActionPrecondition::kGraphRevisionAtLeast,
  };
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(*missing_node, kGeneration,
                                                    kRevision, kNow));
}

TEST(CoreTaskEffectTest, ExactNavigateBindingIsAccepted) {
  EXPECT_TRUE(IsStructurallyValidTaskEffectBinding(
      *NavigateBinding(), kGeneration, kRevision, kNow));
}

TEST(CoreTaskEffectTest, TabControlsAreExactAndDestinationFree) {
  auto back = TabControlBinding(
      mojom::TaskActionOperationKind::kHistoryBack, "browser.back", 2u,
      mojom::TaskActionPostcondition::kDocumentNavigated);
  auto reload = TabControlBinding(
      mojom::TaskActionOperationKind::kReload, "browser.reload", 28u,
      mojom::TaskActionPostcondition::kPageReloaded);
  auto stop = TabControlBinding(
      mojom::TaskActionOperationKind::kStopLoading, "browser.stop_loading", 29u,
      mojom::TaskActionPostcondition::kLoadingStopped);
  EXPECT_TRUE(IsStructurallyValidTaskEffectBinding(*back, kGeneration,
                                                   kRevision, kNow));
  EXPECT_TRUE(IsStructurallyValidTaskEffectBinding(*reload, kGeneration,
                                                   kRevision, kNow));
  EXPECT_TRUE(IsStructurallyValidTaskEffectBinding(*stop, kGeneration,
                                                   kRevision, kNow));

  reload->action->executable->destination_address =
      "https://example.test/invented";
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(*reload, kGeneration,
                                                    kRevision, kNow));
  stop->action->postcondition =
      mojom::TaskActionPostcondition::kPlatformAcknowledged;
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(*stop, kGeneration,
                                                    kRevision, kNow));
}

TEST(CoreTaskEffectTest, ACrossOriginNavigateAddressIsRefused) {
  auto cross_origin = NavigateBinding();
  cross_origin->action->executable->destination_address =
      "https://other.test/next";
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(*cross_origin, kGeneration,
                                                    kRevision, kNow));
}

TEST(CoreTaskEffectTest, AScrollCarryingADestinationAddressIsRefused) {
  auto scroll = ScrollBinding();
  scroll->action->executable->destination_address = "https://example.test/next";
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(*scroll, kGeneration,
                                                    kRevision, kNow));
}

TEST(CoreTaskEffectTest, OperationAndCanonicalIntentAreExact) {
  auto wrong_operation = ScrollBinding();
  wrong_operation->action->executable->operation_kind =
      mojom::TaskActionOperationKind::kDomRead;
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(
      *wrong_operation, kGeneration, kRevision, kNow));

  auto missing_intent = ObservationBinding();
  missing_intent->action->executable->canonical_intent.clear();
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(
      *missing_intent, kGeneration, kRevision, kNow));
}

TEST(CoreTaskEffectTest,
     FilteredQueryCapturesTheDocumentWithoutExecutableNodeAuthority) {
  auto query = ObservationBinding();
  query->action->executable->operation_kind =
      mojom::TaskActionOperationKind::kDomQuery;
  query->action->executable->tool_name = "browser.dom.query";
  query->action->executable->canonical_intent =
      DomQueryIntent(std::string_view("node-1"));
  EXPECT_TRUE(IsStructurallyValidTaskEffectBinding(*query, kGeneration,
                                                   kRevision, kNow));

  query->action->executable->node_id = "node-1";
  query->action->preconditions.push_back(
      mojom::TaskActionPrecondition::kNodePresent);
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(*query, kGeneration,
                                                    kRevision, kNow));

  query = ObservationBinding();
  query->action->executable->operation_kind =
      mojom::TaskActionOperationKind::kDomQuery;
  query->action->executable->tool_name = "browser.dom.query";
  query->action->executable->canonical_intent = DomQueryIntent(std::nullopt);
  query->action->executable->transient_search_query = "query bytes";
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(*query, kGeneration,
                                                    kRevision, kNow));
}

TEST(CoreTaskEffectTest, CorePostconditionNarrowsToExactBrowserEvidence) {
  EXPECT_EQ(PostconditionKind::kSectionVisible,
            PagePostconditionForTaskAction(
                mojom::PolicyActionClass::kScrollIntoView,
                mojom::TaskActionPostcondition::kNodeStateChanged));
  EXPECT_EQ(PostconditionKind::kNodeValueChanged,
            PagePostconditionForTaskAction(
                mojom::PolicyActionClass::kFillField,
                mojom::TaskActionPostcondition::kNodeStateChanged));
  EXPECT_EQ(PostconditionKind::kNodeValueChanged,
            PagePostconditionForTaskAction(
                mojom::PolicyActionClass::kSelectOption,
                mojom::TaskActionPostcondition::kNodeStateChanged));
  EXPECT_EQ(PostconditionKind::kNodeStateChanged,
            PagePostconditionForTaskAction(
                mojom::PolicyActionClass::kToggleControl,
                mojom::TaskActionPostcondition::kNodeStateChanged));
  EXPECT_EQ(PostconditionKind::kCommittedNavigation,
            PagePostconditionForTaskAction(
                mojom::PolicyActionClass::kSubmitForm,
                mojom::TaskActionPostcondition::kNodeStateChanged));
  EXPECT_FALSE(PagePostconditionForTaskAction(
      mojom::PolicyActionClass::kScrollIntoView,
      mojom::TaskActionPostcondition::kObservationCaptured));
  for (const auto action_class : {mojom::PolicyActionClass::kSyntheticClick,
                                  mojom::PolicyActionClass::kMoveFocus}) {
    EXPECT_EQ(
        PostconditionKind::kNodeStateChanged,
        PagePostconditionForTaskAction(
            action_class, mojom::TaskActionPostcondition::kNodeStateChanged));
  }
}

TEST(CoreTaskEffectTest, TagSmugglingAndStaleFactsFailClosed) {
  auto smuggled = ObservationBinding();
  smuggled->approval =
      mojom::TaskApprovalEffect::New("action-1", std::string(64u, 'a'),
                                     nullptr);
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(*smuggled, kGeneration,
                                                    kRevision, kNow));

  auto stale = ObservationBinding();
  stale->operation->task_revision = kRevision - 1u;
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(*stale, kGeneration,
                                                    kRevision, kNow));
}

TEST(CoreTaskEffectTest, ObservationBoundsAndPreconditionOrderAreExact) {
  auto widened = ObservationBinding();
  ++widened->action->observation->max_nodes;
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(*widened, kGeneration,
                                                    kRevision, kNow));

  auto reordered = ObservationBinding();
  std::swap(reordered->action->preconditions[0],
            reordered->action->preconditions[1]);
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(*reordered, kGeneration,
                                                    kRevision, kNow));
}

TEST(CoreTaskEffectTest, CompletionCopiesCorrelationWithoutInventingResult) {
  const auto binding = ObservationBinding();
  auto completion = MakeTaskEffectCompletion(
      binding.get(), mojom::TaskEffectCompletionStatus::kUnavailable);
  ASSERT_TRUE(completion->operation);
  EXPECT_EQ(completion->operation->operation_id,
            binding->operation->operation_id);
  EXPECT_EQ(completion->effect_id, binding->effect_id);
  EXPECT_EQ(completion->task_id, binding->task_id);
  EXPECT_EQ(completion->kind, binding->kind);
  EXPECT_EQ(completion->status,
            mojom::TaskEffectCompletionStatus::kUnavailable);
  EXPECT_FALSE(completion->effect_result);
}

}  // namespace
}  // namespace taffy
