// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_TOOLS_ENTRYPOINTS_TOOL_ENTRYPOINT_REGISTRY_H_
#define TAFFY_COMPONENTS_TOOLS_ENTRYPOINTS_TOOL_ENTRYPOINT_REGISTRY_H_

#include <string_view>

#include "taffy/components/tools/entrypoints/generated/cpp/tool_entrypoints.h"

namespace taffy::tools {

// What this build answers when it is asked for an entrypoint by name.
//
// The rows are in the generated header beside this one. This file holds the
// one decision taken over them, because a decision belongs in a file a person
// wrote and the table does not.
enum class ToolEntrypointAdmission {
  // A row carries this identity and no native path owns the capability.
  kAdmitted,
  // A row carries this identity and a native path owns the capability, so no
  // worker is started for it. Decision 0064 states why the row stays in the
  // registry rather than being left out: a refusal by name is reviewable, and
  // an absence reads as "not written yet".
  kRefusedForNativePath,
  // No compiled-in row carries this identity. There is no second place to
  // look, and no way to add one without shipping a build.
  kUnknown,
};

// The verdict, and the native owner when there is one.
//
// `native_alternative` is empty for every verdict except
// `kRefusedForNativePath`, and it points into the compiled-in table rather
// than owning storage, so a caller may compare and log it and must not outlive
// the process to do so - which it cannot.
struct ToolEntrypointVerdict {
  ToolEntrypointAdmission admission;
  std::string_view native_alternative;
};

// The row with this identity, or nullptr when the build carries none.
//
// The comparison is exact. A lookup that trimmed, lowercased or prefix-matched
// would be a lookup a caller could steer into a row it was not given.
const ToolEntrypoint* FindToolEntrypoint(std::string_view entrypoint_id);

// Whether a Python worker may be asked for this entrypoint.
//
// This is the browser's own gate. The core asks the same question of the same
// generated rows before it ever proposes a job, and the two are deliberately
// not written in terms of each other: the browser does not take the core's
// word for what a sandboxed process may be started for.
ToolEntrypointVerdict AdmitToolEntrypoint(std::string_view entrypoint_id);

}  // namespace taffy::tools

#endif  // TAFFY_COMPONENTS_TOOLS_ENTRYPOINTS_TOOL_ENTRYPOINT_REGISTRY_H_
