// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <array>
#include <span>

#include "base/containers/span.h"
#include "crypto/sha2.h"
#include "taffy/browser/core_task_canonical_intent.h"
#include "taffy/browser/core_task_canonical_intent_internal.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {
namespace {

using canonical_intent_internal::BytesEqual;
using canonical_intent_internal::BytesToString;
using canonical_intent_internal::CanonicalField;
using canonical_intent_internal::IsBoundedCanonicalText;
using canonical_intent_internal::kMaxCanonicalIdentifierBytes;
using canonical_intent_internal::ParseIntent;
using canonical_intent_internal::ParseNestedFields;
using canonical_intent_internal::ReadU32;

constexpr uint8_t kStoreFamily = 251u;
constexpr uint8_t kStoreQueryOperandKind = 7u;
constexpr size_t kMaxOperandHandleBytes = 96u;

bool IsSearchKind(uint8_t kind) {
  return kind == 0u || kind == 2u;
}

bool IsListKind(uint8_t kind) {
  return kind == 1u || kind == 3u;
}

bool FieldsAreSequential(const std::vector<CanonicalField>& fields) {
  for (size_t index = 0; index < fields.size(); ++index) {
    if (fields[index].tag != index) {
      return false;
    }
  }
  return true;
}

bool ReadOpaqueQuery(std::span<const uint8_t> bytes, CanonicalStoreIntent& out) {
  const std::optional<std::vector<CanonicalField>> fields =
      ParseNestedFields(bytes);
  if (!fields || fields->size() != 3u || (*fields)[0].tag != 0u ||
      !IsBoundedCanonicalText((*fields)[0].value, kMaxOperandHandleBytes,
                              true) ||
      (*fields)[1].tag != 1u || (*fields)[1].value.size() != 1u ||
      (*fields)[1].value.front() != kStoreQueryOperandKind ||
      (*fields)[2].tag != 2u ||
      (*fields)[2].value.size() != crypto::kSHA256Length) {
    return false;
  }
  out.operand_handle = BytesToString((*fields)[0].value);
  out.query_digest =
      std::vector<uint8_t>((*fields)[2].value.begin(), (*fields)[2].value.end());
  return true;
}

bool ReadLimit(std::span<const uint8_t> bytes, CanonicalStoreIntent& out) {
  const std::optional<uint32_t> limit = ReadU32(bytes);
  if (!limit || *limit == 0u ||
      *limit > core_service::mojom::kMaxTaskStoreResults) {
    return false;
  }
  out.limit = *limit;
  return true;
}

}  // namespace

std::optional<CanonicalStoreIntent> ReadCanonicalStoreIntent(
    const std::vector<uint8_t>& canonical_intent) {
  const std::optional<std::vector<CanonicalField>> fields =
      ParseIntent(canonical_intent);
  if (!fields || fields->size() < 3u || !FieldsAreSequential(*fields) ||
      (*fields)[0].value.size() != 1u ||
      (*fields)[0].value.front() != kStoreFamily ||
      (*fields)[1].value.size() != 1u || (*fields)[1].value.front() > 4u ||
      !IsBoundedCanonicalText((*fields)[2].value, kMaxCanonicalIdentifierBytes,
                              true)) {
    return std::nullopt;
  }
  CanonicalStoreIntent out;
  out.kind = (*fields)[1].value.front();
  out.context_tab_id = BytesToString((*fields)[2].value);
  if (IsSearchKind(out.kind)) {
    if (fields->size() != 5u || !ReadOpaqueQuery((*fields)[3].value, out) ||
        !ReadLimit((*fields)[4].value, out)) {
      return std::nullopt;
    }
    return out;
  }
  if (IsListKind(out.kind)) {
    if (fields->size() != 4u || !ReadLimit((*fields)[3].value, out)) {
      return std::nullopt;
    }
    return out;
  }
  if (fields->size() != 3u) {
    return std::nullopt;
  }
  return out;
}

bool CanonicalStoreIntentMatches(const std::vector<uint8_t>& canonical_intent,
                                 uint8_t kind,
                                 const std::string& context_tab_id) {
  const std::optional<CanonicalStoreIntent> parsed =
      ReadCanonicalStoreIntent(canonical_intent);
  return parsed && !context_tab_id.empty() && parsed->kind == kind &&
         parsed->context_tab_id == context_tab_id;
}

bool CanonicalStoreSearchIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    uint8_t kind,
    const std::string& context_tab_id,
    const std::string& operand_handle,
    const std::string& transient_query,
    uint32_t limit) {
  if (!IsSearchKind(kind) || operand_handle.empty() ||
      transient_query.empty() ||
      transient_query.size() > core_service::mojom::kMaxTaskStoreQueryBytes ||
      !CanonicalStoreIntentMatches(canonical_intent, kind, context_tab_id)) {
    return false;
  }
  const std::optional<CanonicalStoreIntent> parsed =
      ReadCanonicalStoreIntent(canonical_intent);
  if (!parsed || !parsed->operand_handle || !parsed->query_digest ||
      !parsed->limit || *parsed->operand_handle != operand_handle ||
      *parsed->limit != limit) {
    return false;
  }
  const std::array<uint8_t, crypto::kSHA256Length> digest =
      crypto::SHA256Hash(base::as_byte_span(transient_query));
  return std::equal(digest.begin(), digest.end(),
                    parsed->query_digest->begin(),
                    parsed->query_digest->end());
}

bool CanonicalStoreListIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    uint8_t kind,
    const std::string& context_tab_id,
    uint32_t limit) {
  if (IsSearchKind(kind) ||
      !CanonicalStoreIntentMatches(canonical_intent, kind, context_tab_id)) {
    return false;
  }
  const std::optional<CanonicalStoreIntent> parsed =
      ReadCanonicalStoreIntent(canonical_intent);
  if (!parsed) {
    return false;
  }
  // Open tabs freeze no cap: the read answers every tab under the wire's own
  // cap, and the binding must say so.
  return IsListKind(kind) ? parsed->limit && *parsed->limit == limit
                          : limit == core_service::mojom::kMaxTaskStoreResults;
}

}  // namespace taffy
