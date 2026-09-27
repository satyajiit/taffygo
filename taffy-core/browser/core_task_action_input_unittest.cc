// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "taffy/browser/core_task_action.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

void Field(std::vector<uint8_t>* out,
           uint8_t tag,
           base::span<const uint8_t> value) {
  out->push_back(tag);
  const uint64_t length = value.size();
  for (size_t index = 0; index < sizeof(length); ++index) {
    out->push_back(static_cast<uint8_t>(length >> (index * 8u)));
  }
  out->insert(out->end(), value.begin(), value.end());
}

void TextField(std::vector<uint8_t>* out, uint8_t tag, std::string_view text) {
  Field(out, tag, base::as_byte_span(text));
}

std::vector<uint8_t> FormIntent(uint8_t operation,
                                std::optional<uint32_t> value_position,
                                std::optional<bool> checked) {
  constexpr std::string_view kPrefix = "taffy.action-intent.v1";
  std::vector<uint8_t> intent(kPrefix.begin(), kPrefix.end());
  const std::array<uint8_t, 1> operation_bytes = {operation};
  Field(&intent, 0u, operation_bytes);
  TextField(&intent, 1u, "tab-1");
  TextField(&intent, 2u, operation == 14u ? "submit-1" : "field-1");
  if (value_position) {
    TextField(&intent, 3u, "turn-4-values-1");
    std::array<uint8_t, sizeof(*value_position)> bytes = {};
    for (size_t index = 0; index < bytes.size(); ++index) {
      bytes[index] = static_cast<uint8_t>(*value_position >> (index * 8u));
    }
    Field(&intent, 4u, bytes);
  } else if (checked) {
    const std::array<uint8_t, 1> bytes = {static_cast<uint8_t>(*checked)};
    Field(&intent, 3u, bytes);
  }
  return intent;
}

std::vector<uint8_t> ObservedNodeIntent(uint8_t operation_tag,
                                        std::optional<uint8_t> state) {
  constexpr std::string_view kPrefix = "taffy.action-intent.v1";
  std::vector<uint8_t> intent(kPrefix.begin(), kPrefix.end());
  const std::array<uint8_t, 1> operation = {operation_tag};
  Field(&intent, 0u, operation);
  TextField(&intent, 1u, "tab-1");
  TextField(&intent, 2u, "frame-1");
  TextField(&intent, 3u, "epoch-1");
  const std::array<uint8_t, 8> revision = {7u};
  Field(&intent, 4u, revision);
  TextField(&intent, 5u, "node-1");
  const std::array<uint8_t, 1> tuple_origin = {0u};
  Field(&intent, 6u, tuple_origin);
  TextField(&intent, 7u, "https://example.test");
  if (state) {
    const std::array<uint8_t, 1> expected_state = {*state};
    Field(&intent, 8u, expected_state);
  }
  return intent;
}

std::vector<uint8_t> DomActivationIntent(uint8_t state) {
  return ObservedNodeIntent(10u, state);
}

std::vector<uint8_t> DomFocusIntent() {
  return ObservedNodeIntent(25u, std::nullopt);
}

std::vector<uint8_t> TabControlIntent(uint8_t operation) {
  constexpr std::string_view kPrefix = "taffy.action-intent.v1";
  std::vector<uint8_t> intent(kPrefix.begin(), kPrefix.end());
  const std::array<uint8_t, 1> operation_bytes = {operation};
  Field(&intent, 0u, operation_bytes);
  TextField(&intent, 1u, "tab-1");
  return intent;
}

void U64Field(std::vector<uint8_t>* out, uint8_t tag, uint64_t value) {
  std::array<uint8_t, sizeof(value)> bytes = {};
  for (size_t index = 0; index < bytes.size(); ++index) {
    bytes[index] = static_cast<uint8_t>(value >> (index * 8u));
  }
  Field(out, tag, bytes);
}

void OpaqueField(std::vector<uint8_t>* out, uint8_t tag, uint8_t kind) {
  std::vector<uint8_t> opaque;
  TextField(&opaque, 0u,
            kind == 3u ? "turn-7-call-0-memory-query"
                       : "turn-7-call-0-memory-statement");
  const std::array<uint8_t, 1> kind_bytes = {kind};
  Field(&opaque, 1u, kind_bytes);
  const std::array<uint8_t, 32> digest = {0x5au};
  Field(&opaque, 2u, digest);
  Field(out, tag, opaque);
}

std::vector<uint8_t> MemoryIntent(uint8_t operation) {
  constexpr std::string_view kPrefix = "taffy.action-intent.v1";
  std::vector<uint8_t> intent(kPrefix.begin(), kPrefix.end());
  const std::array<uint8_t, 1> family = {253u};
  const std::array<uint8_t, 1> operation_bytes = {operation};
  Field(&intent, 0u, family);
  Field(&intent, 1u, operation_bytes);
  TextField(&intent, 2u, "tab-1");
  if (operation == 0u) {
    OpaqueField(&intent, 3u, 3u);
    const std::array<uint8_t, 4> limit = {4u, 0u, 0u, 0u};
    Field(&intent, 4u, limit);
  } else if (operation == 1u) {
    OpaqueField(&intent, 3u, 4u);
    const std::array<uint8_t, 1> scope = {1u};
    Field(&intent, 4u, scope);
    std::vector<uint8_t> workspace = {1u};
    constexpr std::string_view kWorkspace = "workspace-1";
    workspace.insert(workspace.end(), kWorkspace.begin(), kWorkspace.end());
    Field(&intent, 5u, workspace);
    const std::array<uint8_t, 1> no_expiry = {0u};
    Field(&intent, 6u, no_expiry);
  } else if (operation == 2u) {
    TextField(&intent, 3u, "memory-1");
    U64Field(&intent, 4u, 7u);
    OpaqueField(&intent, 5u, 4u);
    const std::array<uint8_t, 1> scope = {0u};
    const std::array<uint8_t, 1> no_workspace = {0u};
    const std::array<uint8_t, 1> no_expiry = {0u};
    Field(&intent, 6u, scope);
    Field(&intent, 7u, no_workspace);
    Field(&intent, 8u, no_expiry);
  } else {
    TextField(&intent, 3u, "memory-1");
    U64Field(&intent, 4u, 7u);
  }
  return intent;
}

mojom::TaskActionInputPtr NoneInput() {
  auto input = mojom::TaskActionInput::New();
  input->kind = mojom::TaskActionInputKind::kNone;
  return input;
}

mojom::TaskActionInputPtr SuppliedInput(uint32_t position) {
  auto input = mojom::TaskActionInput::New();
  input->kind = mojom::TaskActionInputKind::kSuppliedValue;
  input->supplied_value = mojom::TaskSuppliedValuePosition::New();
  input->supplied_value->index = position;
  input->supplied_value->request_id = "turn-4-values-1";
  return input;
}

mojom::TaskActionInputPtr ToggleInput(bool checked) {
  auto input = mojom::TaskActionInput::New();
  input->kind = mojom::TaskActionInputKind::kToggleState;
  input->toggle_state = mojom::TaskToggleState::New();
  input->toggle_state->checked = checked;
  return input;
}

mojom::TaskActionEffectPtr ActionWithInput(
    mojom::TaskActionInputPtr input,
    mojom::PolicyActionClass action_class) {
  auto action = mojom::TaskActionEffect::New();
  action->executable = mojom::TaskExecutableAction::New();
  action->executable->input = std::move(input);
  action->executable->action_class = action_class;
  return action;
}

TEST(CoreTaskActionInputTest, SuppliedValuesBindExactOperationNodeAndPosition) {
  auto fill = SuppliedInput(2u);
  EXPECT_TRUE(TaskActionInputMatchesOperationAndCanonical(
      fill.get(), mojom::TaskActionOperationKind::kFormFill,
      FormIntent(13u, 2u, std::nullopt), "tab-1", "field-1"));
  EXPECT_FALSE(TaskActionInputMatchesOperationAndCanonical(
      fill.get(), mojom::TaskActionOperationKind::kFormFill,
      FormIntent(13u, 1u, std::nullopt), "tab-1", "field-1"));
  EXPECT_FALSE(TaskActionInputMatchesOperationAndCanonical(
      fill.get(), mojom::TaskActionOperationKind::kFormFill,
      FormIntent(13u, 2u, std::nullopt), "tab-1", "field-2"));
  EXPECT_FALSE(TaskActionInputMatchesOperationAndCanonical(
      fill.get(), mojom::TaskActionOperationKind::kFormSelect,
      FormIntent(13u, 2u, std::nullopt), "tab-1", "field-1"));
  fill->supplied_value->request_id = "turn-5-values-1";
  EXPECT_FALSE(TaskActionInputMatchesOperationAndCanonical(
      fill.get(), mojom::TaskActionOperationKind::kFormFill,
      FormIntent(13u, 2u, std::nullopt), "tab-1", "field-1"));

  auto select = SuppliedInput(1u);
  EXPECT_TRUE(TaskActionInputMatchesOperationAndCanonical(
      select.get(), mojom::TaskActionOperationKind::kFormSelect,
      FormIntent(23u, 1u, std::nullopt), "tab-1", "field-1"));
  select->supplied_value->index = mojom::kMaxTaskSuppliedValues;
  EXPECT_FALSE(TaskActionInputMatchesOperationAndCanonical(
      select.get(), mojom::TaskActionOperationKind::kFormSelect,
      FormIntent(23u, mojom::kMaxTaskSuppliedValues, std::nullopt), "tab-1",
      "field-1"));
}

TEST(CoreTaskActionInputTest, ToggleAndSubmitRejectEveryMismatchedShape) {
  auto toggle = ToggleInput(true);
  EXPECT_TRUE(TaskActionInputMatchesOperationAndCanonical(
      toggle.get(), mojom::TaskActionOperationKind::kFormToggle,
      FormIntent(24u, std::nullopt, true), "tab-1", "field-1"));
  toggle->toggle_state->checked = false;
  EXPECT_FALSE(TaskActionInputMatchesOperationAndCanonical(
      toggle.get(), mojom::TaskActionOperationKind::kFormToggle,
      FormIntent(24u, std::nullopt, true), "tab-1", "field-1"));

  auto submit = NoneInput();
  EXPECT_TRUE(TaskActionInputMatchesOperationAndCanonical(
      submit.get(), mojom::TaskActionOperationKind::kFormSubmit,
      FormIntent(14u, std::nullopt, std::nullopt), "tab-1", "submit-1"));
  submit->toggle_state = mojom::TaskToggleState::New();
  EXPECT_FALSE(TaskActionInputMatchesOperationAndCanonical(
      submit.get(), mojom::TaskActionOperationKind::kFormSubmit,
      FormIntent(14u, std::nullopt, std::nullopt), "tab-1", "submit-1"));
}

TEST(CoreTaskActionInputTest, ActivationBindsOneClosedExpectedState) {
  const auto input = NoneInput();
  EXPECT_TRUE(TaskActionInputMatchesOperationAndCanonical(
      input.get(), mojom::TaskActionOperationKind::kDomClick,
      DomActivationIntent(0u), "tab-1", "node-1"));
  const auto expanded =
      ReadCanonicalDomActivationIntent(DomActivationIntent(0u));
  ASSERT_TRUE(expanded);
  ASSERT_TRUE(expanded->expected_expanded.has_value());
  EXPECT_TRUE(*expanded->expected_expanded);
  const auto collapsed =
      ReadCanonicalDomActivationIntent(DomActivationIntent(1u));
  ASSERT_TRUE(collapsed);
  ASSERT_TRUE(collapsed->expected_expanded.has_value());
  EXPECT_FALSE(*collapsed->expected_expanded);
  EXPECT_TRUE(CanonicalDomActivationIntentMatchesDocument(
      DomActivationIntent(0u), "tab-1", "frame-1", "epoch-1", 7u, "node-1",
      "https://example.test"));
  EXPECT_FALSE(CanonicalDomActivationIntentMatchesDocument(
      DomActivationIntent(0u), "tab-1", "frame-1", "epoch-1", 8u, "node-1",
      "https://example.test"));
  EXPECT_FALSE(TaskActionInputMatchesOperationAndCanonical(
      input.get(), mojom::TaskActionOperationKind::kDomClick,
      DomActivationIntent(0u), "tab-1", "other-node"));
}

TEST(CoreTaskActionInputTest, AnOrdinaryPressNamesNoStateAndStillBinds) {
  const auto input = NoneInput();
  // Wire tag 2 is the third value of the disclosure field: no claim at all.
  // It binds the same node identity as the other two and is refused at 3,
  // which is the closed-enumeration rule and not an accident of range.
  EXPECT_TRUE(TaskActionInputMatchesOperationAndCanonical(
      input.get(), mojom::TaskActionOperationKind::kDomClick,
      DomActivationIntent(2u), "tab-1", "node-1"));
  const auto press = ReadCanonicalDomActivationIntent(DomActivationIntent(2u));
  ASSERT_TRUE(press);
  EXPECT_FALSE(press->expected_expanded.has_value());
  EXPECT_EQ(press->target.node_id, "node-1");
  EXPECT_TRUE(CanonicalDomActivationIntentMatchesDocument(
      DomActivationIntent(2u), "tab-1", "frame-1", "epoch-1", 7u, "node-1",
      "https://example.test"));
  EXPECT_FALSE(ReadCanonicalDomActivationIntent(DomActivationIntent(3u)));
  EXPECT_FALSE(TaskActionInputMatchesOperationAndCanonical(
      input.get(), mojom::TaskActionOperationKind::kDomClick,
      DomActivationIntent(3u), "tab-1", "node-1"));
}

TEST(CoreTaskActionInputTest, FocusBindsTheExactObservedDocument) {
  const auto input = NoneInput();
  EXPECT_TRUE(TaskActionInputMatchesOperationAndCanonical(
      input.get(), mojom::TaskActionOperationKind::kDomFocus, DomFocusIntent(),
      "tab-1", "node-1"));
  EXPECT_TRUE(CanonicalDomFocusIntentMatchesDocument(
      DomFocusIntent(), "tab-1", "frame-1", "epoch-1", 7u, "node-1",
      "https://example.test"));
  EXPECT_FALSE(CanonicalDomFocusIntentMatchesDocument(
      DomFocusIntent(), "tab-1", "frame-1", "epoch-1", 8u, "node-1",
      "https://example.test"));
  EXPECT_FALSE(TaskActionInputMatchesOperationAndCanonical(
      input.get(), mojom::TaskActionOperationKind::kDomClick, DomFocusIntent(),
      "tab-1", "node-1"));
}

TEST(CoreTaskActionInputTest, NonFormOperationsAcceptOnlyAnEmptyNoneInput) {
  auto input = NoneInput();
  EXPECT_TRUE(TaskActionInputMatchesOperationAndCanonical(
      input.get(), mojom::TaskActionOperationKind::kDomScroll, {}, "tab-1",
      "node-1"));
  input->supplied_value = mojom::TaskSuppliedValuePosition::New();
  EXPECT_FALSE(TaskActionInputMatchesOperationAndCanonical(
      input.get(), mojom::TaskActionOperationKind::kDomScroll, {}, "tab-1",
      "node-1"));
  EXPECT_FALSE(TaskActionInputMatchesOperationAndCanonical(
      nullptr, mojom::TaskActionOperationKind::kDomScroll, {}, "tab-1",
      "node-1"));
}

TEST(CoreTaskActionInputTest, TabControlsBindOnlyTheExactTwoFieldIntent) {
  const auto input = NoneInput();
  constexpr std::array operations = {
      mojom::TaskActionOperationKind::kHistoryBack,
      mojom::TaskActionOperationKind::kHistoryForward,
      mojom::TaskActionOperationKind::kReload,
      mojom::TaskActionOperationKind::kStopLoading,
  };
  constexpr std::array<uint8_t, operations.size()> tags = {2u, 3u, 28u, 29u};
  for (size_t index = 0; index < operations.size(); ++index) {
    SCOPED_TRACE(index);
    const std::vector<uint8_t> intent = TabControlIntent(tags[index]);
    EXPECT_TRUE(TaskActionInputMatchesOperationAndCanonical(
        input.get(), operations[index], intent, "tab-1", std::nullopt));
    EXPECT_FALSE(TaskActionInputMatchesOperationAndCanonical(
        input.get(), operations[index], intent, "tab-2", std::nullopt));
    EXPECT_FALSE(TaskActionInputMatchesOperationAndCanonical(
        input.get(), operations[index], intent, "tab-1", "invented-node"));

    std::vector<uint8_t> trailing = intent;
    const std::array<uint8_t, 1> invented = {0u};
    Field(&trailing, 2u, invented);
    EXPECT_FALSE(TaskActionInputMatchesOperationAndCanonical(
        input.get(), operations[index], trailing, "tab-1", std::nullopt));
  }
}

TEST(CoreTaskActionInputTest, MemoryOperationsBindExactContentFreeIntent) {
  const auto input = NoneInput();
  const std::array operations = {
      mojom::TaskActionOperationKind::kMemorySearch,
      mojom::TaskActionOperationKind::kMemorySave,
      mojom::TaskActionOperationKind::kMemoryUpdate,
      mojom::TaskActionOperationKind::kMemoryDelete,
  };
  for (size_t index = 0; index < operations.size(); ++index) {
    SCOPED_TRACE(index);
    const std::vector<uint8_t> intent =
        MemoryIntent(static_cast<uint8_t>(index));
    EXPECT_TRUE(TaskActionInputMatchesOperationAndCanonical(
        input.get(), operations[index], intent, "tab-1", std::nullopt));
    EXPECT_FALSE(TaskActionInputMatchesOperationAndCanonical(
        input.get(), operations[index], intent, "tab-2", std::nullopt));
    EXPECT_FALSE(TaskActionInputMatchesOperationAndCanonical(
        input.get(), operations[index], intent, "tab-1", "page-node"));
    EXPECT_FALSE(TaskActionInputMatchesOperationAndCanonical(
        input.get(), operations[(index + 1u) % operations.size()], intent,
        "tab-1", std::nullopt));
  }

  std::vector<uint8_t> trailing = MemoryIntent(0u);
  const std::array<uint8_t, 1> invented = {0u};
  Field(&trailing, 5u, invented);
  EXPECT_FALSE(TaskActionInputMatchesOperationAndCanonical(
      input.get(), mojom::TaskActionOperationKind::kMemorySearch, trailing,
      "tab-1", std::nullopt));

  const auto supplied = SuppliedInput(0u);
  EXPECT_FALSE(TaskActionInputMatchesOperationAndCanonical(
      supplied.get(), mojom::TaskActionOperationKind::kMemorySave,
      MemoryIntent(1u), "tab-1", std::nullopt));
}

TEST(CoreTaskActionInputTest, CoreInputProjectsToExactBipEnvelopeShape) {
  const auto fill =
      ActionWithInput(SuppliedInput(2u), mojom::PolicyActionClass::kFillField);
  const std::optional<ActionInput> fill_input =
      PageInputForTaskAction(*fill, ActionType::kSetText);
  ASSERT_TRUE(fill_input.has_value());
  EXPECT_EQ(fill_input->kind, ActionInputKind::kText);
  ASSERT_TRUE(fill_input->value_reference.has_value());
  EXPECT_EQ(fill_input->value_reference->value, "turn-4-values-1-value-2");
  EXPECT_FALSE(fill_input->text.has_value());
  EXPECT_FALSE(fill_input->option_value.has_value());

  const auto select = ActionWithInput(SuppliedInput(1u),
                                      mojom::PolicyActionClass::kSelectOption);
  const std::optional<ActionInput> select_input =
      PageInputForTaskAction(*select, ActionType::kSelectOption);
  ASSERT_TRUE(select_input.has_value());
  EXPECT_EQ(select_input->kind, ActionInputKind::kOption);
  ASSERT_TRUE(select_input->value_reference.has_value());
  EXPECT_EQ(select_input->value_reference->value, "turn-4-values-1-value-1");

  const auto toggle = ActionWithInput(ToggleInput(true),
                                      mojom::PolicyActionClass::kToggleControl);
  const std::optional<ActionInput> toggle_input =
      PageInputForTaskAction(*toggle, ActionType::kToggle);
  ASSERT_TRUE(toggle_input.has_value());
  EXPECT_EQ(toggle_input->kind, ActionInputKind::kToggleState);
  EXPECT_EQ(toggle_input->checked, true);

  const auto submit =
      ActionWithInput(NoneInput(), mojom::PolicyActionClass::kSubmitForm);
  const std::optional<ActionInput> submit_input =
      PageInputForTaskAction(*submit, ActionType::kSubmitForm);
  ASSERT_TRUE(submit_input.has_value());
  EXPECT_EQ(submit_input->kind, ActionInputKind::kNone);

  EXPECT_FALSE(PageInputForTaskAction(*fill, ActionType::kSelectOption));
  EXPECT_FALSE(PageInputForTaskAction(*toggle, ActionType::kSetText));
  EXPECT_FALSE(PageInputForTaskAction(*submit, ActionType::kToggle));
}

}  // namespace
}  // namespace taffy
