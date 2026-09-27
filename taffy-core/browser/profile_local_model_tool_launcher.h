// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PROFILE_LOCAL_MODEL_TOOL_LAUNCHER_H_
#define TAFFY_BROWSER_PROFILE_LOCAL_MODEL_TOOL_LAUNCHER_H_

#include <string>
#include <vector>

#include "base/memory/ref_counted.h"
#include "taffy/contracts/tool-runtime/generated/mojom/tool_runtime.mojom-forward.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"

namespace taffy {

// Profile-owned selector for one vetted, statically linked local-model
// runtime adapter. The default product construction has no adapter and
// refuses before starting a process. A future SP-08-selected adapter supplies
// launch/cancel ports and the exact registered artifact formats it can load;
// no runtime name, library path, provider credential, or fallback crosses the
// seam.
class ProfileLocalModelToolLauncher
    : public base::RefCountedThreadSafe<ProfileLocalModelToolLauncher> {
 public:
  ProfileLocalModelToolLauncher();
  ProfileLocalModelToolLauncher(
      std::vector<tool_runtime::mojom::ToolModelArtifactKind>
          compatible_formats,
      ProfileToolSupervisor::StartPort runtime_start,
      ProfileToolSupervisor::CancelPort runtime_cancel);
  ProfileLocalModelToolLauncher(const ProfileLocalModelToolLauncher&) = delete;
  ProfileLocalModelToolLauncher& operator=(
      const ProfileLocalModelToolLauncher&) = delete;

  ProfileToolSupervisor::StartPort GetStartPort();
  ProfileToolSupervisor::CancelPort GetCancelPort();

 private:
  friend class base::RefCountedThreadSafe<ProfileLocalModelToolLauncher>;
  ~ProfileLocalModelToolLauncher();

  void Start(
      tool_runtime::mojom::ToolJobPtr job,
      tool_runtime::mojom::ToolJobResourcesPtr resources,
      mojo::PendingRemote<tool_runtime::mojom::ToolRuntimeClient> client,
      ProfileToolSupervisor::AdmissionCallback callback);
  void Cancel(const std::string& job_id);

  const std::vector<tool_runtime::mojom::ToolModelArtifactKind>
      compatible_formats_;
  const ProfileToolSupervisor::StartPort runtime_start_;
  const ProfileToolSupervisor::CancelPort runtime_cancel_;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_PROFILE_LOCAL_MODEL_TOOL_LAUNCHER_H_
