// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/strings/string_util.h"
#include "crypto/sha2.h"
#include "taffy/browser/core_task_canonical_intent.h"
#include "taffy/browser/core_task_canonical_intent_internal.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy::canonical_intent_internal {
namespace {

namespace mojom = core_service::mojom;

constexpr std::string_view kCanonicalIntentPrefix = "taffy.action-intent.v1";
constexpr size_t kMaxOpaqueOperandHandleBytes = 96u;
constexpr uint64_t kMaxDomQueryResults = 128u;
// The largest closed top-level intent is the ten-field native media intent.
// Keep an independent count bound so a tiny-field input cannot turn parsing
// into work proportional only to the byte ceiling.
constexpr size_t kMaxCanonicalIntentFields = 10u;

bool ParseFields(std::span<const uint8_t> bytes,
                 std::vector<CanonicalField>* fields) {
  if (!fields) {
    return false;
  }
  fields->clear();
  std::optional<uint8_t> prior_tag;
  while (!bytes.empty()) {
    if (bytes.size() < 9u || fields->size() >= kMaxCanonicalIntentFields) {
      return false;
    }
    const uint8_t tag = bytes[0];
    if (prior_tag && tag <= *prior_tag) {
      return false;
    }
    uint64_t length = 0u;
    for (size_t index = 0; index < 8u; ++index) {
      length |= static_cast<uint64_t>(bytes[index + 1u]) << (index * 8u);
    }
    if (length > std::numeric_limits<size_t>::max()) {
      return false;
    }
    bytes = bytes.subspan(9u);
    const size_t bounded_length = static_cast<size_t>(length);
    if (bounded_length > bytes.size()) {
      return false;
    }
    fields->push_back(CanonicalField{tag, bytes.first(bounded_length)});
    bytes = bytes.subspan(bounded_length);
    prior_tag = tag;
  }
  return true;
}

bool IsCanonicalDecimal(std::string_view value, uint64_t maximum) {
  if (value.empty() || (value.size() > 1u && value.front() == '0')) {
    return false;
  }
  uint64_t parsed = 0u;
  for (const char character : value) {
    if (character < '0' || character > '9') {
      return false;
    }
    const uint64_t digit = static_cast<uint64_t>(character - '0');
    if (parsed > (maximum - digit) / 10u) {
      return false;
    }
    parsed = parsed * 10u + digit;
  }
  return true;
}

bool IsCanonicalDomQueryTextHandle(std::span<const uint8_t> bytes) {
  if (!IsBoundedCanonicalText(bytes, kMaxOpaqueOperandHandleBytes, true)) {
    return false;
  }
  constexpr std::string_view kPrefix = "turn-";
  constexpr std::string_view kSeparator = "-call-";
  constexpr std::string_view kSuffix = "-dom-query-text";
  const std::string handle = BytesToString(bytes);
  if (!std::string_view(handle).starts_with(kPrefix) ||
      !std::string_view(handle).ends_with(kSuffix)) {
    return false;
  }
  std::string_view numbers(handle);
  numbers.remove_prefix(kPrefix.size());
  numbers.remove_suffix(kSuffix.size());
  const size_t separator = numbers.find(kSeparator);
  if (separator == std::string_view::npos ||
      numbers.find(kSeparator, separator + kSeparator.size()) !=
          std::string_view::npos) {
    return false;
  }
  return IsCanonicalDecimal(numbers.substr(0u, separator),
                            std::numeric_limits<uint64_t>::max()) &&
         IsCanonicalDecimal(numbers.substr(separator + kSeparator.size()),
                            std::numeric_limits<uint32_t>::max());
}

}  // namespace

std::optional<std::vector<CanonicalField>> ParseIntent(
    const std::vector<uint8_t>& canonical_intent) {
  if (canonical_intent.empty() ||
      canonical_intent.size() > mojom::kMaxCanonicalActionIntentBytes ||
      canonical_intent.size() < kCanonicalIntentPrefix.size() ||
      !std::equal(kCanonicalIntentPrefix.begin(), kCanonicalIntentPrefix.end(),
                  canonical_intent.begin())) {
    return std::nullopt;
  }
  std::vector<CanonicalField> fields;
  if (!ParseFields(
          std::span(canonical_intent).subspan(kCanonicalIntentPrefix.size()),
          &fields)) {
    return std::nullopt;
  }
  return fields;
}

std::optional<std::vector<CanonicalField>> ParseNestedFields(
    std::span<const uint8_t> bytes) {
  std::vector<CanonicalField> fields;
  if (!ParseFields(bytes, &fields)) {
    return std::nullopt;
  }
  return fields;
}

bool BytesEqual(std::span<const uint8_t> bytes, std::string_view text) {
  return bytes.size() == text.size() &&
         std::equal(bytes.begin(), bytes.end(), text.begin(), text.end(),
                    [](uint8_t left, char right) {
                      return left == static_cast<uint8_t>(right);
                    });
}

std::string BytesToString(std::span<const uint8_t> bytes) {
  return std::string(bytes.begin(), bytes.end());
}

bool IsBoundedCanonicalText(std::span<const uint8_t> bytes,
                            size_t maximum,
                            bool reject_ascii_space) {
  if (bytes.empty() || bytes.size() > maximum) {
    return false;
  }
  for (const uint8_t byte : bytes) {
    if (byte < 0x20u || byte == 0x7fu ||
        (reject_ascii_space && byte == 0x20u)) {
      return false;
    }
  }
  return base::IsStringUTF8(BytesToString(bytes));
}

bool IsCanonicalOptionalIdentifier(std::span<const uint8_t> value) {
  return (value.size() == 1u && value.front() == 0u) ||
         (value.size() > 1u && value.front() == 1u &&
          IsBoundedCanonicalText(value.subspan(1u),
                                 kMaxCanonicalIdentifierBytes, true));
}

bool IsCanonicalOptionalDomRole(std::span<const uint8_t> value) {
  return (value.size() == 1u && value.front() == 0u) ||
         (value.size() == 2u && value.front() == 1u && value[1] <= 7u);
}

bool IsCanonicalOptionalDomQueryText(std::span<const uint8_t> value) {
  if (value.size() == 1u && value.front() == 0u) {
    return true;
  }
  if (value.empty() || value.front() != 1u) {
    return false;
  }
  std::vector<CanonicalField> wrapper;
  if (!ParseFields(value.subspan(1u), &wrapper) || wrapper.size() != 1u ||
      wrapper[0].tag != 0u) {
    return false;
  }
  std::vector<CanonicalField> opaque;
  return ParseFields(wrapper[0].value, &opaque) && opaque.size() == 3u &&
         opaque[0].tag == 0u &&
         IsCanonicalDomQueryTextHandle(opaque[0].value) &&
         opaque[1].tag == 1u && opaque[1].value.size() == 1u &&
         opaque[1].value.front() == 1u && opaque[2].tag == 2u &&
         opaque[2].value.size() == crypto::kSHA256Length;
}

bool IsCanonicalOptionalDomQueryLimit(std::span<const uint8_t> value) {
  if (value.size() == 1u && value.front() == 0u) {
    return true;
  }
  if (value.size() != 1u + sizeof(uint64_t) || value.front() != 1u) {
    return false;
  }
  const std::optional<uint64_t> limit = ReadU64(value.subspan(1u));
  return limit && *limit > 0u && *limit <= kMaxDomQueryResults;
}

std::optional<uint64_t> ReadU64(std::span<const uint8_t> bytes) {
  if (bytes.size() != sizeof(uint64_t)) {
    return std::nullopt;
  }
  uint64_t value = 0u;
  for (size_t index = 0; index < bytes.size(); ++index) {
    value |= static_cast<uint64_t>(bytes[index]) << (index * 8u);
  }
  return value;
}

std::optional<uint32_t> ReadU32(std::span<const uint8_t> bytes) {
  if (bytes.size() != sizeof(uint32_t)) {
    return std::nullopt;
  }
  uint32_t value = 0u;
  for (size_t index = 0; index < bytes.size(); ++index) {
    value |= static_cast<uint32_t>(bytes[index]) << (index * 8u);
  }
  return value;
}

bool HasExactTopLevelShape(const std::vector<CanonicalField>& fields,
                           uint8_t operation) {
  return fields.size() == 3u && fields[0].tag == 0u &&
         fields[0].value.size() == 1u && fields[0].value.front() == operation &&
         fields[1].tag == 1u && fields[2].tag == 2u;
}

bool SearchMatches(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& tab_id,
    const std::optional<std::string_view>& expected_operand_handle,
    const std::string& transient_query) {
  if (tab_id.empty() || transient_query.empty() ||
      transient_query.size() > mojom::kMaxTransientSearchQueryBytes) {
    return false;
  }
  const std::optional<std::vector<CanonicalField>> fields =
      ParseIntent(canonical_intent);
  if (!fields || !HasExactTopLevelShape(*fields, 1u) ||
      !BytesEqual((*fields)[1].value, tab_id)) {
    return false;
  }
  std::vector<CanonicalField> opaque;
  if (!ParseFields((*fields)[2].value, &opaque) || opaque.size() != 3u ||
      opaque[0].tag != 0u || opaque[0].value.empty() || opaque[1].tag != 1u ||
      opaque[1].value.size() != 1u || opaque[1].value.front() != 0u ||
      opaque[2].tag != 2u || opaque[2].value.size() != 32u ||
      (expected_operand_handle &&
       !BytesEqual(opaque[0].value, *expected_operand_handle))) {
    return false;
  }
  const std::array<uint8_t, 32> digest =
      crypto::SHA256Hash(base::as_byte_span(transient_query));
  return std::equal(digest.begin(), digest.end(), opaque[2].value.begin());
}

std::optional<CanonicalObservedNodeHandle> ReadObservedNodeHandle(
    const std::vector<CanonicalField>& fields,
    uint8_t operation_tag) {
  if (fields.size() < 8u || fields[0].tag != 0u ||
      fields[0].value.size() != 1u ||
      fields[0].value.front() != operation_tag || fields[1].tag != 1u ||
      !IsBoundedCanonicalText(fields[1].value, kMaxCanonicalIdentifierBytes,
                              true) ||
      fields[2].tag != 2u ||
      !IsBoundedCanonicalText(fields[2].value, kMaxCanonicalIdentifierBytes,
                              true) ||
      fields[3].tag != 3u ||
      !IsBoundedCanonicalText(fields[3].value, kMaxCanonicalIdentifierBytes,
                              true) ||
      fields[4].tag != 4u || fields[5].tag != 5u ||
      !IsBoundedCanonicalText(fields[5].value, kMaxCanonicalIdentifierBytes,
                              true) ||
      fields[6].tag != 6u || fields[6].value.size() != 1u ||
      fields[6].value.front() > 1u || fields[7].tag != 7u ||
      fields[7].value.empty()) {
    return std::nullopt;
  }
  const std::optional<uint64_t> graph_revision = ReadU64(fields[4].value);
  if (!graph_revision) {
    return std::nullopt;
  }
  return CanonicalObservedNodeHandle{
      .tab_id = BytesToString(fields[1].value),
      .frame_id = BytesToString(fields[2].value),
      .page_epoch = BytesToString(fields[3].value),
      .graph_revision = *graph_revision,
      .node_id = BytesToString(fields[5].value),
      .expected_origin_is_opaque = fields[6].value.front() == 1u,
      .expected_origin = BytesToString(fields[7].value),
  };
}

}  // namespace taffy::canonical_intent_internal

namespace taffy {

// Beside the reader of the handle it compares. It reads no canonical bytes:
// it names which fact of a handle already read disagrees with the document.
const char* ObservedNodeHandleMismatch(
    const CanonicalObservedNodeHandle& handle,
    const std::string& tab_id,
    const std::string& node_id,
    const std::string& frame_id,
    const std::string& page_epoch,
    uint64_t graph_revision,
    const std::string& origin) {
  if (handle.tab_id != tab_id) {
    return "tab";
  }
  if (handle.node_id != node_id) {
    return "node";
  }
  if (handle.frame_id != frame_id) {
    return "frame";
  }
  if (handle.page_epoch != page_epoch) {
    return "page-epoch";
  }
  if (handle.graph_revision == 0u) {
    // Names no reading, and a zero floor would ask the renderer for nothing.
    return "no-revision";
  }
  if (handle.graph_revision > graph_revision) {
    return "revision-from-the-future";
  }
  if (handle.expected_origin_is_opaque) {
    return "opaque-origin";
  }
  if (handle.expected_origin != origin) {
    return "origin";
  }
  return nullptr;
}

}  // namespace taffy
