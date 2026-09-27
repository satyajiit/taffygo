// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_DEFERRED_TASK_SURFACE_OWNER_H_
#define TAFFY_BROWSER_CORE_DEFERRED_TASK_SURFACE_OWNER_H_

#include <stdint.h>

#include <string>

#include "taffy/browser/core_service_manager_types.h"

namespace taffy {

class CoreServiceManager;

// Owns a person's answer while the public task surface is visible but the
// Core Service is still committing the effect that exposed it. Ownership is
// deliberately distinct from admission: only the Core Session can resolve
// the retained command's callback.
class CoreDeferredTaskSurfaceOwner final {
 public:
  static CoreDeferredTaskSurfaceMatch Match(
      const CoreServiceManager& manager,
      const core_service::mojom::CoreServiceCommand& command,
      std::string* effect_id);
  static CoreDeferredTaskSurfaceOwnership Own(
      CoreServiceManager& manager,
      const std::string& effect_id,
      core_service::mojom::CoreServiceCommandPtr command,
      CoreServiceSubmitCallback callback);
  static bool Remember(CoreServiceManager& manager,
                       const core_service::mojom::TaskEffectBinding& binding,
                       uint64_t state_sequence);
  static void Forget(CoreServiceManager& manager,
                     const std::string& effect_id,
                     core_service::mojom::AdmissionStatus pending_status);
  static void RecordAdmission(CoreServiceManager& manager,
                              const std::string& effect_id,
                              const std::string& operation_id,
                              core_service::mojom::AdmissionStatus status);
  static void CompletePublished(CoreServiceManager& manager,
                                uint64_t state_sequence);
  static void Reconcile(CoreServiceManager& manager);
  static void ScheduleDeadline(CoreServiceManager& manager);
  static void FailAll(CoreServiceManager& manager,
                      core_service::mojom::AdmissionStatus status);
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_DEFERRED_TASK_SURFACE_OWNER_H_
