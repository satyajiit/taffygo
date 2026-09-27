// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/profile_local_model_tool_launcher.h"

#include <algorithm>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "taffy/contracts/tool-runtime/generated/mojom/tool_runtime.mojom.h"
#include "taffy/contracts/tool-runtime/tool_runtime_service.mojom.h"

namespace taffy {
namespace {

namespace mojom = tool_runtime::mojom;

const mojom::ToolModelArtifact* DeclaredArtifact(const mojom::ToolJob& job) {
  if (job.local_model) {
    return job.local_model->model.get();
  }
  if (job.local_embedding) {
    return job.local_embedding->model.get();
  }
  return nullptr;
}

void Refuse(const std::string& job_id,
            mojom::ToolAdmissionStatus status,
            ProfileToolSupervisor::AdmissionCallback callback) {
  std::move(callback).Run(mojom::ToolAdmission::New(job_id, status));
}

}  // namespace

ProfileLocalModelToolLauncher::ProfileLocalModelToolLauncher() = default;

ProfileLocalModelToolLauncher::ProfileLocalModelToolLauncher(
    std::vector<mojom::ToolModelArtifactKind> compatible_formats,
    ProfileToolSupervisor::StartPort runtime_start,
    ProfileToolSupervisor::CancelPort runtime_cancel)
    : compatible_formats_(std::move(compatible_formats)),
      runtime_start_(std::move(runtime_start)),
      runtime_cancel_(std::move(runtime_cancel)) {
  CHECK(!compatible_formats_.empty());
  CHECK(!runtime_start_.is_null());
  CHECK(!runtime_cancel_.is_null());
}

ProfileLocalModelToolLauncher::~ProfileLocalModelToolLauncher() = default;

ProfileToolSupervisor::StartPort
ProfileLocalModelToolLauncher::GetStartPort() {
  return base::BindRepeating(&ProfileLocalModelToolLauncher::Start,
                             base::RetainedRef(this));
}

ProfileToolSupervisor::CancelPort
ProfileLocalModelToolLauncher::GetCancelPort() {
  return base::BindRepeating(&ProfileLocalModelToolLauncher::Cancel,
                             base::RetainedRef(this));
}

void ProfileLocalModelToolLauncher::Start(
    mojom::ToolJobPtr job,
    mojom::ToolJobResourcesPtr resources,
    mojo::PendingRemote<mojom::ToolRuntimeClient> client,
    ProfileToolSupervisor::AdmissionCallback callback) {
  const std::string job_id = job ? job->job_id : std::string();
  const mojom::ToolModelArtifact* artifact =
      job ? DeclaredArtifact(*job) : nullptr;
  if (!job || !resources || !client.is_valid() || !artifact ||
      job->runtime != mojom::ToolRuntimeKind::kLocalModel ||
      (job->operation_kind != mojom::ToolOperation::kGenerateLocalModel &&
       job->operation_kind != mojom::ToolOperation::kEmbedLocalModel)) {
    Refuse(job_id, mojom::ToolAdmissionStatus::kInvalidJob,
           std::move(callback));
    return;
  }
  const bool wants_adapter = !artifact->adapter_id.empty();
  if (!resources->model_artifact.IsValid() ||
      (wants_adapter && !resources->model_adapter.IsValid())) {
    Refuse(job_id, mojom::ToolAdmissionStatus::kModelArtifactMissing,
           std::move(callback));
    return;
  }
  if (!wants_adapter && resources->model_adapter.IsValid()) {
    Refuse(job_id, mojom::ToolAdmissionStatus::kModelArtifactIncompatible,
           std::move(callback));
    return;
  }
  if (runtime_start_.is_null()) {
    Refuse(job_id, mojom::ToolAdmissionStatus::kLocalRuntimeUnavailable,
           std::move(callback));
    return;
  }
  if (std::ranges::find(compatible_formats_, artifact->kind) ==
      compatible_formats_.end()) {
    Refuse(job_id, mojom::ToolAdmissionStatus::kModelArtifactIncompatible,
           std::move(callback));
    return;
  }
  runtime_start_.Run(std::move(job), std::move(resources), std::move(client),
                     std::move(callback));
}

void ProfileLocalModelToolLauncher::Cancel(const std::string& job_id) {
  if (!runtime_cancel_.is_null()) {
    runtime_cancel_.Run(job_id);
  }
}

}  // namespace taffy
