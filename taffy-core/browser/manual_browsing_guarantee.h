// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_MANUAL_BROWSING_GUARANTEE_H_
#define TAFFY_BROWSER_MANUAL_BROWSING_GUARANTEE_H_

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "taffy/browser/ai_runtime_state.h"

// The test-visible assertion that manual browsing does not depend on the AI
// runtime (PAR-AI-BR-001, CAP-BR-001 through CAP-BR-005, REQ-BR-001, PRD
// section 5.1's last line).
//
// The requirement is easy to state and hard to keep. Every seam in this
// directory is written to work with the runtime absent, and each of them
// individually looks fine; the failure arrives later, when one call site adds
// a check for "is the assistant ready" to a path a person uses to open a link.
// A comment does not catch that. This class does, by making the property
// executable:
//
//   **Every registered capability must report the same status in every
//   AiRuntimeState, and that status must not be a failure.**
//
// The verification runs each probe once per state, with the process-wide
// availability actually set to that state. A probe that consulted the runtime
// — directly, or through some intermediary that did — produces different
// answers across the six states, and the difference is the finding. That is
// why the probe interface takes no arguments: a probe that was *told* the
// state could not accidentally read it, and would prove nothing.
//
// Probes for the pure seams are built in, because they can be exercised for
// real with no platform behind them: the address-bar classifier is asked to
// classify, the scrubber is asked to scrub, the posture evaluator is asked to
// judge. The seams with a platform delegate — tabs, downloads, navigation —
// register their probes from the layer that owns the delegate, which is also
// the layer that knows what "wired up" means for them.
//
// UI thread only.

namespace taffy {

enum class ManualCapability : uint8_t {
  kNavigation = 0,
  kTabsAndSessions = 1,
  kAddressBar = 2,
  kCredentialIsolation = 3,
  kDownloads = 4,
  kAndroidLifecycle = 5,
  kSecurityPosture = 6,
  kDiagnosticScrubbing = 7,
};

enum class ManualCapabilityStatus : uint8_t {
  // Works, and works correctly — the probe exercised it and checked the
  // answer.
  kOperational = 0,
  // The platform half is not registered. A deployment problem, not an AI
  // dependency: the status is still expected to be identical across every
  // runtime state, which is what this class is checking.
  kPlatformNotWired = 1,
  // The probe ran and the capability gave the wrong answer.
  kFailed = 2,
};

class ManualCapabilityProbe {
 public:
  virtual ~ManualCapabilityProbe() = default;

  // Exercises the capability and reports what happened. Takes no arguments on
  // purpose: see the class comment.
  virtual ManualCapabilityStatus Probe() = 0;
};

struct ManualCapabilityFinding {
  ManualCapability capability = ManualCapability::kNavigation;
  // The status observed with the runtime ready, and with it absent. Equal in a
  // correct build.
  ManualCapabilityStatus with_runtime_ready = ManualCapabilityStatus::kFailed;
  ManualCapabilityStatus with_runtime_absent = ManualCapabilityStatus::kFailed;
  // True when any two of the six states produced different answers.
  bool varies_with_runtime_state = false;
  // True when any state produced kFailed.
  bool failed_in_some_state = false;
};

struct ManualBrowsingVerdict {
  // True when nothing varied and nothing failed.
  bool manual_browsing_is_independent = false;
  // One entry per registered capability, in registration-independent order.
  std::vector<ManualCapabilityFinding> findings;
  size_t registered_capability_count = 0;
};

class ManualBrowsingGuarantee {
 public:
  // Never null. The built-in probes are registered on first use.
  static ManualBrowsingGuarantee& Get();

  ManualBrowsingGuarantee(const ManualBrowsingGuarantee&) = delete;
  ManualBrowsingGuarantee& operator=(const ManualBrowsingGuarantee&) = delete;

  // Null unregisters. Registering a second probe for a capability replaces the
  // first, so a test can substitute one without disturbing the rest.
  void Register(ManualCapability capability, ManualCapabilityProbe* probe);

  // Runs every registered probe once per AiRuntimeState. Restores the
  // availability value it found before returning, so calling this is not a
  // side effect on the running browser.
  ManualBrowsingVerdict Verify();

  size_t registered_count() const { return probes_.size(); }

 private:
  friend class base::NoDestructor<ManualBrowsingGuarantee>;

  ManualBrowsingGuarantee();
  ~ManualBrowsingGuarantee();

  std::map<ManualCapability, raw_ptr<ManualCapabilityProbe>> probes_;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_MANUAL_BROWSING_GUARANTEE_H_
