// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_LIBRARY_H_
#define TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_LIBRARY_H_

#include <stdint.h>

#include <vector>

#include "sql/database.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

// Selected-data reader: no account, session or other bootstrap family is read.
bool LoadLibraryRecords(
    sql::Database* database,
    std::vector<core_service::mojom::LibraryEntryRecordPtr>* records,
    uint64_t* global_revision);

// Restores the complete bounded Library projection and exact global revision.
bool LoadLibrary(sql::Database* database,
                 core_service::mojom::CoreBootstrap* bootstrap);

// Applies one exact-revision entry upsert atomically with the global revision.
bool CommitLibraryEntry(sql::Database* database,
                        const core_service::mojom::EffectEnvelope& effect);

// Removes one entry, its citations, and every derived searchable byte in one
// transaction, retaining only a content-free replay tombstone.
bool CommitLibraryDeletion(sql::Database* database,
                           const core_service::mojom::EffectEnvelope& effect);

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_LIBRARY_H_
