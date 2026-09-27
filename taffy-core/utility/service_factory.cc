// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/utility/service_factory.h"

#include <memory>
#include <utility>

#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/service_factory.h"
#include "taffy/contracts/core-service/core_service.mojom.h"
#include "taffy/contracts/tool-runtime/tool_runtime_service.mojom.h"
#include "taffy/services/core/core_service_impl.h"
#include "taffy/services/tool-runtime/media/media_tool_service.h"
#if defined(TAFFY_ENABLE_PYTHON_RUNTIME)
#include "taffy/services/tool-runtime/python/python_tool_service.h"
#endif

namespace taffy {

namespace {

auto RunCoreService(
    mojo::PendingReceiver<core_service::mojom::TaffyCoreService> receiver) {
  return std::make_unique<CoreServiceImpl>(std::move(receiver));
}

// One media worker per process, because the contract's media root is one job
// per process. The factory names each runtime separately rather than offering
// a runtime parameter: a process that could be asked which runtime to become
// is a universal worker, and the whole isolation argument rests on there not
// being one.
auto RunMediaToolService(
    mojo::PendingReceiver<tool_runtime::mojom::MediaToolService> receiver) {
  return std::make_unique<MediaToolServiceImpl>(std::move(receiver));
}

#if defined(TAFFY_ENABLE_PYTHON_RUNTIME)
auto RunPythonToolService(
    mojo::PendingReceiver<tool_runtime::mojom::PythonToolService> receiver) {
  return std::make_unique<PythonToolServiceImpl>(std::move(receiver));
}
#endif

}  // namespace

void RegisterUtilityMainThreadServices(mojo::ServiceFactory& services) {
  services.Add(RunCoreService);
  services.Add(RunMediaToolService);
#if defined(TAFFY_ENABLE_PYTHON_RUNTIME)
  services.Add(RunPythonToolService);
#endif
}

}  // namespace taffy
