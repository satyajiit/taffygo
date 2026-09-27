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

// See OnPageSnapshotExportReply: `SavedFlowQueryResult` is non-nullable and
// RustCore::QuerySavedFlows returns nullptr on a refused conversion, so the
// reply needs the same closed refusal the admission branch below gives.
void CoreServiceImpl::OnSavedFlowQueryReply(
    mojom::OperationEnvelopePtr operation,
    QuerySavedFlowsCallback callback,
    mojom::SavedFlowQueryResultPtr result) {
  if (!ready_ || shutdown_started_ || !operation ||
      operation->service_generation != generation_ || !result ||
      !result->operation || !result->operation->Equals(*operation)) {
    result = mojom::SavedFlowQueryResult::New();
    result->operation =
        operation ? std::move(operation) : mojom::OperationEnvelope::New();
    result->status = mojom::SavedFlowQueryStatus::kUnavailable;
  }
  std::move(callback).Run(std::move(result));
}

void CoreServiceImpl::QuerySavedFlows(mojom::SavedFlowQueryCommandPtr command,
                                      QuerySavedFlowsCallback callback) {
  if (!ready_ || shutdown_started_ || !command || !command->operation ||
      command->operation->service_generation != generation_) {
    auto result = mojom::SavedFlowQueryResult::New();
    result->operation = command && command->operation
                            ? command->operation.Clone()
                            : mojom::OperationEnvelope::New();
    result->status = mojom::SavedFlowQueryStatus::kUnavailable;
    std::move(callback).Run(std::move(result));
    return;
  }
  auto operation = command->operation.Clone();
  core_.AsyncCall(&RustCore::QuerySavedFlows)
      .WithArgs(std::move(command), NowMonotonicMillis())
      .Then(base::BindOnce(&CoreServiceImpl::OnSavedFlowQueryReply,
                           weak_factory_.GetWeakPtr(), std::move(operation),
                           std::move(callback)));
}

}  // namespace taffy
