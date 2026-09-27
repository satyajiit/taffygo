// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/action_accessibility_mapping.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>

#include "testing/gtest/include/gtest/gtest.h"

// Decision 0059, asserted rather than described.
//
// The claim under test is a negative one - a write never becomes a DOM call -
// and a negative is hard to test by watching a function run. It is easy to
// test by reading what the function is allowed to return, which is why the
// mapping is a table with an output type narrow enough to have no way of
// expressing a DOM call at all. These cases walk that table.

namespace taffy {
namespace {

// Every operation, so a member added to ActionKind fails a count here as well
// as the exhaustive switches in the mapping.
constexpr auto kEveryAction = std::to_array<ActionKind>({
    ActionKind::kActivate,
    ActionKind::kFocus,
    ActionKind::kScrollIntoView,
    ActionKind::kSetText,
    ActionKind::kSelectOption,
    ActionKind::kToggle,
    ActionKind::kSubmitForm,
});

ActionValue TextValue(const std::string& text) {
  ActionValue value;
  value.text = text;
  return value;
}

ActionValue CheckedValue(bool checked) {
  ActionValue value;
  value.checked = checked;
  return value;
}

ActionValue ValueFor(ActionKind action) {
  switch (action) {
    case ActionKind::kSetText:
    case ActionKind::kSelectOption:
      return TextValue("a value the browser resolved");
    case ActionKind::kToggle:
      return CheckedValue(true);
    case ActionKind::kActivate:
    case ActionKind::kFocus:
    case ActionKind::kScrollIntoView:
    case ActionKind::kSubmitForm:
      return ActionValue();
  }
}

TEST(ActionAccessibilityMappingTest, EveryOperationIsAnAccessibilityAction) {
  // The whole vocabulary an operation may reach the page through. Four
  // members, all of them platform accessibility actions, and no member for
  // anything else - there is no DOM setter to name, no event to synthesize,
  // and no form to submit, because the output type has nowhere to put one.
  constexpr auto kPermitted = std::to_array<ax::mojom::Action>({
      ax::mojom::Action::kScrollToMakeVisible,
      ax::mojom::Action::kFocus,
      ax::mojom::Action::kDoDefault,
      ax::mojom::Action::kSetValue,
  });

  for (ActionKind action : kEveryAction) {
    const std::optional<AccessibilityAction> mapped =
        AccessibilityActionFor(action, ValueFor(action));
    ASSERT_TRUE(mapped.has_value()) << static_cast<int>(action);
    EXPECT_NE(std::ranges::find(kPermitted, mapped->action), kPermitted.end())
        << "operation " << static_cast<int>(action)
        << " reaches the page by something outside the accessibility path";
  }
}

TEST(ActionAccessibilityMappingTest, EachWriteMapsToTheActionDecidedForIt) {
  EXPECT_EQ(
      AccessibilityActionFor(ActionKind::kSetText, TextValue("Shrewsbury"))
          ->action,
      ax::mojom::Action::kSetValue);
  EXPECT_EQ(AccessibilityActionFor(ActionKind::kSelectOption,
                                   TextValue("standard-delivery"))
                ->action,
            ax::mojom::Action::kDoDefault);
  EXPECT_EQ(AccessibilityActionFor(ActionKind::kSelectOption,
                                   TextValue("standard-delivery"))
                ->target,
            AccessibilityTarget::kOptionWithValue);
  EXPECT_EQ(
      AccessibilityActionFor(ActionKind::kToggle, CheckedValue(true))->action,
      ax::mojom::Action::kDoDefault);
  EXPECT_EQ(
      AccessibilityActionFor(ActionKind::kSubmitForm, ActionValue())->action,
      ax::mojom::Action::kDoDefault);
}

TEST(ActionAccessibilityMappingTest, SubmittingIsPressingTheControl) {
  // The strongest form the no-programmatic-submission rule can be asserted
  // in. A submission is not merely "not HTMLFormElement::submit()" - it is
  // indistinguishable, at this seam, from activating the button, because it
  // is the same action on the same kind of target. There is no submission
  // primitive in this process for a defect or an attacker to reach.
  const std::optional<AccessibilityAction> submit =
      AccessibilityActionFor(ActionKind::kSubmitForm, ActionValue());
  const std::optional<AccessibilityAction> activate =
      AccessibilityActionFor(ActionKind::kActivate, ActionValue());
  ASSERT_TRUE(submit.has_value());
  ASSERT_TRUE(activate.has_value());
  EXPECT_EQ(submit->action, activate->action);
  EXPECT_EQ(submit->value, activate->value);
}

TEST(ActionAccessibilityMappingTest, OnlyTheTwoSettersCarryAValue) {
  for (ActionKind action : kEveryAction) {
    const std::optional<AccessibilityAction> mapped =
        AccessibilityActionFor(action, ValueFor(action));
    ASSERT_TRUE(mapped.has_value());
    const bool carries = !mapped->value.empty();
    const bool expected =
        action == ActionKind::kSetText || action == ActionKind::kSelectOption;
    EXPECT_EQ(carries, expected) << static_cast<int>(action);
  }
}

TEST(ActionAccessibilityMappingTest, AnOperationWithNoValueIsRefused) {
  // Only the browser builds these, so a missing value is a defect on the far
  // side of the process boundary. The answer to a defect is a refusal, never
  // a smaller version of what was asked for - filling a field with nothing
  // and reporting success would be the worst available outcome.
  EXPECT_FALSE(
      AccessibilityActionFor(ActionKind::kSetText, ActionValue()).has_value());
  EXPECT_FALSE(AccessibilityActionFor(ActionKind::kSelectOption, ActionValue())
                   .has_value());
  EXPECT_FALSE(
      AccessibilityActionFor(ActionKind::kToggle, ActionValue()).has_value());

  // The empty string is not a value either. Clearing a field is a different
  // intent, and a proposal that meant it would have to say so in a shape a
  // person could be shown.
  EXPECT_FALSE(
      AccessibilityActionFor(ActionKind::kSetText, TextValue("")).has_value());
}

TEST(ActionAccessibilityMappingTest, AValueForTheWrongOperationIsIgnored) {
  // A toggle state handed to a text write does not become text, and text
  // handed to a toggle does not become a state. Each operation reads only its
  // own operand, so a command that carried the wrong one is refused by the
  // mapping rather than performed with whatever it happened to find.
  EXPECT_FALSE(AccessibilityActionFor(ActionKind::kSetText, CheckedValue(true))
                   .has_value());
  EXPECT_FALSE(AccessibilityActionFor(ActionKind::kToggle, TextValue("true"))
                   .has_value());
}

TEST(ActionAccessibilityMappingTest, TheTwoQuestionsAboutAnOperationAgree) {
  // Exactly the operations that need a value are the two setters and the
  // toggle; exactly the ones that change something a person would notice are
  // those three plus activation and submission. Stating both as tables and
  // asserting them here keeps the executor from re-deriving either.
  EXPECT_FALSE(ActionRequiresValue(ActionKind::kScrollIntoView));
  EXPECT_FALSE(ActionRequiresValue(ActionKind::kFocus));
  EXPECT_FALSE(ActionRequiresValue(ActionKind::kActivate));
  EXPECT_FALSE(ActionRequiresValue(ActionKind::kSubmitForm));
  EXPECT_TRUE(ActionRequiresValue(ActionKind::kSetText));
  EXPECT_TRUE(ActionRequiresValue(ActionKind::kSelectOption));
  EXPECT_TRUE(ActionRequiresValue(ActionKind::kToggle));

  EXPECT_FALSE(ActionMutatesPage(ActionKind::kScrollIntoView));
  EXPECT_FALSE(ActionMutatesPage(ActionKind::kFocus));
  for (ActionKind action :
       {ActionKind::kActivate, ActionKind::kSetText, ActionKind::kSelectOption,
        ActionKind::kToggle, ActionKind::kSubmitForm}) {
    EXPECT_TRUE(ActionMutatesPage(action)) << static_cast<int>(action);
  }
}

}  // namespace
}  // namespace taffy
