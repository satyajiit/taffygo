// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/time/time.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "taffy/common/public/bip_action.h"
#include "taffy/components/security/browser/value_reference_vault.h"
#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"
#include "taffy/test/correctness/form_action_test_base.h"
#include "taffy/test/support/bip_graph_payload_reader.h"
#include "taffy/test/support/taffy_observation_test_base.h"
#include "testing/gtest/include/gtest/gtest.h"

// The live write path, from a node the renderer really observed through the
// browser's one-use capability and value vault and back to a verified page
// change. Before this suite, every browser action test named a fabricated node
// or an unregistered capability and therefore proved only that writes could be
// refused. A dispatcher that refused every form action satisfied all of them.

namespace taffy::test {
namespace {

class FormActionTest : public FormActionTestBase {};

IN_PROC_BROWSER_TEST_F(FormActionTest,
                       BrowserOwnedValuesFillAndSelectOnTheLivePage) {
  ObserveFixture("form-laboratory");
  ASSERT_TRUE(content::ExecJs(
      web_contents(),
      "document.getElementById('region').setAttribute('autocomplete', "
      "'country-name')"));
  // Script mutation advances the renderer's graph revision and retires the
  // root authority held by the preceding snapshot. Discover the form again;
  // carrying that stale root into a section request must remain unsupported.
  const ObservationEnvelope document = ObserveCurrentDocument();
  ObservationEnvelope observed =
      ObserveFormContaining(document, "Full name", mojom::ActionType::kSetText);
  const std::optional<GraphPayloadNode> name =
      NodeNamedForAction(observed, "Full name", mojom::ActionType::kSetText);
  ASSERT_TRUE(name.has_value());

  const ValueReference name_value =
      MintValue(Sensitivity::kPersonal, "Taffy Test Person");
  ASSERT_TRUE(name_value.is_valid());
  ActionInput text;
  text.kind = ActionInputKind::kText;
  text.sensitivity = Sensitivity::kPersonal;
  text.value_reference = name_value;
  const ActionResult filled = DispatchTaskActionAndWait(Action(
      observed, *name, ActionType::kSetText, std::move(text), ValueChanged()));
  EXPECT_EQ(ActionResultCode::kVerified, filled.result_code);
  EXPECT_TRUE(filled.dispatched);
  EXPECT_FALSE(values_.Holds(name_value))
      << "The one-use value survived a successful dispatch.";
  EXPECT_EQ("Taffy Test Person",
            content::EvalJs(web_contents(),
                            "document.getElementById('full-name').value")
                .ExtractString());

  observed = RefreshForm(observed);
  const std::optional<GraphPayloadNode> region =
      NodeNamedForAction(observed, "Region", mojom::ActionType::kSelectOption);
  ASSERT_TRUE(region.has_value());
  const ValueReference option_value =
      MintValue(Sensitivity::kNotSensitive, "south");
  ASSERT_TRUE(option_value.is_valid());
  ActionInput option;
  option.kind = ActionInputKind::kOption;
  option.sensitivity = Sensitivity::kNotSensitive;
  option.value_reference = option_value;
  const ActionResult selected = DispatchTaskActionAndWait(
      Action(observed, *region, ActionType::kSelectOption, std::move(option),
             ValueChanged()));
  EXPECT_EQ(ActionResultCode::kVerified, selected.result_code);
  EXPECT_TRUE(selected.dispatched);
  EXPECT_FALSE(values_.Holds(option_value));
  EXPECT_EQ("south", content::EvalJs(web_contents(),
                                     "document.getElementById('region').value")
                         .ExtractString());
}

IN_PROC_BROWSER_TEST_F(FormActionTest, ToggleUsesTheDeclaredEndState) {
  ObserveFixture("form-laboratory");
  ASSERT_TRUE(content::ExecJs(
      web_contents(),
      "document.getElementById('gift-wrap').name = 'shipping_preference'"));
  const ObservationEnvelope document = ObserveCurrentDocument();
  const ObservationEnvelope observed =
      ObserveFormContaining(document, "Gift wrap", mojom::ActionType::kToggle);
  const std::optional<GraphPayloadNode> gift_wrap =
      NodeNamedForAction(observed, "Gift wrap", mojom::ActionType::kToggle);
  ASSERT_TRUE(gift_wrap.has_value());

  ActionInput toggle;
  toggle.kind = ActionInputKind::kToggleState;
  toggle.sensitivity = Sensitivity::kPersonal;
  toggle.checked = true;
  const ActionResult result = DispatchTaskActionAndWait(
      Action(observed, *gift_wrap, ActionType::kToggle, std::move(toggle),
             BecameChecked()));

  EXPECT_EQ(ActionResultCode::kVerified, result.result_code);
  EXPECT_TRUE(result.dispatched);
  EXPECT_TRUE(content::EvalJs(web_contents(),
                              "document.getElementById('gift-wrap').checked")
                  .ExtractBool());
}

IN_PROC_BROWSER_TEST_F(FormActionTest,
                       SubmitUsesThePageControlAndBrowserCommitWitness) {
  const ObservationEnvelope document = ObserveFixture("form-laboratory");
  ObservationEnvelope observed =
      ObserveFormContaining(document, "Full name", mojom::ActionType::kSetText);
  const std::optional<GraphPayloadNode> name =
      NodeNamedForAction(observed, "Full name", mojom::ActionType::kSetText);
  ASSERT_TRUE(name.has_value());

  const ValueReference name_value =
      MintValue(Sensitivity::kPersonal, "Taffy Submit Person");
  ASSERT_TRUE(name_value.is_valid());
  ActionInput text;
  text.kind = ActionInputKind::kText;
  text.sensitivity = Sensitivity::kPersonal;
  text.value_reference = name_value;
  ASSERT_EQ(
      ActionResultCode::kVerified,
      DispatchTaskActionAndWait(Action(observed, *name, ActionType::kSetText,
                                       std::move(text), ValueChanged()))
          .result_code);

  observed = RefreshForm(observed);
  const std::optional<GraphPayloadNode> submit = NodeNamedForAction(
      observed, "Place fixture order", mojom::ActionType::kSubmitForm);
  ASSERT_TRUE(submit.has_value());
  ActionInput none;
  none.kind = ActionInputKind::kNone;
  const ActionResult result = DispatchTaskActionAndWait(
      Action(observed, *submit, ActionType::kSubmitForm, std::move(none),
             CommittedOnFixtureOrigin()));

  EXPECT_EQ(ActionResultCode::kVerified, result.result_code)
      << "The submit control was refused. RendererActionExecutor requires "
         "kVisible and kEnabled for kSubmitForm, and the required-state loop "
         "wants each asserted rather than merely un-negated. Observed states: "
      << DescribeNodeStates(submit->states);
  EXPECT_TRUE(result.dispatched);
  EXPECT_EQ("/forms/submit", web_contents()->GetLastCommittedURL().path());
}

IN_PROC_BROWSER_TEST_F(FormActionTest,
                       UnknownFieldClassificationStaysFailClosed) {
  const ObservationEnvelope document = ObserveFixture("form-laboratory");
  const ObservationEnvelope observed =
      ObserveFormContaining(document, "Gift wrap", mojom::ActionType::kToggle);
  const std::optional<GraphPayloadNode> gift_wrap =
      NodeNamedForAction(observed, "Gift wrap", mojom::ActionType::kToggle);
  ASSERT_TRUE(gift_wrap.has_value());

  ActionInput toggle;
  toggle.kind = ActionInputKind::kToggleState;
  toggle.sensitivity = Sensitivity::kNotSensitive;
  toggle.checked = true;
  const ActionResult result = DispatchTaskActionAndWait(
      Action(observed, *gift_wrap, ActionType::kToggle, std::move(toggle),
             BecameChecked()));

  EXPECT_EQ(ActionResultCode::kSensitiveField, result.result_code);
  EXPECT_FALSE(result.dispatched);
  EXPECT_FALSE(content::EvalJs(web_contents(),
                               "document.getElementById('gift-wrap').checked")
                   .ExtractBool());
}

}  // namespace
}  // namespace taffy::test
