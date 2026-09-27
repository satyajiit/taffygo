// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <utility>

#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "taffy/test/correctness/form_action_test_base.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::test {
namespace {

class FormActionVisibilityTest : public FormActionTestBase {};

// A field below the fold when the page was read, and then scrolled into view
// by a scroll that changes nothing else, is filled. The renderer requires a
// field to be visible before it types into it, and that has to be judged
// from where the field is now rather than from where the reading left it:
// no reading follows a scroll. On a phone the myAadhaar CAPTCHA's field was
// below the fold when the page was read and was scrolled to before the sheet
// asked for it (decision 0250).
IN_PROC_BROWSER_TEST_F(FormActionVisibilityTest,
                       AFieldScrolledIntoViewAfterTheReadIsFilled) {
  ObserveFixture("form-laboratory");
  ASSERT_TRUE(content::ExecJs(web_contents(),
                              "const spacer = document.createElement('div');"
                              "spacer.style.height = '6000px';"
                              "document.body.prepend(spacer);"
                              "window.scrollTo(0, 0);"));
  const ObservationEnvelope document = ObserveCurrentDocument();
  const ObservationEnvelope observed =
      ObserveFormContaining(document, "Full name", mojom::ActionType::kSetText);
  const std::optional<GraphPayloadNode> name =
      NodeNamedForAction(observed, "Full name", mojom::ActionType::kSetText);
  ASSERT_TRUE(name.has_value());

  ASSERT_TRUE(content::ExecJs(
      web_contents(),
      "document.getElementById('full-name').scrollIntoView({block: "
      "'center'});"));
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
  EXPECT_EQ("Taffy Test Person",
            content::EvalJs(web_contents(),
                            "document.getElementById('full-name').value")
                .ExtractString());
}

}  // namespace
}  // namespace taffy::test
