// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "base/functional/bind.h"

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/core_service_impl.h"

namespace taffy {

namespace mojom = core_service::mojom;

// RustCore::ExportPageSnapshot answers a refused conversion, a refused shape
// or a mismatched operation echo with nullptr, and `PageSnapshotExportResult`
// is a non-nullable response. Forwarding the nullptr fails receive-side
// validation and raises an error on the CoreSession pipe, which ends the whole
// utility generation rather than this one call. The admission branch below
// already knows the shape of a closed refusal; this is the same refusal for
// the answers that come back.
void CoreServiceImpl::OnPageSnapshotExportReply(
    mojom::OperationEnvelopePtr operation,
    mojom::PageSnapshotExportFormat format,
    ExportPageSnapshotCallback callback,
    mojom::PageSnapshotExportResultPtr result) {
  if (!ready_ || shutdown_started_ || !operation ||
      operation->service_generation != generation_ || !result ||
      !result->operation || !result->operation->Equals(*operation)) {
    result = mojom::PageSnapshotExportResult::New();
    result->operation = operation ? std::move(operation)
                                  : mojom::OperationEnvelope::New();
    result->status = mojom::PageSnapshotExportStatus::kUnavailable;
    result->format = format;
  }
  std::move(callback).Run(std::move(result));
}

void CoreServiceImpl::ExportPageSnapshot(
    mojom::PageSnapshotExportCommandPtr command,
    ExportPageSnapshotCallback callback) {
  if (!ready_ || shutdown_started_ || !command || !command->operation ||
      command->operation->service_generation != generation_) {
    auto result = mojom::PageSnapshotExportResult::New();
    result->operation = command && command->operation
                            ? command->operation.Clone()
                            : mojom::OperationEnvelope::New();
    result->status = mojom::PageSnapshotExportStatus::kUnavailable;
    result->format =
        command ? command->format : mojom::PageSnapshotExportFormat::kMarkdown;
    std::move(callback).Run(std::move(result));
    return;
  }
  auto operation = command->operation.Clone();
  const mojom::PageSnapshotExportFormat format = command->format;
  core_.AsyncCall(&RustCore::ExportPageSnapshot)
      .WithArgs(std::move(command), NowMonotonicMillis())
      .Then(base::BindOnce(&CoreServiceImpl::OnPageSnapshotExportReply,
                           weak_factory_.GetWeakPtr(), std::move(operation),
                           format, std::move(callback)));
}

void CoreServiceImpl::CancelPageSnapshotExport(
    mojom::OperationEnvelopePtr operation,
    CancelPageSnapshotExportCallback callback) {
  if (!ready_ || shutdown_started_ || !operation ||
      operation->service_generation != generation_) {
    std::move(callback).Run(false);
    return;
  }
  core_.AsyncCall(&RustCore::CancelPageSnapshotExport)
      .WithArgs(operation->operation_id, operation->idempotency_key)
      .Then(std::move(callback));
}

}  // namespace taffy
