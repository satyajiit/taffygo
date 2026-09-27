// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <set>
#include <string>
#include <utility>

#include "taffy/renderer/snapshot_form_observation_root.h"
#include "taffy/renderer/test/endpoint_test_harness.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::test {
namespace {

class FormIdentityTest : public EndpointTestHarness {};

TEST_F(FormIdentityTest,
       ExactFormOwnershipDisambiguatesAccessibilityAndDomContainment) {
  // The input is physically nested under one form but explicitly owned by a
  // different form. Its AX parent and immediate DOM parent are both plausible
  // generic CONTAINS predecessors; neither is authority for a SECTION root.
  // Blink's form-owner relation is the only exact answer.
  LoadAndBind(
      "<html><body>"
      "<form id='visual'><fieldset><legend>Visual group</legend>"
      "<label for='city'>Destination city</label>"
      "<input id='city' name='city' form='owner' type='text' "
      "autocomplete='given-name'>"
      "</fieldset></form>"
      "<form id='owner'></form>"
      "</body></html>");
  // Match the browser's two-stage form workflow exactly. Its discovery read
  // intentionally omits layout; the narrower SECTION read asks for layout so
  // it can authorize a later write against current bounds.
  mojom::SnapshotRequestPtr document_request = DocumentRequest();
  document_request->observed_frame_id = document_request->root_frame_id;
  std::erase_if(document_request->adapters,
                [](const mojom::AdapterRequirementPtr& requirement) {
                  return requirement->adapter == mojom::AdapterKind::kLayout;
                });
  mojom::SnapshotResultPtr document = Snapshot(std::move(document_request));
  ASSERT_TRUE(document && document->snapshot);

  const mojom::SemanticNode* field = nullptr;
  for (const mojom::SemanticNodePtr& node : document->snapshot->nodes) {
    if (node->name.value_or(std::string()) == "Destination city" &&
        std::ranges::find(node->actions, mojom::ActionType::kSetText) !=
            node->actions.end()) {
      field = node.get();
      break;
    }
  }
  ASSERT_TRUE(field);

  std::set<std::string> equivalent_ids = {field->node_id};
  for (const mojom::SemanticEdgePtr& edge : document->snapshot->edges) {
    if (edge->relationship != mojom::RelationshipKind::kSameEntityAs) {
      continue;
    }
    if (edge->from_node_id == field->node_id) {
      equivalent_ids.insert(edge->to_node_id);
    }
    if (edge->to_node_id == field->node_id) {
      equivalent_ids.insert(edge->from_node_id);
    }
  }

  std::set<std::string> generic_parents;
  std::set<std::string> exact_owners;
  for (const mojom::SemanticEdgePtr& edge : document->snapshot->edges) {
    if (!equivalent_ids.contains(edge->to_node_id)) {
      continue;
    }
    if (edge->relationship == mojom::RelationshipKind::kContains) {
      generic_parents.insert(edge->from_node_id);
    }
    if (edge->relationship == mojom::RelationshipKind::kOwns) {
      exact_owners.insert(edge->from_node_id);
    }
  }
  EXPECT_GT(generic_parents.size(), 1u)
      << "the adversarial fixture no longer exposes ambiguous containment";
  ASSERT_EQ(exact_owners.size(), 1u)
      << "one WebFormControlElement must have one exact WebFormElement owner";

  mojom::SnapshotRequestPtr section =
      RequestWithScope(mojom::ObservationScope::kSection);
  section->observed_frame_id = section->root_frame_id;
  section->adapters.clear();
  const auto require_adapter = [&section](
                                   mojom::AdapterKind kind,
                                   mojom::AdapterRequirementLevel level) {
    auto requirement = mojom::AdapterRequirement::New();
    requirement->adapter = kind;
    requirement->requirement = level;
    section->adapters.push_back(std::move(requirement));
  };
  require_adapter(mojom::AdapterKind::kDom,
                  mojom::AdapterRequirementLevel::kOptional);
  require_adapter(mojom::AdapterKind::kForms,
                  mojom::AdapterRequirementLevel::kRequired);
  require_adapter(mojom::AdapterKind::kLayout,
                  mojom::AdapterRequirementLevel::kOptional);
  section->form_root = mojom::FormObservationRoot::New();
  section->form_root->node_id = *exact_owners.begin();
  section->form_root->minimum_graph_revision =
      document->snapshot->graph_revision;
  mojom::SnapshotResultPtr observed = Snapshot(std::move(section));
  ASSERT_TRUE(observed);
  std::string refusal_details;
  for (const mojom::SnapshotWarningPtr& warning : observed->warnings) {
    if (!refusal_details.empty()) {
      refusal_details.append(", ");
    }
    refusal_details.append(
        warning->detail_code.value_or("warning-without-detail"));
  }
  EXPECT_NE(observed->code, mojom::ObservationResultCode::kUnsupported)
      << "SECTION refusal details: " << refusal_details;
  ASSERT_TRUE(observed->snapshot);
  ASSERT_TRUE(observed->snapshot->form_root);
  EXPECT_EQ(observed->snapshot->form_root->node_id, *exact_owners.begin());

  std::set<std::string> owned_control_ids;
  for (const mojom::SemanticEdgePtr& edge : observed->snapshot->edges) {
    if (edge->relationship == mojom::RelationshipKind::kOwns &&
        edge->from_node_id == *exact_owners.begin()) {
      owned_control_ids.insert(edge->to_node_id);
    }
  }
  ASSERT_EQ(owned_control_ids.size(), 1u);
  const std::string& exact_control_id = *owned_control_ids.begin();
  const auto exact_control = std::ranges::find_if(
      observed->snapshot->nodes, [&](const mojom::SemanticNodePtr& node) {
        return node->node_id == exact_control_id;
      });
  ASSERT_NE(exact_control, observed->snapshot->nodes.end());
  EXPECT_EQ((*exact_control)->sensitivity, mojom::Sensitivity::kPersonal)
      << "the fixture must exercise a label the browser withholds on the "
         "sensitive DOM control";
  EXPECT_NE(
      std::ranges::find((*exact_control)->actions, mojom::ActionType::kSetText),
      (*exact_control)->actions.end());

  std::set<std::string> exact_ax_ids;
  for (const mojom::SemanticEdgePtr& edge : observed->snapshot->edges) {
    if (edge->relationship != mojom::RelationshipKind::kSameEntityAs) {
      continue;
    }
    if (edge->from_node_id == exact_control_id) {
      exact_ax_ids.insert(edge->to_node_id);
    } else if (edge->to_node_id == exact_control_id) {
      exact_ax_ids.insert(edge->from_node_id);
    }
  }
  ASSERT_EQ(exact_ax_ids.size(), 1u)
      << "SECTION must retain only the exact accessibility identity joined "
         "to its DOM action target";
  const auto label_witness = std::ranges::find_if(
      observed->snapshot->nodes, [&](const mojom::SemanticNodePtr& node) {
        return node->node_id == *exact_ax_ids.begin();
      });
  ASSERT_NE(label_witness, observed->snapshot->nodes.end());
  EXPECT_EQ((*label_witness)->name.value_or(std::string()), "Destination city");
  EXPECT_EQ((*label_witness)->sensitivity, mojom::Sensitivity::kNotSensitive);
  EXPECT_TRUE((*label_witness)->actions.empty())
      << "the safe label witness must never become an action target";
}

TEST_F(FormIdentityTest,
       FinalFormRootValidationRejectsReplacementAfterAdmission) {
  LoadAndBind(
      "<html><body><form id='owner'>"
      "<label for='city'>City</label><input id='city' name='city'>"
      "</form></body></html>");
  mojom::SnapshotResultPtr document = Snapshot(DocumentRequest());
  ASSERT_TRUE(document && document->snapshot);

  std::set<std::string> exact_owners;
  for (const mojom::SemanticEdgePtr& edge : document->snapshot->edges) {
    if (edge->relationship == mojom::RelationshipKind::kOwns) {
      exact_owners.insert(edge->from_node_id);
    }
  }
  ASSERT_EQ(exact_owners.size(), 1u);

  mojom::SnapshotRequestPtr section =
      RequestWithScope(mojom::ObservationScope::kSection);
  section->form_root = mojom::FormObservationRoot::New();
  section->form_root->node_id = *exact_owners.begin();
  section->form_root->minimum_graph_revision =
      document->snapshot->graph_revision;
  ASSERT_EQ(ValidateFormObservationRoot(
                *section, GetMainFrame(), *endpoint()->store_for_testing(),
                ObservationRootValidationPhase::kAdmission),
            FormObservationRootStatus::kOk);

  // A same-shaped replacement with the same authored id is still a different
  // DOM identity. The final pass must never rediscover it by selector or
  // broaden the old browser-issued handle to the document.
  ExecuteJavaScriptForTests(
      "document.getElementById('owner').outerHTML = "
      "'<form id=\"owner\"><input id=\"city\" name=\"city\"></form>'; ");
  EXPECT_NE(ValidateFormObservationRoot(
                *section, GetMainFrame(), *endpoint()->store_for_testing(),
                ObservationRootValidationPhase::kAfterCollection),
            FormObservationRootStatus::kOk);
}

}  // namespace
}  // namespace taffy::test
