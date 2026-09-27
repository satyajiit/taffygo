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

#include "taffy/renderer/adapters/adapter.h"
#include "taffy/renderer/semantic_graph_store.h"
#include "taffy/renderer/test/endpoint_test_harness.h"
#include "testing/gtest/include/gtest/gtest.h"

// Form projection and exact-node write evidence against a parsed document.
// These tests live together because they prove one privacy boundary: the
// renderer describes controls and can act on one resolved node, but never
// exports the value already held by any control.

namespace taffy::test {
namespace {

class FormAdapterTest : public EndpointTestHarness {};

TEST_F(FormAdapterTest, FormlessControlsAreEnumerated) {
  // A sign-in box with no <form> element is the ordinary shape of the web,
  // and these are exactly the controls whose classification matters most.
  LoadAndBind(
      "<html><body>"
      "<div><label for='u'>Email</label>"
      "<input id='u' name='email' type='email' autocomplete='email'>"
      "<input id='p' name='password' type='password'>"
      "<button id='go' type='button'>Sign in</button></div>"
      "</body></html>");
  mojom::SnapshotResultPtr result = Snapshot(DocumentRequest());
  ASSERT_TRUE(result && result->snapshot);

  const mojom::AdapterReport* report =
      ReportFor(*result->snapshot, mojom::AdapterKind::kForms);
  ASSERT_TRUE(report);
  EXPECT_NE(report->status, mojom::AdapterStatus::kUnsupported);

  int text_fields = 0;
  int withheld = 0;
  std::vector<std::string> control_types;
  for (const mojom::SemanticNodePtr& node : result->snapshot->nodes) {
    if (node->role == mojom::SemanticRole::kTextField) {
      ++text_fields;
    }
    if (node->value_descriptor &&
        node->value_descriptor->kind == mojom::ValueKind::kSecretWithheld) {
      ++withheld;
    }
    for (const mojom::NodeAttributePtr& attribute : node->attributes) {
      if (attribute->name == mojom::AttributeName::kInputType) {
        control_types.push_back(attribute->value);
      }
    }
  }
  EXPECT_GT(text_fields, 0)
      << "no control outside a form element was described at all";
  EXPECT_EQ(withheld, 1)
      << "the password control outside a form must still produce a "
         "structural placeholder";
  EXPECT_EQ(control_types,
            (std::vector<std::string>{"email", "password", "button"}))
      << "the iterative fallback walk must preserve document order; field "
         "values are bound positionally in the browser-owned surface";
}

TEST_F(FormAdapterTest, OrdinaryFieldValuesAreNeverRead) {
  // Not even for a demonstrably insensitive field. Emptiness is a fact about
  // a value, and a code path that reads one "only when the field is ordinary"
  // is a path whose safety depends entirely on the classifier being right.
  LoadAndBind(
      "<html><body><form>"
      "<input type='search' name='search' value='desk lamp'>"
      "</form></body></html>");
  mojom::SnapshotResultPtr result = Snapshot(DocumentRequest());
  ASSERT_TRUE(result && result->snapshot);

  for (const std::string& value : AllStringsIn(*result)) {
    EXPECT_EQ(value.find("desk lamp"), std::string::npos)
        << "a form control value reached the wire";
  }
}

TEST_F(FormAdapterTest, FormActionsCarryTheControlsStandardAccessibleName) {
  LoadAndBind(
      "<html><body><form>"
      "<label for='city'>Destination city</label>"
      "<input id='city' name='city' type='text'>"
      "<label><input name='gift' type='checkbox'> Gift wrap</label>"
      "<button type='submit'>Place order</button>"
      "</form></body></html>");
  mojom::SnapshotResultPtr result = Snapshot(DocumentRequest());
  ASSERT_TRUE(result && result->snapshot);

  std::set<std::string> node_ids;
  for (const mojom::SemanticNodePtr& node : result->snapshot->nodes) {
    EXPECT_TRUE(node_ids.insert(node->node_id).second)
        << "two adapter rows crossed the wire under one node identity";
  }

  struct ExpectedControl {
    mojom::ActionType action;
    std::string_view name;
  };
  constexpr ExpectedControl kExpected[] = {
      {mojom::ActionType::kSetText, "Destination city"},
      {mojom::ActionType::kToggle, "Gift wrap"},
      {mojom::ActionType::kSubmitForm, "Place order"},
  };
  for (const ExpectedControl& expected : kExpected) {
    const auto found = std::ranges::find_if(
        result->snapshot->nodes, [&](const mojom::SemanticNodePtr& node) {
          return node->name.value_or(std::string()) == expected.name &&
                 std::ranges::find(node->actions, expected.action) !=
                     node->actions.end();
        });
    EXPECT_NE(found, result->snapshot->nodes.end())
        << "an ordinary HTML label did not name the exact actionable node: "
        << expected.name;
    if (found == result->snapshot->nodes.end()) {
      continue;
    }
    const auto identity_edge = std::ranges::find_if(
        result->snapshot->edges, [&](const mojom::SemanticEdgePtr& edge) {
          return edge->relationship == mojom::RelationshipKind::kSameEntityAs &&
                 (edge->from_node_id == (*found)->node_id ||
                  edge->to_node_id == (*found)->node_id);
        });
    EXPECT_NE(identity_edge, result->snapshot->edges.end())
        << "the form action was not joined to Blink's exact accessibility "
           "identity: "
        << expected.name;
  }
}

TEST_F(FormAdapterTest,
       AccessibilityValueWriteProducesContentFreeExactNodeEvidence) {
  LoadAndBind(
      "<html><body><form>"
      "<label for='city'>City</label>"
      "<input id='city' name='city' type='text'>"
      "</form></body></html>");
  mojom::SnapshotResultPtr snapshot = Snapshot(DocumentRequest());
  ASSERT_TRUE(snapshot && snapshot->snapshot);

  std::set<std::string> exact_form_controls;
  for (const mojom::SemanticEdgePtr& edge : snapshot->snapshot->edges) {
    if (edge->relationship == mojom::RelationshipKind::kOwns) {
      exact_form_controls.insert(edge->to_node_id);
    }
  }
  const mojom::SemanticNode* field = nullptr;
  for (const mojom::SemanticNodePtr& node : snapshot->snapshot->nodes) {
    if (exact_form_controls.contains(node->node_id) &&
        node->role == mojom::SemanticRole::kTextField &&
        std::ranges::find(node->actions, mojom::ActionType::kSetText) !=
            node->actions.end()) {
      field = node.get();
      break;
    }
  }
  ASSERT_TRUE(field)
      << "the ordinary editable field did not advertise SET_TEXT";

  auto command = mojom::RendererActionCommand::New();
  command->schema_version = "0.11";
  command->command_id = "write-city";
  command->frame_id = "frame-main";
  command->page_epoch = page_epoch();
  command->required_graph_revision = snapshot->snapshot->graph_revision;
  command->node_id = field->node_id;
  command->operation = mojom::ActionType::kSetText;
  command->deadline_ms = 1000u;
  command->input = mojom::ActionInput::New();
  command->input->kind = mojom::ActionInputKind::kText;
  command->input->sensitivity = mojom::Sensitivity::kNotSensitive;
  command->input->text = "Pune";

  auto role = mojom::Precondition::New();
  role->kind = mojom::PreconditionKind::kNodeRoleUnchanged;
  role->expected_role = field->role;
  command->renderer_preconditions.push_back(std::move(role));
  auto action = mojom::Precondition::New();
  action->kind = mojom::PreconditionKind::kNodeActionAvailable;
  action->expected_action_type = mojom::ActionType::kSetText;
  command->renderer_preconditions.push_back(std::move(action));
  auto sensitivity = mojom::Precondition::New();
  sensitivity->kind = mojom::PreconditionKind::kNotSensitiveField;
  sensitivity->max_sensitivity = field->sensitivity;
  command->renderer_preconditions.push_back(std::move(sensitivity));

  mojom::RendererActionResultPtr write = Execute(std::move(command));
  ASSERT_TRUE(write);
  EXPECT_EQ(write->outcome, mojom::RendererActionOutcome::kDispatched);

  mojom::ResolveNodeRequestPtr resolve =
      ResolveRequest(field->node_id, page_epoch());
  // The dispatch reply carries the last pre-action revision. The exact input
  // event is evidence only if a fresh read is strictly newer than that
  // baseline; equality would make the browser verifier wait until timeout.
  resolve->required_graph_revision = write->observed_at_revision;
  mojom::ResolveNodeResultPtr resolved = Resolve(std::move(resolve));
  ASSERT_TRUE(resolved && resolved->node)
      << "the exact DOM form-control identity did not survive its own "
         "accessibility value write; resolution code="
      << (resolved ? static_cast<int>(resolved->code) : -1);
  EXPECT_GT(resolved->node->value_changed_at_revision,
            write->observed_at_revision);
  EXPECT_GT(resolved->node->observed_at_revision, write->observed_at_revision);
  EXPECT_FALSE(resolved->node->value_digest);
}

TEST_F(FormAdapterTest,
       AccessibilityToggleKeepsPreActionBaselineAndRefreshesExactState) {
  LoadAndBind(
      "<html><body><form>"
      "<label><input id='gift' name='shipping_preference' "
      "type='checkbox'> Gift wrap</label>"
      "</form></body></html>");
  mojom::SnapshotResultPtr snapshot = Snapshot(DocumentRequest());
  ASSERT_TRUE(snapshot && snapshot->snapshot);

  std::set<std::string> exact_form_controls;
  for (const mojom::SemanticEdgePtr& edge : snapshot->snapshot->edges) {
    if (edge->relationship == mojom::RelationshipKind::kOwns) {
      exact_form_controls.insert(edge->to_node_id);
    }
  }
  const mojom::SemanticNode* checkbox = nullptr;
  for (const mojom::SemanticNodePtr& node : snapshot->snapshot->nodes) {
    if (exact_form_controls.contains(node->node_id) &&
        node->role == mojom::SemanticRole::kCheckbox &&
        std::ranges::find(node->actions, mojom::ActionType::kToggle) !=
            node->actions.end()) {
      checkbox = node.get();
      break;
    }
  }
  ASSERT_TRUE(checkbox);
  EXPECT_NE(std::ranges::find(checkbox->states, mojom::NodeState::kUnchecked),
            checkbox->states.end())
      << "an unchecked control must assert its state; silence cannot authorize "
         "a state-setting toggle";

  auto command = mojom::RendererActionCommand::New();
  command->schema_version = "0.11";
  command->command_id = "toggle-gift";
  command->frame_id = "frame-main";
  command->page_epoch = page_epoch();
  command->required_graph_revision = snapshot->snapshot->graph_revision;
  command->node_id = checkbox->node_id;
  command->operation = mojom::ActionType::kToggle;
  command->deadline_ms = 1000u;
  command->input = mojom::ActionInput::New();
  command->input->kind = mojom::ActionInputKind::kToggleState;
  command->input->sensitivity = checkbox->sensitivity;
  command->input->checked = true;

  auto role = mojom::Precondition::New();
  role->kind = mojom::PreconditionKind::kNodeRoleUnchanged;
  role->expected_role = checkbox->role;
  command->renderer_preconditions.push_back(std::move(role));
  auto action = mojom::Precondition::New();
  action->kind = mojom::PreconditionKind::kNodeActionAvailable;
  action->expected_action_type = mojom::ActionType::kToggle;
  command->renderer_preconditions.push_back(std::move(action));
  auto sensitivity = mojom::Precondition::New();
  sensitivity->kind = mojom::PreconditionKind::kNotSensitiveField;
  sensitivity->max_sensitivity = checkbox->sensitivity;
  command->renderer_preconditions.push_back(std::move(sensitivity));

  mojom::RendererActionResultPtr toggled = Execute(std::move(command));
  ASSERT_TRUE(toggled);
  ASSERT_EQ(toggled->outcome, mojom::RendererActionOutcome::kDispatched)
      << "failed precondition="
      << (toggled->failed_precondition.has_value()
              ? static_cast<int>(*toggled->failed_precondition)
              : -1);

  mojom::ResolveNodeRequestPtr resolve =
      ResolveRequest(checkbox->node_id, page_epoch());
  resolve->required_graph_revision = toggled->observed_at_revision;
  mojom::ResolveNodeResultPtr resolved = Resolve(std::move(resolve));
  ASSERT_TRUE(resolved && resolved->node);
  EXPECT_GT(resolved->node->observed_at_revision,
            toggled->observed_at_revision);
  EXPECT_NE(
      std::ranges::find(resolved->node->states, mojom::NodeState::kChecked),
      resolved->node->states.end());
  EXPECT_EQ(
      std::ranges::find(resolved->node->states, mojom::NodeState::kUnchecked),
      resolved->node->states.end());
}

}  // namespace
}  // namespace taffy::test
