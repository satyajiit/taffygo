// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_ACCEPTED_APPROVAL_LEDGER_TEST_SUPPORT_H_
#define TAFFY_BROWSER_ACCEPTED_APPROVAL_LEDGER_TEST_SUPPORT_H_

#include <stdint.h>

#include <optional>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

class AcceptedApprovalLedger;
class CoreStateBindingRegistry;

// Completes the exact browser storage proof for an already-staged command.
bool CompleteStagedStorageCommitForTesting(
    AcceptedApprovalLedger* ledger,
    const core_service::mojom::CoreServiceCommand& command,
    uint64_t resulting_revision,
    uint64_t now_monotonic_ms);


// The accepted-consent fixture the ledger suites share: one web errand, its
// start command, the storage proof that commits it, and the durable binding a
// later service generation republishes. It lives here rather than in one
// suite's anonymous namespace because four files build the same errand, and
// because the suite that owned it had grown past the file cap around it.
namespace accepted_approval_ledger_test {

inline constexpr uint64_t kGeneration = 3u;
inline constexpr uint64_t kRestartGeneration = kGeneration + 1u;
inline constexpr uint64_t kInitialTaskRevision = 3u;
inline constexpr uint64_t kCommittedRevision = 8u;
inline constexpr uint64_t kNow = 10'000u;
inline constexpr uint64_t kNowUtc = 1'800'000'000'000u;
inline constexpr char kProfileId[] = "profile-1";
inline constexpr char kBrowserSessionId[] = "browser-session-1";
inline constexpr char kTaskId[] = "task-1";
inline constexpr char kTabId[] = "tab-1";
inline constexpr char kOrigin[] = "https://example.test";


core_service::mojom::CoreStateBrowserBindingsPtr State(
    std::optional<uint64_t> revision);
void Register(CoreStateBindingRegistry* registry,
              core_service::mojom::CoreStateBrowserBindingsPtr state);
void RestampGeneration(core_service::mojom::CoreStateBrowserBindings* state,
                       uint64_t generation);
core_service::mojom::CoreServiceCommandPtr ErrandStart(bool with_source);
void CommitStart(AcceptedApprovalLedger* ledger,
                 const core_service::mojom::CoreServiceCommand& command);
void AcceptStart(AcceptedApprovalLedger* ledger,
                 CoreStateBindingRegistry* registry,
                 const core_service::mojom::CoreServiceCommand& command);
void RetainErrandStart(AcceptedApprovalLedger* ledger, bool with_source);
core_service::mojom::CoreStateBrowserBindingsPtr DurableErrand(
    bool with_source,
    uint32_t remaining_cap);

}  // namespace accepted_approval_ledger_test

}  // namespace taffy

#endif  // TAFFY_BROWSER_ACCEPTED_APPROVAL_LEDGER_TEST_SUPPORT_H_
