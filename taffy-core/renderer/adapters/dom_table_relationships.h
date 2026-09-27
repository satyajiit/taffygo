// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_ADAPTERS_DOM_TABLE_RELATIONSHIPS_H_
#define TAFFY_RENDERER_ADAPTERS_DOM_TABLE_RELATIONSHIPS_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "taffy/renderer/adapters/adapter.h"

namespace taffy::dom_table_relationships {

struct TableCell {
  int64_t table_key = 0;
  uint32_t row_index = 0;
  uint32_t column_index = 0;
  bool is_header = false;
  std::string scope;
  SemanticNodeId node_id;
};

// Adds the typed header relationships after the DOM walk. Work and output are
// bounded by the extraction's deadline and total-byte budget.
void Append(const std::vector<TableCell>& table_cells,
            ExtractionContext& context,
            AdapterResult* result);

}  // namespace taffy::dom_table_relationships

#endif  // TAFFY_RENDERER_ADAPTERS_DOM_TABLE_RELATIONSHIPS_H_
