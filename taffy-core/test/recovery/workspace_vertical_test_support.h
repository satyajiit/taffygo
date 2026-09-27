// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_RECOVERY_WORKSPACE_VERTICAL_TEST_SUPPORT_H_
#define TAFFY_TEST_RECOVERY_WORKSPACE_VERTICAL_TEST_SUPPORT_H_

#include <optional>
#include <string>

#include "mojo/public/cpp/bindings/remote.h"
#include "taffy/test/recovery/core_api_status_observer.h"

namespace taffy::test {

struct ObservedWorkspaceExports {
  ObservedWorkspaceExport markdown;
  ObservedWorkspaceExport csv;
};

std::optional<ObservedWorkspaceExports> RequestObservedWorkspaceExports(
    mojo::Remote<core_api::mojom::TaffyProfileCoreApi>* facade,
    CoreApiStatusObserver* observer,
    const std::string& request_prefix,
    const ObservedWorkspaceStatus& workspace,
    bool require_source_backed_content);

void ExpectRestoredWorkspace(const ObservedWorkspaceStatus& expected,
                             const ObservedWorkspaceStatus& actual);
void ExpectRestoredCompletedTask(const ObservedTaskStatus& expected,
                                 const CoreApiStatusObserver& actual_observer);
void ExpectSingleDomSourceWorkspace(const ObservedWorkspaceStatus& workspace,
                                    const std::string& expected_host);
void ExpectSourceBackedWorkspaceExports(
    const ObservedWorkspaceStatus& workspace,
    const ObservedWorkspaceExport& markdown,
    const ObservedWorkspaceExport& csv);

}  // namespace taffy::test

#endif  // TAFFY_TEST_RECOVERY_WORKSPACE_VERTICAL_TEST_SUPPORT_H_
