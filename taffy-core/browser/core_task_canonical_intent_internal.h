// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_TASK_CANONICAL_INTENT_INTERNAL_H_
#define TAFFY_BROWSER_CORE_TASK_CANONICAL_INTENT_INTERNAL_H_

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "taffy/browser/core_task_canonical_intent.h"

namespace taffy::canonical_intent_internal {

inline constexpr size_t kMaxCanonicalIdentifierBytes = 256u;

struct CanonicalField {
  uint8_t tag = 0;
  std::span<const uint8_t> value;
};

std::optional<CanonicalObservedNodeHandle> ReadObservedNodeHandle(
    const std::vector<CanonicalField>& fields, uint8_t operation_tag);

std::optional<std::vector<CanonicalField>> ParseIntent(
    const std::vector<uint8_t>& canonical_intent);
std::optional<std::vector<CanonicalField>> ParseNestedFields(
    std::span<const uint8_t> bytes);
bool BytesEqual(std::span<const uint8_t> bytes, std::string_view text);
std::string BytesToString(std::span<const uint8_t> bytes);
bool IsBoundedCanonicalText(std::span<const uint8_t> bytes,
                            size_t maximum,
                            bool reject_ascii_space);
bool IsCanonicalOptionalIdentifier(std::span<const uint8_t> value);
bool IsCanonicalOptionalDomRole(std::span<const uint8_t> value);
bool IsCanonicalOptionalDomQueryText(std::span<const uint8_t> value);
bool IsCanonicalOptionalDomQueryLimit(std::span<const uint8_t> value);
std::optional<uint64_t> ReadU64(std::span<const uint8_t> bytes);
std::optional<uint32_t> ReadU32(std::span<const uint8_t> bytes);
bool HasExactTopLevelShape(const std::vector<CanonicalField>& fields,
                           uint8_t operation);
bool SearchMatches(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& tab_id,
    const std::optional<std::string_view>& expected_operand_handle,
    const std::string& transient_query);

}  // namespace taffy::canonical_intent_internal

#endif  // TAFFY_BROWSER_CORE_TASK_CANONICAL_INTENT_INTERNAL_H_
