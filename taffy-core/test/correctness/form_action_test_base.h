// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_CORRECTNESS_FORM_ACTION_TEST_BASE_H_
#define TAFFY_TEST_CORRECTNESS_FORM_ACTION_TEST_BASE_H_

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
#include "taffy/test/support/bip_graph_payload_reader.h"
#include "taffy/test/support/taffy_observation_test_base.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::test {

// The live write path's fixture: a node the renderer really observed, the
// browser's one-use capability and value vault, and a verified page change.
// Shared by the form action suites, which split along what they are about.
class FormActionTestBase : public TaffyObservationTestBase {
 public:
  // The production classifier refuses every otherwise ordinary control on an
  // insecure document. A success-path suite must therefore use the corpus's
  // real TLS mode; running it over the default HTTP origin would be asserting
  // that the product bypasses its own sensitivity floor.
  FormActionTestBase();
  ~FormActionTestBase() override;

  void SetUpOnMainThread() override;
  void TearDownOnMainThread() override;

 protected:
  GraphPayload GraphOf(const ObservationEnvelope& envelope) {
    const std::optional<GraphPayload> graph =
        ReadGraphPayload(envelope.graph_payload);
    EXPECT_TRUE(graph.has_value())
        << "The live graph payload did not decode, so no renderer-issued node "
           "can be targeted honestly. Result code="
        << static_cast<int>(envelope.code)
        << ", encoding=" << static_cast<int>(envelope.encoding)
        << ", declared nodes=" << envelope.node_count
        << ", graph bytes=" << envelope.graph_payload.size()
        << ", first framing byte="
        << (envelope.graph_payload.empty()
                ? -1
                : static_cast<int>(envelope.graph_payload.front()));
    return graph.value_or(GraphPayload());
  }

  ObservationEnvelope ObserveCurrentDocument() {
    return client().Observe(
        builder().Observation(MainFrameId(), ObservationScope::kDocument));
  }

  std::optional<GraphPayloadNode> NodeNamedForAction(
      const ObservationEnvelope& envelope,
      std::string_view name_fragment,
      mojom::ActionType action) {
    const uint16_t wire_action = static_cast<uint16_t>(action);
    const GraphPayload graph = GraphOf(envelope);
    const auto node_with_id =
        [&](std::string_view node_id) -> const GraphPayloadNode* {
      const auto node = std::ranges::find_if(
          graph.nodes, [node_id](const GraphPayloadNode& candidate) {
            return candidate.node_id == node_id;
          });
      return node == graph.nodes.end() ? nullptr : &*node;
    };
    const auto has_name = [name_fragment](const GraphPayloadNode* node) {
      return node && node->name.find(name_fragment) != std::string::npos;
    };
    const auto has_action = [wire_action](const GraphPayloadNode* node) {
      return node && std::ranges::find(node->actions, wire_action) !=
                         node->actions.end();
    };
    const uint16_t same_entity_as =
        static_cast<uint16_t>(mojom::RelationshipKind::kSameEntityAs);
    const uint16_t owns = static_cast<uint16_t>(mojom::RelationshipKind::kOwns);

    std::set<std::string> owned_control_ids;
    for (const GraphPayloadEdge& edge : graph.edges) {
      if (edge.relationship == owns) {
        owned_control_ids.insert(edge.to_node_id);
      }
    }

    // A form action must use the DOM control identity that Blink assigned to
    // a form. The AX identity commonly carries the first matching accessible
    // name and action, but generic graph order cannot turn it into the exact
    // form control. Accept a directly named DOM control or reach it through
    // Blink's SAME_ENTITY_AS join, then require the independent OWNS proof.
    std::set<std::string> exact_matches;
    for (const GraphPayloadNode& node : graph.nodes) {
      if (has_name(&node) && has_action(&node) &&
          owned_control_ids.contains(node.node_id)) {
        exact_matches.insert(node.node_id);
      }
    }
    for (const GraphPayloadEdge& edge : graph.edges) {
      if (edge.relationship != same_entity_as) {
        continue;
      }
      const GraphPayloadNode* from = node_with_id(edge.from_node_id);
      const GraphPayloadNode* to = node_with_id(edge.to_node_id);
      if (has_name(from) && has_action(to) &&
          owned_control_ids.contains(to->node_id)) {
        exact_matches.insert(to->node_id);
      }
      if (has_name(to) && has_action(from) &&
          owned_control_ids.contains(from->node_id)) {
        exact_matches.insert(from->node_id);
      }
    }

    if (exact_matches.size() != 1u) {
      ADD_FAILURE() << "The live node named like '" << name_fragment
                    << "' and advertising action " << wire_action << " had "
                    << exact_matches.size()
                    << " exact DOM form-control identities after "
                       "SAME_ENTITY_AS and OWNS validation; exactly one is "
                       "required.";
      return std::nullopt;
    }
    const GraphPayloadNode* exact = node_with_id(*exact_matches.begin());
    if (!exact) {
      ADD_FAILURE() << "The exact form-control identity disappeared from the "
                       "same bounded graph payload.";
      return std::nullopt;
    }
    return *exact;
  }

  std::optional<FormObservationRoot> FormRootContaining(
      const ObservationEnvelope& envelope,
      std::string_view control_name,
      mojom::ActionType action) {
    const std::optional<GraphPayloadNode> control =
        NodeNamedForAction(envelope, control_name, action);
    if (!control) {
      return std::nullopt;
    }

    const GraphPayload graph = GraphOf(envelope);
    const uint16_t same_entity_as =
        static_cast<uint16_t>(mojom::RelationshipKind::kSameEntityAs);
    const uint16_t owns = static_cast<uint16_t>(mojom::RelationshipKind::kOwns);

    // The standard accessible name and action often live on the AX identity,
    // while an exact form root is necessarily a DOM identity. Follow only
    // Blink-authored identity joins to collect the exact control identities;
    // never guess from document position or generic containment.
    std::set<std::string> exact_control_ids = {control->node_id};
    std::vector<std::string> pending = {control->node_id};
    for (size_t index = 0; index < pending.size(); ++index) {
      for (const GraphPayloadEdge& edge : graph.edges) {
        if (edge.relationship != same_entity_as) {
          continue;
        }
        std::string other;
        if (edge.from_node_id == pending[index]) {
          other = edge.to_node_id;
        } else if (edge.to_node_id == pending[index]) {
          other = edge.from_node_id;
        }
        if (!other.empty() && exact_control_ids.insert(other).second) {
          pending.push_back(std::move(other));
        }
      }
    }

    std::set<std::string> form_ids;
    for (const GraphPayloadEdge& edge : graph.edges) {
      if (edge.relationship == owns &&
          exact_control_ids.contains(edge.to_node_id)) {
        form_ids.insert(edge.from_node_id);
      }
    }
    if (form_ids.size() != 1u) {
      ADD_FAILURE() << "The live control named like '" << control_name
                    << "' had " << form_ids.size()
                    << " exact OWNS form roots after SAME_ENTITY_AS joined "
                       "its AX and DOM identities; exactly one is required.";
      return std::nullopt;
    }
    return FormObservationRoot{
        .node_id = SemanticNodeId{*form_ids.begin()},
        .minimum_graph_revision = envelope.graph_revision,
    };
  }

  ObservationEnvelope ObserveForm(const ObservationEnvelope& prior,
                                  std::optional<FormObservationRoot> root) {
    EXPECT_TRUE(root.has_value())
        << "An exact form observation cannot be widened to the document when "
           "its root is unavailable.";
    if (!root) {
      return ObservationEnvelope();
    }
    root->minimum_graph_revision = prior.graph_revision;
    ObservationRequest request =
        builder().FormObservation(MainFrameId(), std::move(*root));
    request.expected_page_epoch = prior.page_epoch;
    request.allowed_origins = {MainFrameOrigin()};
    return client().Observe(std::move(request));
  }

  ObservationEnvelope ObserveFormContaining(const ObservationEnvelope& document,
                                            std::string_view control_name,
                                            mojom::ActionType action) {
    return ObserveForm(document,
                       FormRootContaining(document, control_name, action));
  }

  ObservationEnvelope RefreshForm(const ObservationEnvelope& prior) {
    return ObserveForm(prior, prior.form_root);
  }

  NodeHandle HandleFor(const ObservationEnvelope& envelope,
                       const GraphPayloadNode& node) {
    NodeHandle handle;
    handle.tab_id = broker()->tab_id();
    handle.frame_id = FrameId{node.frame_id};
    handle.page_epoch = envelope.page_epoch;
    handle.graph_revision = envelope.graph_revision;
    handle.node_id = SemanticNodeId{node.node_id};
    handle.expected_origin = MainFrameOrigin();
    return handle;
  }

  ValueReference MintValue(Sensitivity sensitivity, std::string value) {
    const std::optional<FillClearance> clearance =
        FillClearance::For(sensitivity);
    EXPECT_TRUE(clearance.has_value());
    if (!clearance) {
      return ValueReference();
    }
    return values_.Mint(builder().task_id(), *clearance, std::move(value),
                        base::TimeTicks::Now() + base::Seconds(15));
  }

  AuthorizedActionEnvelope Action(const ObservationEnvelope& envelope,
                                  const GraphPayloadNode& node,
                                  ActionType action_type,
                                  ActionInput input,
                                  Postcondition postcondition) {
    AuthorizedActionEnvelope action =
        builder().Activate(HandleFor(envelope, node), envelope.graph_revision,
                           {std::move(postcondition)});
    action.action_type = action_type;
    action.input = std::move(input);
    action.idempotency_policy = action_type == ActionType::kSubmitForm
                                    ? IdempotencyPolicy::kNonIdempotent
                                    : IdempotencyPolicy::kIdempotentWrite;
    EXPECT_TRUE(RegisterActionGrant(&action))
        << "The exact live-node capability was not registered.";
    return action;
  }

  static Postcondition ValueChanged() {
    Postcondition postcondition;
    postcondition.kind = PostconditionKind::kNodeValueChanged;
    postcondition.timeout_ms = 3000u;
    return postcondition;
  }

  static Postcondition BecameChecked() {
    Postcondition postcondition;
    postcondition.kind = PostconditionKind::kNodeStateChanged;
    postcondition.expected_node_state = NodeState::kChecked;
    postcondition.timeout_ms = 3000u;
    return postcondition;
  }

  Postcondition CommittedOnFixtureOrigin() {
    Postcondition postcondition;
    postcondition.kind = PostconditionKind::kCommittedNavigation;
    postcondition.allowed_origins.push_back(MainFrameOrigin());
    postcondition.timeout_ms = 5000u;
    return postcondition;
  }

  ValueReferenceVault values_;
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_CORRECTNESS_FORM_ACTION_TEST_BASE_H_
