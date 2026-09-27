// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "content/public/renderer/render_frame.h"
#include "taffy/renderer/test/endpoint_test_harness.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_element.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_node.h"
#include "third_party/blink/public/web/web_option_element.h"
#include "third_party/blink/public/web/web_select_element.h"

namespace taffy::test {
namespace {

class FormActionSelectTest : public EndpointTestHarness {
 protected:
  const mojom::SemanticNode* ExactOwnedSelect(
      const mojom::PageSnapshot& snapshot) {
    std::set<std::string> owned_controls;
    for (const mojom::SemanticEdgePtr& edge : snapshot.edges) {
      if (edge->relationship == mojom::RelationshipKind::kOwns) {
        owned_controls.insert(edge->to_node_id);
      }
    }
    const mojom::SemanticNode* match = nullptr;
    for (const mojom::SemanticNodePtr& node : snapshot.nodes) {
      if (!owned_controls.contains(node->node_id) ||
          node->role != mojom::SemanticRole::kSelect ||
          std::ranges::find(node->actions, mojom::ActionType::kSelectOption) ==
              node->actions.end()) {
        continue;
      }
      if (match) {
        ADD_FAILURE() << "more than one owned select was actionable";
        return nullptr;
      }
      match = node.get();
    }
    return match;
  }

  mojom::RendererActionCommandPtr SelectCommand(
      const mojom::PageSnapshot& snapshot,
      const mojom::SemanticNode& select,
      std::string value) {
    auto command = mojom::RendererActionCommand::New();
    command->schema_version = "0.11";
    command->command_id = "select-region";
    command->frame_id = "frame-main";
    command->page_epoch = page_epoch();
    command->required_graph_revision = snapshot.graph_revision;
    command->node_id = select.node_id;
    command->operation = mojom::ActionType::kSelectOption;
    command->deadline_ms = 1000u;
    command->input = mojom::ActionInput::New();
    command->input->kind = mojom::ActionInputKind::kOption;
    command->input->sensitivity = select.sensitivity;
    command->input->option_value = std::move(value);

    auto role = mojom::Precondition::New();
    role->kind = mojom::PreconditionKind::kNodeRoleUnchanged;
    role->expected_role = select.role;
    command->renderer_preconditions.push_back(std::move(role));
    auto action = mojom::Precondition::New();
    action->kind = mojom::PreconditionKind::kNodeActionAvailable;
    action->expected_action_type = mojom::ActionType::kSelectOption;
    command->renderer_preconditions.push_back(std::move(action));
    auto sensitivity = mojom::Precondition::New();
    sensitivity->kind = mojom::PreconditionKind::kNotSensitiveField;
    sensitivity->max_sensitivity = select.sensitivity;
    command->renderer_preconditions.push_back(std::move(sensitivity));
    return command;
  }

  std::vector<blink::WebOptionElement> LiveOptions(std::string_view select_id) {
    const blink::WebDocument document =
        GetMainRenderFrame()->GetWebFrame()->GetDocument();
    const blink::WebElement element =
        document.GetElementById(blink::WebString::FromUtf8(select_id));
    const blink::WebSelectElement select =
        element.DynamicTo<blink::WebSelectElement>();
    if (select.IsNull()) {
      ADD_FAILURE() << "the live select disappeared";
      return {};
    }
    std::vector<blink::WebOptionElement> options;
    for (blink::WebNode current = select.FirstChild(); !current.IsNull();
         current = current.NextInFlatTree(select)) {
      blink::WebOptionElement option =
          current.DynamicTo<blink::WebOptionElement>();
      if (!option.IsNull()) {
        options.push_back(option);
      }
    }
    return options;
  }
};

TEST_F(FormActionSelectTest,
       AccessibilityOptionActionSelectsExactValueAndPublishesEvidence) {
  LoadAndBind(
      "<html><body><form>"
      "<label for='region'>Region</label>"
      "<select id='region' name='region'>"
      "<option value='north' selected>North</option>"
      "<option value='south'>South</option>"
      "</select></form></body></html>");
  mojom::SnapshotResultPtr snapshot = Snapshot(DocumentRequest());
  ASSERT_TRUE(snapshot && snapshot->snapshot);
  const mojom::SemanticNode* select = ExactOwnedSelect(*snapshot->snapshot);
  ASSERT_TRUE(select);

  const std::vector<blink::WebOptionElement> before = LiveOptions("region");
  ASSERT_EQ(before.size(), 2u);
  EXPECT_TRUE(before[0].IsSelected());
  EXPECT_FALSE(before[1].IsSelected());

  mojom::RendererActionResultPtr selected =
      Execute(SelectCommand(*snapshot->snapshot, *select, "south"));
  ASSERT_TRUE(selected);
  ASSERT_EQ(selected->outcome, mojom::RendererActionOutcome::kDispatched)
      << "failed precondition="
      << (selected->failed_precondition.has_value()
              ? static_cast<int>(*selected->failed_precondition)
              : -1);

  const std::vector<blink::WebOptionElement> after = LiveOptions("region");
  ASSERT_EQ(after.size(), 2u);
  EXPECT_FALSE(after[0].IsSelected());
  EXPECT_TRUE(after[1].IsSelected());

  mojom::ResolveNodeRequestPtr resolve =
      ResolveRequest(select->node_id, page_epoch());
  resolve->required_graph_revision = selected->observed_at_revision;
  mojom::ResolveNodeResultPtr resolved = Resolve(std::move(resolve));
  ASSERT_TRUE(resolved && resolved->node);
  EXPECT_GT(resolved->node->value_changed_at_revision,
            selected->observed_at_revision);
  EXPECT_GT(resolved->node->observed_at_revision,
            selected->observed_at_revision);
}

TEST_F(FormActionSelectTest,
       DuplicateOptionValuesRefuseWithoutChoosingByOrder) {
  LoadAndBind(
      "<html><body><form>"
      "<label for='region'>Region</label>"
      "<select id='region' name='region'>"
      "<option value='hold' selected disabled>Choose</option>"
      "<option value='south'>South one</option>"
      "<option value='south'>South two</option>"
      "</select></form></body></html>");
  mojom::SnapshotResultPtr snapshot = Snapshot(DocumentRequest());
  ASSERT_TRUE(snapshot && snapshot->snapshot);
  const mojom::SemanticNode* select = ExactOwnedSelect(*snapshot->snapshot);
  ASSERT_TRUE(select);

  mojom::RendererActionResultPtr selected =
      Execute(SelectCommand(*snapshot->snapshot, *select, "south"));
  ASSERT_TRUE(selected);
  EXPECT_EQ(selected->outcome, mojom::RendererActionOutcome::kUnsupported);

  const std::vector<blink::WebOptionElement> after = LiveOptions("region");
  ASSERT_EQ(after.size(), 3u);
  EXPECT_TRUE(after[0].IsSelected());
  EXPECT_FALSE(after[1].IsSelected());
  EXPECT_FALSE(after[2].IsSelected());
}

TEST_F(FormActionSelectTest, AlreadySelectedOptionRefusesWithoutTogglingOff) {
  LoadAndBind(
      "<html><body><form>"
      "<label for='region'>Region</label>"
      "<select id='region' name='region'>"
      "<option value='north' selected>North</option>"
      "<option value='south'>South</option>"
      "</select></form></body></html>");
  mojom::SnapshotResultPtr snapshot = Snapshot(DocumentRequest());
  ASSERT_TRUE(snapshot && snapshot->snapshot);
  const mojom::SemanticNode* select = ExactOwnedSelect(*snapshot->snapshot);
  ASSERT_TRUE(select);

  mojom::RendererActionResultPtr selected =
      Execute(SelectCommand(*snapshot->snapshot, *select, "north"));
  ASSERT_TRUE(selected);
  EXPECT_EQ(selected->outcome, mojom::RendererActionOutcome::kUnsupported);

  const std::vector<blink::WebOptionElement> after = LiveOptions("region");
  ASSERT_EQ(after.size(), 2u);
  EXPECT_TRUE(after[0].IsSelected());
  EXPECT_FALSE(after[1].IsSelected());
}

}  // namespace
}  // namespace taffy::test
