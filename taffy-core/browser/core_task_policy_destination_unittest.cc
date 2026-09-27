// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_task_policy_destination.h"

#include <optional>
#include <string>

#include "taffy/browser/core_task_policy_test_support.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

using core_task_policy_test::Effect;

std::optional<std::string> NoSearch(const std::string&, const std::string&) {
  return std::nullopt;
}

mojom::TaskPolicyEffectPtr Navigate(std::string address) {
  auto effect = Effect();
  effect->action_class = mojom::PolicyActionClass::kOpenLink;
  effect->operation_kind = mojom::TaskActionOperationKind::kNavigate;
  effect->tool_name = "browser.navigate";
  effect->context_risk = mojom::PolicyRiskClass::kReversibleDisclosure;
  effect->destination_address = std::move(address);
  return effect;
}

// The typed address is a lead, and the destination the browser judges it by
// is the address's own origin — not the document's — so a navigate may leave
// the site it is on. History and reload carry no destination at all.
TEST(CoreTaskPolicyDestinationTest, ANavigateCarriesTheTypedAddressAsItsOwn) {
  EXPECT_TRUE(TaskPolicyOperationCarriesDestination(
      mojom::TaskActionOperationKind::kNavigate));
  EXPECT_FALSE(TaskPolicyOperationCarriesDestination(
      mojom::TaskActionOperationKind::kHistoryBack));
  EXPECT_FALSE(TaskPolicyOperationCarriesDestination(
      mojom::TaskActionOperationKind::kReload));

  const auto same_origin = ResolveTaskPolicyDestination(
      nullptr, *Navigate("https://example.test/next"), nullptr, NoSearch);
  ASSERT_TRUE(same_origin.has_value());
  EXPECT_EQ("https://example.test/next", same_origin->address);
  EXPECT_EQ("https://example.test", same_origin->origin);

  const auto new_origin = ResolveTaskPolicyDestination(
      nullptr, *Navigate("https://myaadhaar.uidai.gov.in/"), nullptr, NoSearch);
  ASSERT_TRUE(new_origin.has_value());
  EXPECT_EQ("https://myaadhaar.uidai.gov.in/", new_origin->address);
  EXPECT_EQ("https://myaadhaar.uidai.gov.in", new_origin->origin);

  // A model names a site the way a person says it, with no trailing slash.
  // Every address in this suite used to carry a path or a slash, so the
  // spelling rule that refused this one was never exercised here — and on a
  // phone it refused `https://openally.ai` before policy was asked.
  const auto bare = ResolveTaskPolicyDestination(
      nullptr, *Navigate("https://openally.ai"), nullptr, NoSearch);
  ASSERT_TRUE(bare.has_value());
  EXPECT_EQ("https://openally.ai", bare->address);
  EXPECT_EQ("https://openally.ai", bare->origin);
}

// The one spelling difference admitted is an address that names an origin and
// nothing else. Anything else the caller did not spell the way GURL does is
// still refused, because an address that does not say where it resolves to is
// what the rule exists to catch.
TEST(CoreTaskPolicyDestinationTest, OnlyTheOriginOnlySpellingIsForgiven) {
  for (const char* address : {"https://example.test/a b", "https://EXAMPLE.test",
                              "https://example.test:443/", "https://example.test/../x"}) {
    const auto resolved = ResolveTaskPolicyDestination(
        nullptr, *Navigate(address), nullptr, NoSearch);
    EXPECT_FALSE(resolved.has_value()) << address;
  }
}

// The bound a download already carries: the model's words become a request
// on the wire, and only https carries them. A refusal here is a decision
// about the proposal with the code the action is settled under.
TEST(CoreTaskPolicyDestinationTest,
     ANavigateOutsideHttpsIsDeniedAsEgressNotAuthorized) {
  const auto http = ResolveTaskPolicyDestination(
      nullptr, *Navigate("http://example.test/next"), nullptr, NoSearch);
  ASSERT_FALSE(http.has_value());
  EXPECT_EQ(mojom::TaskActionResultCode::kEgressNotAuthorized, http.error());

  const auto scheme = ResolveTaskPolicyDestination(
      nullptr, *Navigate("javascript:alert(1)"), nullptr, NoSearch);
  ASSERT_FALSE(scheme.has_value());
  EXPECT_EQ(mojom::TaskActionResultCode::kEgressNotAuthorized, scheme.error());

  auto missing = Navigate("https://example.test/next");
  missing->destination_address.reset();
  const auto absent =
      ResolveTaskPolicyDestination(nullptr, *missing, nullptr, NoSearch);
  ASSERT_FALSE(absent.has_value());
  EXPECT_EQ(mojom::TaskActionResultCode::kEgressNotAuthorized, absent.error());
}

// A search with no configured engine is not a destination the model can be
// told to fix, and says so with its own code rather than the egress one.
TEST(CoreTaskPolicyDestinationTest, ASearchWithNoEngineIsUnsupported) {
  auto effect = Effect();
  effect->action_class = mojom::PolicyActionClass::kOpenLink;
  effect->operation_kind = mojom::TaskActionOperationKind::kSearch;
  effect->tool_name = "browser.search";
  effect->transient_search_query = "official site";
  const auto resolved =
      ResolveTaskPolicyDestination(nullptr, *effect, nullptr, NoSearch);
  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(mojom::TaskActionResultCode::kUnsupported, resolved.error());
}

// The live document a search results page presents, and one link handle read
// from it.
TaskPolicyDocumentContext Live(uint64_t graph_revision) {
  return TaskPolicyDocumentContext{.tab_id = "tab-1",
                                   .frame_id = "frame-1",
                                   .page_epoch = "epoch-1",
                                   .origin = "https://example.test",
                                   .graph_revision = graph_revision};
}

mojom::TaskPolicyEffectPtr LinkOpen(uint8_t handle_revision) {
  auto effect = Effect();
  effect->action_class = mojom::PolicyActionClass::kOpenLink;
  effect->operation_kind = mojom::TaskActionOperationKind::kLinkOpen;
  effect->tool_name = "browser.link.open";
  effect->context_risk = mojom::PolicyRiskClass::kReversibleDisclosure;
  effect->node_id = "node-1";
  effect->canonical_intent =
      core_task_policy_test::LinkOpenIntent("node-1", handle_revision);
  return effect;
}

// A results page mutates between the reading a model saw and the tap it asks
// for, so a link handle is ordinarily older than the live graph revision. This
// clause required the two to be *equal*, which is a third copy of a rule the
// observed-link registry owns and the strictest of the three, so it refused
// before the one that owns it was asked. On a phone that was
// `[taffy_link_open_refused] at=revision` on the one move an errand has for
// following a search result, twice in one run (decisions 0156, 0182).
//
// A handle from a revision the browser has never reported is still refused:
// that is a claim about a future, not a stale read.
TEST(CoreTaskPolicyDestinationTest, ALinkHandleMayBeOlderThanTheLivePage) {
  const TaskPolicyDocumentContext live = Live(20u);

  // Older and equal both get past this gate and are answered by the registry,
  // which no unit test has a browser context for — `kNodeGone` is the registry
  // saying so, and it is not `kStaleGraph`.
  for (uint8_t revision : {uint8_t{12u}, uint8_t{20u}}) {
    const auto resolved = ResolveTaskPolicyDestination(
        nullptr, *LinkOpen(revision), &live, NoSearch);
    ASSERT_FALSE(resolved.has_value()) << static_cast<int>(revision);
    EXPECT_EQ(mojom::TaskActionResultCode::kNodeGone, resolved.error())
        << static_cast<int>(revision);
  }

  const auto future =
      ResolveTaskPolicyDestination(nullptr, *LinkOpen(21u), &live, NoSearch);
  ASSERT_FALSE(future.has_value());
  EXPECT_EQ(mojom::TaskActionResultCode::kStaleGraph, future.error());
}

// Everything else about the live document is still exact: a handle read in a
// different document is not a stale read of this one.
TEST(CoreTaskPolicyDestinationTest, ALinkHandleIsExactAboutWhichDocument) {
  TaskPolicyDocumentContext moved = Live(20u);
  moved.page_epoch = "epoch-2";
  const auto epoch =
      ResolveTaskPolicyDestination(nullptr, *LinkOpen(12u), &moved, NoSearch);
  ASSERT_FALSE(epoch.has_value());
  EXPECT_EQ(mojom::TaskActionResultCode::kStaleGraph, epoch.error());

  TaskPolicyDocumentContext elsewhere = Live(20u);
  elsewhere.origin = "https://attacker.test";
  const auto origin = ResolveTaskPolicyDestination(
      nullptr, *LinkOpen(12u), &elsewhere, NoSearch);
  ASSERT_FALSE(origin.has_value());
  EXPECT_EQ(mojom::TaskActionResultCode::kStaleGraph, origin.error());

  const auto no_document =
      ResolveTaskPolicyDestination(nullptr, *LinkOpen(12u), nullptr, NoSearch);
  ASSERT_FALSE(no_document.has_value());
  EXPECT_EQ(mojom::TaskActionResultCode::kStaleGraph, no_document.error());
}

// A site is free to answer on a sibling host of its own registrable domain,
// and that is the site the task opened. Anything else is a different source.
TEST(CoreTaskPolicyDestinationTest, ASourceFollowsItsOwnSite) {
  EXPECT_TRUE(TaskSourceOriginStillNamesTheSite("https://example.test",
                                                "https://example.test"));
  EXPECT_TRUE(TaskSourceOriginStillNamesTheSite("https://myaadhaar.uidai.test",
                                                "https://beta.uidai.test"));
  EXPECT_TRUE(TaskSourceOriginStillNamesTheSite("https://uidai.test",
                                                "https://www.uidai.test"));

  // Another registrable domain, a port that moved, an http downgrade, and a
  // shared pages registry where two hosts are deliberately separate sites.
  EXPECT_FALSE(TaskSourceOriginStillNamesTheSite("https://example.test",
                                                 "https://attacker.test"));
  EXPECT_FALSE(TaskSourceOriginStillNamesTheSite("https://example.test",
                                                 "https://example.test:8443"));
  EXPECT_FALSE(TaskSourceOriginStillNamesTheSite("https://example.test",
                                                 "http://example.test"));
  EXPECT_FALSE(TaskSourceOriginStillNamesTheSite("http://example.test",
                                                 "http://example.test:99"));
  EXPECT_FALSE(TaskSourceOriginStillNamesTheSite("https://one.github.io",
                                                 "https://two.github.io"));
  EXPECT_FALSE(TaskSourceOriginStillNamesTheSite("", "https://example.test"));
  EXPECT_FALSE(TaskSourceOriginStillNamesTheSite("https://example.test", ""));
}

}  // namespace
}  // namespace taffy
