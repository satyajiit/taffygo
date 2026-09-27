// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_TOOL_RUNTIME_SUPERVISOR_TOOL_JOB_RESOURCES_H_
#define TAFFY_SERVICES_TOOL_RUNTIME_SUPERVISOR_TOOL_JOB_RESOURCES_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "base/files/file.h"
#include "base/functional/callback.h"
#include "taffy/contracts/tool-runtime/generated/mojom/tool_runtime.mojom-forward.h"
#include "taffy/contracts/tool-runtime/generated/mojom/tool_runtime.mojom-shared.h"
#include "taffy/contracts/tool-runtime/tool_runtime_service.mojom-forward.h"
#include "taffy/services/tool-runtime/supervisor/tool_handle_broker.h"

namespace taffy::tool_job_resources {

// One registered model artifact as the browser's own register records it.
// The caller's typed declaration must agree field for field; this record
// verifies it and carries the opened descriptor, it does not repair it.
struct ModelArtifactSource {
  ModelArtifactSource();
  ModelArtifactSource(ModelArtifactSource&&);
  ModelArtifactSource& operator=(ModelArtifactSource&&);
  ~ModelArtifactSource();

  base::File file;
  uint64_t byte_length = 0u;
  // Exactly thirty-two bytes. The contract's `bytes32` projects to a Mojo
  // fixed-length array, which the C++ bindings carry as a vector and validate
  // the length of on serialization.
  std::vector<uint8_t> digest;
  tool_runtime::mojom::ToolModelArtifactKind kind =
      tool_runtime::mojom::ToolModelArtifactKind::kLitertTflite;
};

// The browser register's content-free answer to one exact lookup. A caller
// never learns whether a path was absent, validation is still pending, or a
// catalog row was withdrawn; all are the same safe fact: no verified opened
// artifact is available for this identity now.
enum class ModelArtifactResolution {
  kResolved,
  kMissing,
};

// Resolves one registered model identity against the browser's register and
// opens it read-only. Injected for the same reason the launch ports are: a
// model register is profile state, and this directory depends on neither
// //chrome nor //content. Absent until the local-model milestone installs it,
// and absent means a job that names a model is refused rather than started
// without one.
using ModelArtifactPort =
    base::RepeatingCallback<ModelArtifactResolution(
        const std::string& model_id,
        const std::string& model_revision,
        bool adapter,
        ModelArtifactSource* resolved)>;

// Resource binding finishes before a worker or runtime adapter is asked to do
// anything. The model failures stay separate because absence can be repaired
// by installing the exact signed catalog row, while incompatibility requires
// a different declaration or a compiled adapter. Neither answer carries a
// path, catalog body, or artifact bytes.
enum class BindResult {
  kBound,
  kInvalidJob,
  kModelArtifactMissing,
  kModelArtifactIncompatible,
};

// The bundled Python standard library as the browser's own build records it.
// There is one per build rather than one per job, so the port takes no
// identity: a caller cannot ask for a different library, because asking is not
// something this seam offers.
struct PythonLibrarySource {
  PythonLibrarySource();
  PythonLibrarySource(PythonLibrarySource&&);
  PythonLibrarySource& operator=(PythonLibrarySource&&);
  ~PythonLibrarySource();

  base::File file;
  std::string library_id;
  std::string library_version;
  uint64_t byte_length = 0u;
  // Exactly thirty-two bytes, for the reason ModelArtifactSource::digest is.
  std::vector<uint8_t> digest;
};

// Opens the installed bundled standard library read-only. Injected for the
// same reason the model port is: this layer cannot choose or open a profile
// asset. Configurations with the source-built interpreter install the port and
// require its descriptor and measured facts to agree with the job; other
// configurations omit both this port and the Python launch port.
using PythonLibraryPort =
    base::RepeatingCallback<bool(PythonLibrarySource* resolved)>;

// Decides how this job's bounded input and output cross the process boundary.
// The decision is the browser's alone: it follows from the declared length,
// the operation, and whether anything is present to receive a stream. A worker
// is told the answer and never asked for one.
void PlanPayloads(bool streaming_available, tool_runtime::mojom::ToolJob* job);

// Opens every resource the plan and arguments name, and verifies model facts
// against the browser register before any descriptor crosses to a worker.
//
// False means the job is refused. There is deliberately no partial success: a
// job that named a resource and did not get it would otherwise run against
// whatever was left, which for a model artifact means running against no model
// and reporting a result anyway.
BindResult Bind(const ModelArtifactPort& open_model_artifact,
                const PythonLibraryPort& open_python_library,
                ToolHandleBroker* broker,
                tool_runtime::mojom::ToolJob* job,
                tool_runtime::mojom::ToolJobResourcesPtr* resources);

}  // namespace taffy::tool_job_resources

#endif  // TAFFY_SERVICES_TOOL_RUNTIME_SUPERVISOR_TOOL_JOB_RESOURCES_H_
