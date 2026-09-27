// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_SERVICE_COMMAND_VALIDATION_INTERNAL_H_
#define TAFFY_BROWSER_CORE_SERVICE_COMMAND_VALIDATION_INTERNAL_H_

#include <stddef.h>

#include <optional>
#include <string>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

inline bool AddBounded(size_t amount, size_t ceiling, size_t* total) {
  if (amount > ceiling || *total > ceiling - amount) {
    return false;
  }
  *total += amount;
  return true;
}

inline bool AddIdentifier(const std::string& value,
                          size_t ceiling,
                          size_t* total) {
  return !value.empty() &&
         value.size() <= core_service::mojom::kMaxIdentifierBytes &&
         AddBounded(value.size(), ceiling, total);
}

inline bool AddOptionalIdentifier(const std::optional<std::string>& value,
                                  size_t ceiling,
                                  size_t* total) {
  return !value || AddIdentifier(*value, ceiling, total);
}

// A sixty-four character lowercase hexadecimal digest, and nothing else. Two
// bodies name one: the approval digest a decision carries, and the preview id
// a library refresh names.
inline bool AddSha256Digest(const std::string& value,
                           size_t ceiling,
                           size_t* total) {
  if (value.size() != 64u) {
    return false;
  }
  for (char character : value) {
    if (!((character >= '0' && character <= '9') ||
          (character >= 'a' && character <= 'f'))) {
      return false;
    }
  }
  return AddBounded(value.size(), ceiling, total);
}

// A provider identity is bounded by its own limit, not by the generic
// identifier bound: 64 bytes rather than 256, because the Android provider
// store cannot file a record under a longer one (decision 0049 section 4).
inline bool AddProviderId(const std::string& value,
                          size_t ceiling,
                          size_t* total) {
  return !value.empty() &&
         value.size() <= core_service::mojom::kMaxProviderIdBytes &&
         AddBounded(value.size(), ceiling, total);
}

std::optional<size_t> ProviderCommandByteSize(
    const core_service::mojom::CoreServiceCommand& command,
    size_t ceiling);

std::optional<size_t> MemoryCommandByteSize(
    const core_service::mojom::CoreServiceCommand& command,
    size_t ceiling);

std::optional<size_t> ProfileCommandByteSize(
    const core_service::mojom::CoreServiceCommand& command,
    size_t ceiling);

std::optional<size_t> SkillCommandByteSize(
    const core_service::mojom::CoreServiceCommand& command,
    size_t ceiling);

std::optional<size_t> WorkspaceCommandByteSize(
    const core_service::mojom::CoreServiceCommand& command,
    size_t ceiling);

bool HasMatchingCommandBody(
    const core_service::mojom::CoreServiceCommand& command);

// The start body's own bounded shape. `refusal` is set, to a compiled-in
// clause name, exactly when this answers nothing.
std::optional<size_t> StartTaskByteSize(
    const core_service::mojom::StartTaskCommand& start,
    size_t ceiling,
    const char** refusal);

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_SERVICE_COMMAND_VALIDATION_INTERNAL_H_
