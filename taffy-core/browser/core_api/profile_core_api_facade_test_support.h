// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_API_PROFILE_CORE_API_FACADE_TEST_SUPPORT_H_
#define TAFFY_BROWSER_CORE_API_PROFILE_CORE_API_FACADE_TEST_SUPPORT_H_

#include <stdint.h>

#include <memory>
#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "content/public/test/test_browser_context.h"
#include "taffy/browser/core_effect_broker.h"
#include "taffy/browser/core_page_observation_broker.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_test_support.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"

namespace taffy::core_api_test {

inline constexpr uint64_t kGeneration = 1u;

// The one reply shape every credential-bearing provider handler answers with.
// It is named here rather than at a suite because the response half of the
// credential rule is asserted against it by static assertion.
using StatusOnly =
    base::OnceCallback<void(core_api::mojom::CoreApiSubmissionStatus)>;

// What one status-only provider handler answered, or nothing if it has not
// answered yet.
class RecordedStatus final {
 public:
  StatusOnly Bind() {
    return base::BindOnce(&RecordedStatus::Record, base::Unretained(this));
  }

  const std::optional<core_api::mojom::CoreApiSubmissionStatus>& value() const {
    return value_;
  }

 private:
  void Record(core_api::mojom::CoreApiSubmissionStatus status) {
    value_ = status;
  }

  std::optional<core_api::mojom::CoreApiSubmissionStatus> value_;
};

// A manager with no storage broker and no tool ports.
//
// A manager that reads nothing reaches its own availability transitions
// without launching a service process, which is what lets a facade suite
// observe a handler rather than a launch.
inline std::unique_ptr<CoreServiceManager> MakeFacadeTestManager(
    test::QuietManagerTail& tail,
    content::TestBrowserContext* context) {
  auto tools = std::make_unique<ProfileToolSupervisor>(
      kGeneration, ProfileToolSupervisor::PythonPorts::Unsupported(),
      ProfileToolSupervisor::LocalModelPorts::Unsupported(),
      ProfileToolSupervisor::MediaPorts::Unsupported());
  return tail.MakeManager(
      context, /*storage_broker=*/nullptr, std::move(tools),
      base::MakeRefCounted<CorePageObservationBroker>(context),
      std::make_unique<CoreEffectBroker>(CoreEffectBroker::Handlers{}));
}

}  // namespace taffy::core_api_test

#endif  // TAFFY_BROWSER_CORE_API_PROFILE_CORE_API_FACADE_TEST_SUPPORT_H_
