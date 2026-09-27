// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/profile_tool_artifact_broker.h"

#include <stdint.h>

#include <algorithm>
#include <string_view>
#include <utility>

#include "content/public/browser/web_contents.h"
#include "taffy/browser/profile_tool_handle_store.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr uint64_t kMaxMediaBytes = 16u * 1024u * 1024u;
bool IsIdentifier(const std::string& value) {
  return !value.empty() && value.size() <= mojom::kMaxIdentifierBytes &&
         std::none_of(value.begin(), value.end(), [](char character) {
           return static_cast<unsigned char>(character) < 0x20u;
         });
}

bool SourceTypeMatches(MediaTopLevelType type, mojom::ToolOperation operation) {
  if (operation == mojom::ToolOperation::kSampleFrames) {
    return type == MediaTopLevelType::kVideo;
  }
  return type == MediaTopLevelType::kAudio || type == MediaTopLevelType::kVideo;
}

}  // namespace

ProfileToolArtifactBroker::ProfileToolArtifactBroker(
    ProfileToolSupervisor* supervisor,
    scoped_refptr<ProfileToolHandleStore> handles,
    base::FilePath output_root,
    bool private_profile)
    : supervisor_(supervisor),
      handles_(std::move(handles)),
      output_root_(std::move(output_root)),
      private_profile_(private_profile) {
  CHECK(supervisor_);
  CHECK(handles_);
}

ProfileToolArtifactBroker::~ProfileToolArtifactBroker() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  Shutdown();
}

ProfileToolArtifactBroker::OperationIdentity
ProfileToolArtifactBroker::CaptureOperation(
    const mojom::OperationEnvelope& operation) {
  return OperationIdentity{operation.operation_id,
                           operation.service_generation,
                           operation.task_revision,
                           operation.deadline_monotonic_ms,
                           operation.idempotency_key};
}

bool ProfileToolArtifactBroker::MatchesOperation(
    const OperationIdentity& expected,
    const mojom::OperationEnvelope& actual) {
  return expected.operation_id == actual.operation_id &&
         expected.service_generation == actual.service_generation &&
         expected.task_revision == actual.task_revision &&
         expected.deadline_monotonic_ms == actual.deadline_monotonic_ms &&
         expected.idempotency_key == actual.idempotency_key;
}

bool ProfileToolArtifactBroker::OperationsMatch(
    const mojom::OperationEnvelope& expected,
    const mojom::OperationEnvelope& actual) {
  return expected.operation_id == actual.operation_id &&
         expected.service_generation == actual.service_generation &&
         expected.task_revision == actual.task_revision &&
         expected.deadline_monotonic_ms == actual.deadline_monotonic_ms &&
         expected.idempotency_key == actual.idempotency_key;
}

bool ProfileToolArtifactBroker::RegisterCompletedDownload(
    const std::string& task_id,
    const std::string& browser_session_id,
    uint64_t service_generation,
    content::WebContents* web_contents,
    const TaskDownloadManagerSnapshot& snapshot) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!web_contents || !IsIdentifier(task_id) ||
      !IsIdentifier(browser_session_id) ||
      !IsIdentifier(snapshot.download_id) ||
      service_generation != active_generation_ ||
      snapshot.state != DownloadState::kComplete ||
      !SourceTypeMatches(snapshot.media_type,
                         mojom::ToolOperation::kProbeMedia) ||
      snapshot.received_bytes == 0u ||
      snapshot.received_bytes > kMaxMediaBytes) {
    return false;
  }
  const auto key = std::make_pair(task_id, snapshot.download_id);
  if (!CanAdmitSource(task_id, sources_.contains(key))) {
    return false;
  }
  SourceAdmission admission{SourceKind::kCompletedDownload,
                            task_id,
                            snapshot.download_id,
                            browser_session_id,
                            service_generation,
                            snapshot.received_bytes,
                            snapshot.media_type};
  sources_.insert_or_assign(key,
                            SourceEntry{std::move(admission), base::File(),
                                        web_contents->GetWeakPtr(), snapshot});
  return true;
}

bool ProfileToolArtifactBroker::AdmitOpenedSource(SourceAdmission admission,
                                                  base::File resource) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const int64_t length = resource.IsValid() ? resource.GetLength() : -1;
  if (!IsIdentifier(admission.task_id) || !IsIdentifier(admission.source_id) ||
      !IsIdentifier(admission.browser_session_id) ||
      admission.service_generation != active_generation_ ||
      admission.byte_count == 0u || admission.byte_count > kMaxMediaBytes ||
      !SourceTypeMatches(admission.media_type,
                         mojom::ToolOperation::kProbeMedia) ||
      length < 0 || static_cast<uint64_t>(length) != admission.byte_count) {
    return false;
  }
  const auto key = std::make_pair(admission.task_id, admission.source_id);
  if (sources_.contains(key) || !CanAdmitSource(admission.task_id, false)) {
    return false;
  }
  sources_.insert_or_assign(
      key,
      SourceEntry{std::move(admission), std::move(resource), {}, std::nullopt});
  return true;
}

}  // namespace taffy
