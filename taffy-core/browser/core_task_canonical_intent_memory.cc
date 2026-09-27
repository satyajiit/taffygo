// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <span>

#include "taffy/browser/core_task_canonical_intent.h"
#include "taffy/browser/core_task_canonical_intent_internal.h"

namespace taffy {
namespace {

using canonical_intent_internal::BytesEqual;
using canonical_intent_internal::CanonicalField;
using canonical_intent_internal::IsBoundedCanonicalText;
using canonical_intent_internal::kMaxCanonicalIdentifierBytes;
using canonical_intent_internal::ParseIntent;
using canonical_intent_internal::ParseNestedFields;
using canonical_intent_internal::ReadU32;
using canonical_intent_internal::ReadU64;

bool HasExactMemoryPrefix(const std::vector<CanonicalField>& fields,
                          size_t expected_size,
                          uint8_t operation_kind,
                          const std::string& context_tab_id) {
  if (fields.size() != expected_size) {
    return false;
  }
  for (size_t index = 0; index < fields.size(); ++index) {
    if (fields[index].tag != index) {
      return false;
    }
  }
  return fields[0].value.size() == 1u && fields[0].value.front() == 253u &&
         fields[1].value.size() == 1u &&
         fields[1].value.front() == operation_kind &&
         IsBoundedCanonicalText(fields[2].value, kMaxCanonicalIdentifierBytes,
                                true) &&
         BytesEqual(fields[2].value, context_tab_id);
}

bool IsOpaqueOperand(std::span<const uint8_t> bytes, uint8_t expected_kind) {
  const std::optional<std::vector<CanonicalField>> fields =
      ParseNestedFields(bytes);
  return fields && fields->size() == 3u && (*fields)[0].tag == 0u &&
         IsBoundedCanonicalText((*fields)[0].value, 96u, true) &&
         (*fields)[1].tag == 1u && (*fields)[1].value.size() == 1u &&
         (*fields)[1].value.front() == expected_kind &&
         (*fields)[2].tag == 2u && (*fields)[2].value.size() == 32u;
}

bool ScopeAndWorkspaceMatch(std::span<const uint8_t> scope,
                            std::span<const uint8_t> workspace) {
  if (scope.size() != 1u) {
    return false;
  }
  if (scope.front() == 0u) {
    return workspace.size() == 1u && workspace.front() == 0u;
  }
  return scope.front() == 1u && workspace.size() > 1u &&
         workspace.front() == 1u &&
         IsBoundedCanonicalText(workspace.subspan(1u),
                                kMaxCanonicalIdentifierBytes, true);
}

bool IsOptionalNonZeroU64(std::span<const uint8_t> value) {
  if (value.size() == 1u) {
    return value.front() == 0u;
  }
  if (value.size() != 9u || value.front() != 1u) {
    return false;
  }
  const std::optional<uint64_t> decoded = ReadU64(value.subspan(1u));
  return decoded && *decoded > 0u;
}

}  // namespace

bool CanonicalMemoryIntentMatches(const std::vector<uint8_t>& canonical_intent,
                                  uint8_t operation_kind,
                                  const std::string& context_tab_id) {
  const std::optional<std::vector<CanonicalField>> fields =
      ParseIntent(canonical_intent);
  if (!fields || context_tab_id.empty()) {
    return false;
  }
  if (operation_kind == 0u) {
    const std::optional<uint32_t> limit =
        fields->size() == 5u ? ReadU32((*fields)[4].value) : std::nullopt;
    return HasExactMemoryPrefix(*fields, 5u, operation_kind, context_tab_id) &&
           IsOpaqueOperand((*fields)[3].value, 3u) && limit && *limit > 0u &&
           *limit <= 32u;
  }
  if (operation_kind == 1u) {
    return HasExactMemoryPrefix(*fields, 7u, operation_kind, context_tab_id) &&
           IsOpaqueOperand((*fields)[3].value, 4u) &&
           ScopeAndWorkspaceMatch((*fields)[4].value, (*fields)[5].value) &&
           IsOptionalNonZeroU64((*fields)[6].value);
  }
  if (operation_kind == 2u) {
    const std::optional<uint64_t> revision =
        fields->size() == 9u ? ReadU64((*fields)[4].value) : std::nullopt;
    return HasExactMemoryPrefix(*fields, 9u, operation_kind, context_tab_id) &&
           IsBoundedCanonicalText((*fields)[3].value,
                                  kMaxCanonicalIdentifierBytes, true) &&
           revision && *revision > 0u &&
           IsOpaqueOperand((*fields)[5].value, 4u) &&
           ScopeAndWorkspaceMatch((*fields)[6].value, (*fields)[7].value) &&
           IsOptionalNonZeroU64((*fields)[8].value);
  }
  if (operation_kind == 3u) {
    const std::optional<uint64_t> revision =
        fields->size() == 5u ? ReadU64((*fields)[4].value) : std::nullopt;
    return HasExactMemoryPrefix(*fields, 5u, operation_kind, context_tab_id) &&
           IsBoundedCanonicalText((*fields)[3].value,
                                  kMaxCanonicalIdentifierBytes, true) &&
           revision && *revision > 0u;
  }
  return false;
}

}  // namespace taffy
