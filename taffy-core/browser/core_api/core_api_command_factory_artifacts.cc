// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <utility>

#include "taffy/browser/core_api/core_api_command_factory.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;
namespace service = core_service::mojom;

bool IsIdentifier(const std::string& value) {
  return !value.empty() && value.size() <= api::kMaxIdentifierBytes;
}

std::optional<service::TaskArtifactKind> ProjectArtifactKind(
    api::TaskArtifactKind kind) {
  switch (kind) {
    case api::TaskArtifactKind::kMarkdown:
      return service::TaskArtifactKind::kMarkdown;
    case api::TaskArtifactKind::kCsv:
      return service::TaskArtifactKind::kCsv;
    case api::TaskArtifactKind::kXlsx:
      return service::TaskArtifactKind::kXlsx;
    case api::TaskArtifactKind::kPdf:
      return service::TaskArtifactKind::kPdf;
    case api::TaskArtifactKind::kDocx:
      return service::TaskArtifactKind::kDocx;
    case api::TaskArtifactKind::kPptx:
      return service::TaskArtifactKind::kPptx;
    case api::TaskArtifactKind::kWaveAudio:
      return service::TaskArtifactKind::kWaveAudio;
    case api::TaskArtifactKind::kFrameArchive:
      return service::TaskArtifactKind::kFrameArchive;
  }
  return std::nullopt;
}

}  // namespace

std::optional<ProjectedCoreCommand>
CoreApiCommandFactory::BuildAcceptTaskArtifact(std::string task_id,
                                               std::string artifact_id,
                                               uint64_t task_revision,
                                               uint64_t service_generation,
                                               uint64_t now_monotonic_ms) {
  if (!IsIdentifier(task_id) || !IsIdentifier(artifact_id) ||
      task_revision == 0u) {
    return std::nullopt;
  }
  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(task_revision, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kAcceptTaskArtifact;
  core_command->accept_task_artifact =
      api::AcceptTaskArtifactBody::New(task_id, artifact_id);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }

  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind = service::CoreServiceCommandKind::kAcceptTaskArtifact;
  service_command->accept_task_artifact =
      service::AcceptTaskArtifactCommand::New(std::move(task_id),
                                              std::move(artifact_id));
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

std::optional<ProjectedCoreCommand>
CoreApiCommandFactory::BuildRequestTaskArtifactExport(
    std::string request_id,
    std::string task_id,
    std::string artifact_id,
    api::TaskArtifactKind kind,
    uint64_t task_revision,
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  const std::optional<service::TaskArtifactKind> service_kind =
      ProjectArtifactKind(kind);
  if (!service_kind || !IsIdentifier(request_id) || !IsIdentifier(task_id) ||
      !IsIdentifier(artifact_id) || task_revision == 0u) {
    return std::nullopt;
  }
  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(task_revision, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kRequestTaskArtifactExport;
  core_command->request_task_artifact_export =
      api::RequestTaskArtifactExportBody::New(request_id, task_id, artifact_id,
                                              kind);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }

  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind = service::CoreServiceCommandKind::kExportTaskArtifact;
  service_command->export_task_artifact =
      service::ExportTaskArtifactCommand::New(
          std::move(task_id), std::move(artifact_id), *service_kind);
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

}  // namespace taffy
