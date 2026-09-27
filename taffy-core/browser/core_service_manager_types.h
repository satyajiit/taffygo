// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_SERVICE_MANAGER_TYPES_H_
#define TAFFY_BROWSER_CORE_SERVICE_MANAGER_TYPES_H_

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list_types.h"
#include "base/timer/timer.h"
#include "taffy/common/public/bip_identity.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace content {
class WebContents;
}

namespace taffy {

enum class CoreServiceAvailability {
  kStopped,
  kStarting,
  kReady,
  kUnavailable,
  kCircuitOpen,
  kShuttingDown,
};

class CoreServiceObserver : public base::CheckedObserver {
 public:
  ~CoreServiceObserver() override = default;
  virtual void OnCoreAvailabilityChanged(CoreServiceAvailability availability) {
  }
  virtual void OnCoreState(const core_service::mojom::CoreStateUpdate& state) {}
  virtual void OnCorePermissionRequest(
      const std::string& request_id,
      core_api::mojom::PlatformPermission permission) {}
  // The core has asked a person to fill a form in (decision 0088). Nothing
  // about a value travels on this notification and nothing ever will: it
  // names the request, the task, and the tab and node the form is in, and
  // the browser decides from the node itself which of that form's fields
  // need a person. The person types into a surface the browser owns and the
  // bytes go straight into the profile's vault; what crosses back to the
  // core is a count.
  virtual void OnCoreFieldValueRequest(const std::string& request_id,
                                       const std::string& task_id,
                                       const std::string& tab_id,
                                       const std::string& node_id) {}
  // How far one running asset transfer has got.
  //
  // This is the one observation on this interface that does not come from
  // the isolated core. It cannot: the core is told what a transfer did when
  // it ends, and a progress bar needs to move while it is still running.
  virtual void OnAssetProgress(const std::string& asset_id,
                               const std::string& asset_revision,
                               uint64_t written_bytes,
                               uint64_t total_bytes) {}
  // What the core proposes the person types next, for the one request that
  // asked. Absent text is the honest answer for "nothing worth offering"
  // and is not the same as an empty string; the surface decides what either
  // draws. Nothing about it is durable — it is one push, and a surface that
  // was not attached has missed it.
  virtual void OnComposerCompletion(const std::string& request_id,
                                    const std::optional<std::string>& text) {}
  // One sandbox-sanitized visible answer event. Raw provider frames never
  // cross this observer; a terminal carries no text and closes one call.
  virtual void OnTaskAnswerDelta(const std::string& task_id,
                                 const std::string& call_id,
                                 uint32_t sequence,
                                 const std::optional<std::string>& text,
                                 bool terminal,
                                 bool complete) {}
  // A generated file remains inside the browser until a profile facade proves
  // that a still-live explicit request matches it. Returning false tells the
  // core that no trusted surface accepted custody; restored work therefore
  // cannot leak bytes after the requesting surface has gone away.
  virtual bool OnTaskArtifactExport(
      const std::string& task_id,
      const std::string& artifact_id,
      core_service::mojom::TaskArtifactKind kind,
      const std::vector<uint8_t>& content);
};

using CoreServiceSubmitCallback =
    base::OnceCallback<void(core_service::mojom::AdmissionPtr)>;
using CoreServicePolicyEvaluationCallback =
    base::OnceCallback<void(core_service::mojom::PolicyEvaluationResultPtr)>;
using CoreServicePageInspectorCallback =
    base::OnceCallback<void(core_api::mojom::PageInspectorSnapshotResultPtr)>;
using CoreServicePageExportCallback =
    base::OnceCallback<void(core_api::mojom::PageSnapshotExportResultPtr)>;
using BackupPrepareRequest =
    core_service::mojom::BackupManifestPrepareRequestPtr;
using BackupPrepareResult =
    core_service::mojom::BackupManifestPrepareResultPtr;
using BackupPrepareCallback = base::OnceCallback<void(BackupPrepareResult)>;
using BackupInspectRequest =
    core_service::mojom::BackupManifestInspectRequestPtr;
using BackupInspectResult =
    core_service::mojom::BackupManifestInspectResultPtr;
using BackupInspectCallback = base::OnceCallback<void(BackupInspectResult)>;
using BackupRestoreRequest =
    core_service::mojom::BackupRestorePlanRequestPtr;
using BackupRestoreResult =
    core_service::mojom::BackupRestorePlanResultPtr;
using BackupRestoreCallback = base::OnceCallback<void(BackupRestoreResult)>;
using CoreModelStreamChunkCallback = base::OnceCallback<void(
    core_service::mojom::ModelStreamChunkStatus)>;

// The manager's lifecycle queue records. They live together because a service
// generation either dispatches all three kinds or settles all three as
// unavailable; none is product state of its own.
enum class CoreServiceTeardownReason {
  kNone,
  kProfileShutdown,
  kIdle,
  kInitializationRefused,
  kAccountReconciliation,
};

struct CorePendingAdmission {
  core_service::mojom::CoreServiceCommandPtr command;
  CoreServiceSubmitCallback callback;
  bool sent = false;
  // Present only while a command was accepted into browser custody during the
  // publication window for the task surface that produced it. `ready` means a
  // strictly newer public state retained the exact unchanged binding; it is
  // not an admission result from the Core Service.
  struct DeferredTaskSurface {
    std::string effect_id;
    bool ready = false;
  };
  std::optional<DeferredTaskSurface> deferred_task_surface;
};

// One surface whose first public state can be observed before the Core Service
// has durably completed the effect that handed the surface to the browser.
// Keeping the full immutable binding makes the later command comparison exact
// without inventing a second tuple for approval, permission, and field-value
// requests.
struct CoreDeferredTaskSurface {
  uint64_t state_sequence = 0;
  core_service::mojom::TaskEffectBindingPtr binding;
  std::optional<std::string> owner_operation_id;
  // A real Core admission does not by itself withdraw the public binding.
  // Keep its one-decision guard until a later state proves that binding
  // advanced or disappeared.
  bool admitted = false;
};

enum class CoreDeferredTaskSurfaceMatch {
  kNone,
  kExact,
  kConflict,
};

// Ownership means the browser has retained exactly one command and its
// callback. It never means the Core Service admitted that command.
enum class CoreDeferredTaskSurfaceOwnership {
  kOwned,
  kRefused,
};

struct CorePendingPolicyEvaluation {
  core_service::mojom::PolicyEvaluationRequestPtr request;
  CoreServicePolicyEvaluationCallback callback;
  bool sent = false;
};

struct CorePendingPageInspection {
  base::WeakPtr<content::WebContents> web_contents;
  CoreServicePageInspectorCallback callback;
};

// One exact browser-owned page lifetime. Export and saved-skill offers both
// bind to this record so those security checks cannot drift.
struct CorePageObservationIdentity {
  std::string tab_id;
  std::string frame_id;
  std::string page_epoch;
  std::string origin;
  uint64_t graph_revision = 0;
};

// One direct read's browser-owned authority and exact page lifetime, moved as
// one record through policy evaluation and observation completion so callback
// signatures cannot accidentally drop one of the bindings.
struct CoreDirectObservationRequest {
  base::WeakPtr<content::WebContents> web_contents;
  CorePageObservationIdentity identity;
  ActorLeaseId lease_id;
  CoreServicePageInspectorCallback callback;
};

struct CorePendingPageExport {
  base::WeakPtr<content::WebContents> web_contents;
  std::string request_id;
  std::string document_id;
  core_api::mojom::PageSnapshotExportFormat format =
      core_api::mojom::PageSnapshotExportFormat::kMarkdown;
  CorePageObservationIdentity identity;
  core_service::mojom::OperationEnvelopePtr operation;
  std::string effect_id;
  uint64_t captured_at_epoch_ms = 0;
  bool source_query_withheld = false;
  bool source_fragment_withheld = false;
  bool secure_context = false;
  CoreServicePageExportCallback callback;
};

// Facts one closed handover window observed, for a CompleteHandover command.
//
// Lease identities and the input count stay in the browser process. The Core
// API body is the task identity only, so a surface cannot read how much the
// person typed.
struct HandoverCompletionFacts {
  std::string handover_id;
  std::string lease_before;
  std::string resumed_with;
  uint32_t person_input = 0;
};

// Browser-only custody of one still-open handover window.
struct CoreOpenHandover {
  std::string handover_id;
  std::optional<std::string> tab_id;
  std::string lease_before;
  uint32_t window_ms = 0;
  std::unique_ptr<base::OneShotTimer> expiry;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_SERVICE_MANAGER_TYPES_H_
