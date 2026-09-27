// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/support/taffy_observation_test_base.h"

#include <utility>

#include "base/functional/bind.h"
#include "content/public/test/test_utils.h"
#include "taffy/test/support/renderer_endpoint_requirement.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::test {

TaffyObservationTestBase::TaffyObservationTestBase(
    FixtureOriginMap::Scheme fixture_scheme)
    : TaffyBrowserTestBase(fixture_scheme) {}
TaffyObservationTestBase::~TaffyObservationTestBase() = default;

void TaffyObservationTestBase::SetUpCommandLine(
    base::CommandLine* command_line) {
  TaffyBrowserTestBase::SetUpCommandLine(command_line);

  // Every suite on this fixture makes claims about frames: which origins are
  // admitted, whose parent a frame reports, whether a granted frame still
  // withholds its secret. content_shell does not turn site isolation on by
  // itself, and on Android the default is the partial policy, so without this
  // a "cross-origin" child frame shares its parent's renderer process. The
  // origin-scoping assertions would still read as green - they are decided on
  // origin, not on process - while measuring a process model the product does
  // not have, and FramesTest.CrossOriginFramesAreOutOfProcess exists precisely
  // to refuse that.
  //
  // This is a property of the binary, so it is set for the binary rather than
  // asserted per test. It is upstream's own helper for exactly this, and it is
  // the configuration these suites already assume in their comments.
  content::IsolateAllSitesForTesting(command_line);
}

void TaffyObservationTestBase::SetUpOnMainThread() {
  TaffyBrowserTestBase::SetUpOnMainThread();

  // Armed here, run on the test's first scripted call - not run here.
  //
  // The requirement asks a document whether a renderer endpoint answers
  // negotiation. At this point the only document is content_shell's initial
  // about:blank, which is not an observable document at all: the broker
  // declines to create an endpoint for it, negotiation is refused before it
  // reaches a renderer, and the requirement fails for a reason that says
  // nothing about whether this binary has an endpoint. Every suite deriving
  // from this fixture failed that way, including the ones whose own
  // assertions then passed.
  //
  // Deferring it to the first scripted call asks the same question of the page
  // the test navigated to, which is the document its assertions are about. The
  // check itself is unchanged and still fatal: an unbound endpoint and a broken
  // one produce the same result code, and this failure is the only thing that
  // tells them apart.
  //
  // What the deferral costs, named so a future test does not lose it by
  // accident: a test that makes no scripted call never runs the requirement.
  // Three of the cases on this fixture are in that position today -
  // FramesTest.CrossOriginFramesAreOutOfProcess,
  // PopupAndTaskTabTest.AnUnactivatedPopupFollowsBrowserPolicy and
  // PromptInjectionTest.TheCollectionEndpointIsReachableWhenAsked - and for
  // each of them that is correct rather than merely tolerable: none touches
  // the BIP endpoint, so none would be measuring anything the requirement is
  // about. A new test here that also skips it is a visible change to this
  // list, not a silent one.
  client().SetPreflight(base::BindOnce(&RendererEndpointRequirement::Require,
                                       std::ref(client()), broker()->tab_id()));
}

ObservationEnvelope TaffyObservationTestBase::ObserveFixture(
    std::string_view fixture_id) {
  EXPECT_TRUE(NavigateToFixture(fixture_id));
  GrantObservation({MainFrameOrigin()}, /*may_include_child_frames=*/false);
  return client().Observe(
      builder().Observation(MainFrameId(), ObservationScope::kDocument));
}

ObservationEnvelope TaffyObservationTestBase::ObserveFixtureWithFrames(
    std::string_view fixture_id,
    const std::vector<std::string>& additional_origin_keys) {
  EXPECT_TRUE(NavigateToFixture(fixture_id));

  std::vector<Origin> allowed = {MainFrameOrigin()};
  for (const std::string& key : additional_origin_keys) {
    Origin origin;
    origin.kind = OriginKind::kTuple;
    origin.serialization = origins().OriginOf(key).Serialize();
    allowed.push_back(std::move(origin));
  }
  GrantObservation(allowed, /*may_include_child_frames=*/true);

  ObservationRequest request =
      builder().Observation(MainFrameId(), ObservationScope::kDocument);
  request.include_child_frames = true;
  for (const Origin& origin : allowed) {
    request.allowed_origins.push_back(origin);
  }
  return client().Observe(std::move(request));
}

// static
const FrameSummary* TaffyObservationTestBase::FrameIn(
    const ObservationEnvelope& envelope,
    const FrameId& frame_id) {
  for (const FrameSummary& frame : envelope.frames) {
    if (frame.frame_id == frame_id) {
      return &frame;
    }
  }
  return nullptr;
}

}  // namespace taffy::test
