// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_MEMORY_H_
#define TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_MEMORY_H_

#include <stdint.h>

#include <vector>

#include "sql/database.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

// Selected-data reader: no account, session or other bootstrap family is read.
bool LoadMemoryRecords(
    sql::Database* database,
    std::vector<core_service::mojom::MemoryRecordPtr>* records,
    uint64_t* global_revision);

// Restores the complete bounded Memory projection and exact global revision.
bool LoadMemory(sql::Database* database,
                core_service::mojom::CoreBootstrap* bootstrap);

// Applies one exact-revision record upsert atomically with the global revision.
bool CommitMemoryRecord(sql::Database* database,
                        const core_service::mojom::EffectEnvelope& effect);

// Removes every content-bearing byte for one exact record and retains only a
// content-free replay tombstone.
bool CommitMemoryDeletion(sql::Database* database,
                          const core_service::mojom::EffectEnvelope& effect);

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_MEMORY_H_
