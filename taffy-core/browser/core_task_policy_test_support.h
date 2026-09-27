// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_TASK_POLICY_TEST_SUPPORT_H_
#define TAFFY_BROWSER_CORE_TASK_POLICY_TEST_SUPPORT_H_

#include <stdint.h>

#include <optional>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "taffy/browser/core_task_policy.h"

namespace taffy::core_task_policy_test {

inline constexpr uint64_t kGeneration = 7u;
inline constexpr uint64_t kRevision = 11u;
inline constexpr uint64_t kNow = 1'000u;
inline constexpr uint64_t kNowUtc = 1'800'000'000'000u;
inline constexpr char kBrowserSessionId[] = "browser-session-1";

std::vector<uint8_t> BrowserIntent(uint8_t operation,
                                   base::span<const uint8_t> operand);
std::vector<uint8_t> FormInspectIntent(std::string_view form_node_id);
// The canonical intent of a `browser.search` for `query` from tab-1, the
// shape the validator checks a transient search query against.
std::vector<uint8_t> SearchIntent(std::string_view query);
// `expected_expanded` absent is an ordinary press, which is the common case
// on a real page; a value asks for a disclosure control's exact result.
std::vector<uint8_t> DomActivationIntent(
    std::string_view node_id,
    std::optional<bool> expected_expanded = std::nullopt);
std::vector<uint8_t> DomFocusIntent(std::string_view node_id);
// The canonical intent of a `browser.link.open` on a node observed at
// `graph_revision`, in tab-1 / frame-1 / epoch-1 at https://example.test.
std::vector<uint8_t> LinkOpenIntent(std::string_view node_id,
                                    uint8_t graph_revision = 12u);
std::vector<uint8_t> SelectionReadIntent();
std::vector<uint8_t> TabControlIntent(uint8_t operation);
std::vector<uint8_t> TaskTabIntent(
    uint8_t operation,
    std::string_view context_tab_id = "tab-1",
    std::string_view browser_session_id = kBrowserSessionId,
    uint64_t target_revision = 9u);
std::vector<uint8_t> DomQueryIntent(std::optional<std::string_view> within);
core_service::mojom::TaskPolicyEffectPtr Effect();
ActorLeaseResult Lease();
TaskPolicyDocumentBinding Document();

}  // namespace taffy::core_task_policy_test

#endif  // TAFFY_BROWSER_CORE_TASK_POLICY_TEST_SUPPORT_H_
