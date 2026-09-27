// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/tools/entrypoints/tool_entrypoint_registry.h"

namespace taffy::tools {

const ToolEntrypoint* FindToolEntrypoint(std::string_view entrypoint_id) {
  // A linear scan over three rows, and it stays a linear scan when there are
  // thirty-two: the table is bounded by the generator, and a binary search
  // would add an ordering assumption this file would then have to keep in step
  // with the source.
  for (const ToolEntrypoint& row : kToolEntrypoints) {
    if (row.id == entrypoint_id) {
      return &row;
    }
  }
  return nullptr;
}

ToolEntrypointVerdict AdmitToolEntrypoint(std::string_view entrypoint_id) {
  const ToolEntrypoint* row = FindToolEntrypoint(entrypoint_id);
  if (!row) {
    return {ToolEntrypointAdmission::kUnknown, std::string_view()};
  }
  if (!row->native_alternative.empty()) {
    return {ToolEntrypointAdmission::kRefusedForNativePath,
            row->native_alternative};
  }
  return {ToolEntrypointAdmission::kAdmitted, std::string_view()};
}

}  // namespace taffy::tools
