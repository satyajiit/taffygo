// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/dom_mutation_signal_source.h"

#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

// What a DOM mutation means, decided without a document.
//
// blink::WebDomMutation is a plain struct, so the whole classification is
// testable here rather than only on a device - which matters because the
// interesting failures are all wrong-class rather than crash: a destination
// change reported as a cosmetic one is dropped first under backpressure, and
// a removal reported as a change leaves a retired handle resolvable.

namespace taffy {
namespace {

using Kind = blink::WebDomMutation::Kind;
using ChangeClass = SemanticGraphStore::ChangeClass;

blink::WebDomMutation ChildListMutation(int target,
                                        std::vector<int> added,
                                        std::vector<int> removed) {
  blink::WebDomMutation mutation;
  mutation.kind = Kind::kChildList;
  mutation.target_dom_node_id = target;
  mutation.added_dom_node_ids = std::move(added);
  mutation.removed_dom_node_ids = std::move(removed);
  return mutation;
}

blink::WebDomMutation AttributeMutation(int target, const char* name) {
  blink::WebDomMutation mutation;
  mutation.kind = Kind::kAttributes;
  mutation.target_dom_node_id = target;
  mutation.attribute_name = blink::WebString::FromUtf8(name);
  return mutation;
}

TEST(DomMutationSignalSourceTest, AnInsertionIsAChangeAboutTheParent) {
  const DomMutationSignals signals =
      TranslateDomMutations({ChildListMutation(11, {12, 13}, {})});

  ASSERT_EQ(1u, signals.changes.size());
  EXPECT_EQ(ChangeClass::kNodeAdded, signals.changes[0].change);
  EXPECT_EQ(11, signals.changes[0].dom_node_id)
      << "An insertion was attributed to the inserted node. The inserted node "
         "has no identity in this graph, and naming it here would mint one "
         "outside the producing adapters.";
  EXPECT_TRUE(signals.removed_dom_node_ids.empty());
}

TEST(DomMutationSignalSourceTest, ARemovalIsReportedByIdentityOnly) {
  const DomMutationSignals signals =
      TranslateDomMutations({ChildListMutation(11, {}, {21, 22})});

  EXPECT_EQ(std::vector<int>({21, 22}), signals.removed_dom_node_ids);
  EXPECT_TRUE(signals.changes.empty())
      << "A removal produced a second, class-shaped signal. The store's "
         "retirement path is what says which ids actually died, and a node it "
         "never described invalidates nothing.";
}

TEST(DomMutationSignalSourceTest, AZeroNodeIdIsNeverReportedAsRemoved) {
  const DomMutationSignals signals =
      TranslateDomMutations({ChildListMutation(11, {}, {0})});

  EXPECT_TRUE(signals.removed_dom_node_ids.empty())
      << "Zero means Blink named no node, and retiring by zero would match "
         "whatever the store happens to hold under that key.";
}

TEST(DomMutationSignalSourceTest, CharacterDataIsAttributedToItsElement) {
  blink::WebDomMutation mutation;
  mutation.kind = Kind::kCharacterData;
  mutation.target_dom_node_id = 31;  // The text node.
  mutation.parent_dom_node_id = 30;  // The element it lives in.

  const DomMutationSignals signals = TranslateDomMutations({mutation});

  ASSERT_EQ(1u, signals.changes.size());
  EXPECT_EQ(ChangeClass::kAccessibleStateChanged, signals.changes[0].change);
  EXPECT_EQ(30, signals.changes[0].dom_node_id);
}

TEST(DomMutationSignalSourceTest, CharacterDataWithNoParentStillReports) {
  blink::WebDomMutation mutation;
  mutation.kind = Kind::kCharacterData;
  mutation.target_dom_node_id = 31;

  const DomMutationSignals signals = TranslateDomMutations({mutation});

  ASSERT_EQ(1u, signals.changes.size());
  EXPECT_EQ(31, signals.changes[0].dom_node_id)
      << "A change with no element to name is still a change. Dropping it "
         "would hide a revision advance the precondition check depends on.";
}

TEST(DomMutationSignalSourceTest, TheAttributeTableNamesItsClasses) {
  EXPECT_EQ(ChangeClass::kDestinationChanged, ChangeClassForAttribute("href"));
  EXPECT_EQ(ChangeClass::kDestinationChanged,
            ChangeClassForAttribute("formaction"));
  EXPECT_EQ(ChangeClass::kEnabledStateChanged,
            ChangeClassForAttribute("disabled"));
  EXPECT_EQ(ChangeClass::kVisibilityChanged, ChangeClassForAttribute("hidden"));
  EXPECT_EQ(ChangeClass::kVisibilityChanged, ChangeClassForAttribute("class"));
  EXPECT_EQ(ChangeClass::kRoleChanged, ChangeClassForAttribute("role"));
  EXPECT_EQ(ChangeClass::kFormStateChanged, ChangeClassForAttribute("checked"));
}

TEST(DomMutationSignalSourceTest, AnUnlistedAttributeIsNotAssumedCosmetic) {
  EXPECT_EQ(ChangeClass::kAccessibleStateChanged,
            ChangeClassForAttribute("data-taffy-unknown"))
      << "An unrecognised attribute was classified as something droppable. "
         "The conservative class is the one that survives backpressure.";
}

TEST(DomMutationSignalSourceTest, AnAttributeChangeNamesItsElement) {
  const DomMutationSignals signals =
      TranslateDomMutations({AttributeMutation(41, "href")});

  ASSERT_EQ(1u, signals.changes.size());
  EXPECT_EQ(ChangeClass::kDestinationChanged, signals.changes[0].change);
  EXPECT_EQ(41, signals.changes[0].dom_node_id);
}

TEST(DomMutationSignalSourceTest, ABatchKeepsEveryRecord) {
  const DomMutationSignals signals = TranslateDomMutations({
      ChildListMutation(11, {12}, {21}),
      AttributeMutation(41, "disabled"),
  });

  EXPECT_EQ(std::vector<int>({21}), signals.removed_dom_node_ids);
  ASSERT_EQ(2u, signals.changes.size());
  EXPECT_EQ(ChangeClass::kNodeAdded, signals.changes[0].change);
  EXPECT_EQ(ChangeClass::kEnabledStateChanged, signals.changes[1].change);
}

}  // namespace
}  // namespace taffy
