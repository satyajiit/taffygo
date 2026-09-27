// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_SKILL_SHAPE_H_
#define TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_SKILL_SHAPE_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy::skill_internal {

// The bounds a skill row is held to, in one place, so the write path and the
// restore path agree by construction. They are the contract's own limits: a
// value the writer would have refused must not be a value the reader accepts,
// because a database is written by one build and read by the next.

bool IsSkillIdentifier(const std::string& value);
bool IsOrigin(const std::string& value);
bool IsVersion(int64_t value);
bool IsStepCount(int64_t value);
bool IsDefinition(const std::vector<uint8_t>& value);

// Whether a timestamp fits the signed integer SQLite stores it in. A value
// that does not is refused rather than wrapped, because a wrapped timestamp
// still sorts, and recall is sorted by exactly this column.
bool IsTimestamp(uint64_t value);

// Whether the task-commit half of a storage effect is empty, which every
// non-APPEND_TASK_COMMIT operation requires.
bool HasNoTaskFields(const core_service::mojom::StorageCommitEffect& body);

// Whether exactly the named skill body is present and no other optional body
// is. A storage operation carrying two bodies is a message two readers would
// disagree about.
bool HasOnlySkillBody(const core_service::mojom::StorageCommitEffect& body,
                      core_service::mojom::StorageOperation expected);

}  // namespace taffy::skill_internal

#endif  // TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_SKILL_SHAPE_H_
