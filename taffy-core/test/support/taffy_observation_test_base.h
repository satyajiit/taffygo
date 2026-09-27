// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_SUPPORT_TAFFY_OBSERVATION_TEST_BASE_H_
#define TAFFY_TEST_SUPPORT_TAFFY_OBSERVATION_TEST_BASE_H_

#include <string_view>
#include <vector>

namespace base {
class CommandLine;
}  // namespace base

#include "taffy/common/public/bip_identity.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/test/support/taffy_browser_test_base.h"

// The fixture for every suite whose subject is what an observation contains.
//
// It adds one thing to TaffyBrowserTestBase and it is not convenience: it
// refuses to run when no renderer endpoint is bound. Without that refusal a
// correctness test in the wrong binary reports every observation as unsupported
// and passes, because "the result was not an error" is true of a refusal. The
// failure names which binary has the endpoint and what to read to bind one
// elsewhere.
//
// It also opens a document-scope grant on the page's own origin. Narrow on
// purpose: a suite that needs child frames or a second origin widens it
// explicitly, so a frame-policy assertion can never be satisfied by a grant
// somebody widened for an unrelated test.

namespace taffy::test {

class TaffyObservationTestBase : public TaffyBrowserTestBase {
 public:
  explicit TaffyObservationTestBase(FixtureOriginMap::Scheme fixture_scheme =
                                        FixtureOriginMap::Scheme::kHttp);
  ~TaffyObservationTestBase() override;

  void SetUpCommandLine(base::CommandLine* command_line) override;
  void SetUpOnMainThread() override;

 protected:
  // Navigates to a corpus fixture, opens the grant for its origin, and returns
  // a document-scope observation of the main frame. The form almost every test
  // in these suites starts with.
  //
  // The grant is registered against the builder's current task, and a
  // replacement grant may only narrow. Calling this twice for pages on
  // different origins therefore needs BeginNewTask() in between: without it the
  // second grant is a widening, the service refuses it, and the second page is
  // observed under the first page's grant. Two readings are two tasks in the
  // shipping browser for precisely this reason.
  ObservationEnvelope ObserveFixture(std::string_view fixture_id);

  // The same, with child frames included and the named additional origins
  // admitted. Every cross-origin case has to say which origins it admits,
  // because an empty allowlist admits same-origin children and nothing else.
  ObservationEnvelope ObserveFixtureWithFrames(
      std::string_view fixture_id,
      const std::vector<std::string>& additional_origin_keys);

  // The frame summary for one frame identifier inside an envelope, or null.
  static const FrameSummary* FrameIn(const ObservationEnvelope& envelope,
                                     const FrameId& frame_id);
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_SUPPORT_TAFFY_OBSERVATION_TEST_BASE_H_
