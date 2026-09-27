// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "taffy/contracts/tool-runtime/generated/mojom/tool_runtime.mojom.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"

namespace taffy {
namespace {

namespace runtime = tool_runtime::mojom;

ProfileToolSupervisor::StartPort UnsupportedStartPort() {
  return base::BindRepeating(
      [](runtime::ToolJobPtr job, runtime::ToolJobResourcesPtr,
         mojo::PendingRemote<runtime::ToolRuntimeClient>,
         ProfileToolSupervisor::AdmissionCallback callback) {
        const std::string job_id = job ? job->job_id : std::string();
        std::move(callback).Run(runtime::ToolAdmission::New(
            job_id, runtime::ToolAdmissionStatus::kUnsupported));
      });
}

ProfileToolSupervisor::CancelPort UnsupportedCancelPort() {
  return base::BindRepeating([](const std::string&) {});
}

}  // namespace

ProfileToolSupervisor::PythonPorts::PythonPorts(StartPort start,
                                                CancelPort cancel)
    : start(std::move(start)), cancel(std::move(cancel)) {
  CHECK(!this->start.is_null());
  CHECK(!this->cancel.is_null());
}
ProfileToolSupervisor::PythonPorts::PythonPorts(const PythonPorts&) = default;
ProfileToolSupervisor::PythonPorts::PythonPorts(PythonPorts&&) = default;
ProfileToolSupervisor::PythonPorts::~PythonPorts() = default;

ProfileToolSupervisor::PythonPorts
ProfileToolSupervisor::PythonPorts::Unsupported() {
  return PythonPorts(UnsupportedStartPort(), UnsupportedCancelPort());
}

ProfileToolSupervisor::LocalModelPorts::LocalModelPorts(StartPort start,
                                                        CancelPort cancel)
    : start(std::move(start)), cancel(std::move(cancel)) {
  CHECK(!this->start.is_null());
  CHECK(!this->cancel.is_null());
}
ProfileToolSupervisor::LocalModelPorts::LocalModelPorts(
    const LocalModelPorts&) = default;
ProfileToolSupervisor::LocalModelPorts::LocalModelPorts(LocalModelPorts&&) =
    default;
ProfileToolSupervisor::LocalModelPorts::~LocalModelPorts() = default;

ProfileToolSupervisor::LocalModelPorts
ProfileToolSupervisor::LocalModelPorts::Unsupported() {
  return LocalModelPorts(UnsupportedStartPort(), UnsupportedCancelPort());
}

ProfileToolSupervisor::MediaPorts::MediaPorts(StartPort start,
                                              CancelPort cancel)
    : start(std::move(start)), cancel(std::move(cancel)) {
  CHECK(!this->start.is_null());
  CHECK(!this->cancel.is_null());
}
ProfileToolSupervisor::MediaPorts::MediaPorts(const MediaPorts&) = default;
ProfileToolSupervisor::MediaPorts::MediaPorts(MediaPorts&&) = default;
ProfileToolSupervisor::MediaPorts::~MediaPorts() = default;

ProfileToolSupervisor::MediaPorts
ProfileToolSupervisor::MediaPorts::Unsupported() {
  return MediaPorts(UnsupportedStartPort(), UnsupportedCancelPort());
}

}  // namespace taffy
