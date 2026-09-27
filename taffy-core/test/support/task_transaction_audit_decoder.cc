// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stddef.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/memory/raw_span.h"
#include "base/numerics/byte_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/string_view_util.h"
#include "taffy/test/support/task_benchmark_audit_reader.h"
#include "taffy/test/support/task_transaction_schema.h"

namespace taffy::test {
namespace {

using internal::TaskTransactionSchema;
using internal::TransactionFieldSpec;

class Decoder {
 public:
  Decoder(const TaskTransactionSchema& schema,
          const std::vector<uint8_t>& bytes,
          std::string* error)
      : schema_(schema), bytes_(bytes), error_(error) {}

  std::optional<std::vector<TaskBenchmarkAuditRecord>> Decode() {
    if (bytes_.size() > schema_->max_bytes ||
        !Take(schema_->magic.size(), nullptr) ||
        !std::equal(schema_->magic.begin(), schema_->magic.end(),
                    bytes_.begin())) {
      Fail("the transaction has invalid size or magic");
      return std::nullopt;
    }
    const size_t root_offset = offset_;
    uint32_t version = 0u;
    if (!ReadU32(&version) || version != schema_->version) {
      Fail("the transaction schema version is unsupported");
      return std::nullopt;
    }
    offset_ = root_offset;
    if (!DecodeValue(schema_->root, 0u) || offset_ != bytes_.size()) {
      if (error_->empty()) {
        Fail("the transaction has trailing bytes");
      }
      return std::nullopt;
    }
    return std::move(records_);
  }

 private:
  bool Fail(std::string_view message) {
    if (error_->empty()) {
      *error_ = std::string(message) + " at byte " + std::to_string(offset_);
    }
    return false;
  }

  bool Take(size_t size, base::span<const uint8_t>* value) {
    if (size > bytes_.size() - std::min(offset_, bytes_.size())) {
      return Fail("the transaction is truncated");
    }
    if (value) {
      *value = bytes_.subspan(offset_, size);
    }
    offset_ += size;
    return true;
  }

  bool ReadU8(uint8_t* value) {
    base::span<const uint8_t> bytes;
    if (!Take(1u, &bytes)) {
      return false;
    }
    *value = bytes.front();
    return true;
  }

  bool ReadU32(uint32_t* value) {
    base::span<const uint8_t> bytes;
    if (!Take(4u, &bytes)) {
      return false;
    }
    *value = base::U32FromLittleEndian(bytes.first<4u>());
    return true;
  }

  bool ReadU64(uint64_t* value) {
    base::span<const uint8_t> bytes;
    if (!Take(8u, &bytes)) {
      return false;
    }
    *value = base::U64FromLittleEndian(bytes.first<8u>());
    return true;
  }

  bool ReadBool(bool* value) {
    uint8_t wire = 0u;
    if (!ReadU8(&wire) || wire > 1u) {
      return Fail("the transaction has an invalid boolean tag");
    }
    *value = wire == 1u;
    return true;
  }

  bool ReadString(std::string* value) {
    uint32_t size = 0u;
    base::span<const uint8_t> bytes;
    if (!ReadU32(&size) || size > schema_->max_string_bytes ||
        !Take(size, &bytes)) {
      return Fail("the transaction has an invalid string length");
    }
    const std::string_view view = base::as_string_view(bytes);
    if (!base::IsStringUTF8(view)) {
      return Fail("the transaction has invalid UTF-8");
    }
    if (value) {
      value->assign(view);
    }
    return true;
  }

  bool DecodeEnum(std::string_view type,
                  uint32_t* wire_out,
                  std::string* name_out) {
    const auto found = schema_->enums.find(std::string(type));
    uint32_t wire = 0u;
    if (found == schema_->enums.end() || !ReadU32(&wire)) {
      return Fail("the transaction names an unknown enumeration");
    }
    const auto member = found->second.find(wire);
    if (member == found->second.end()) {
      return Fail("the transaction carries an unknown enumeration value");
    }
    if (wire_out) {
      *wire_out = wire;
    }
    if (name_out) {
      *name_out = member->second;
    }
    return true;
  }

  bool DecodeAuditRecord(size_t depth) {
    const auto found = schema_->records.find("PersistedAuditRecord");
    if (found == schema_->records.end()) {
      return Fail("the transaction schema has no audit record");
    }
    TaskBenchmarkAuditRecord record;
    bool saw_event_type = false;
    bool saw_task_id = false;
    bool saw_sequence = false;
    bool saw_content_posture = false;
    for (const TransactionFieldSpec& field : found->second) {
      if (field.name == "event_type") {
        if (field.type != "PersistedAuditEventType" ||
            !DecodeEnum(field.type, &record.event_type_wire,
                        &record.event_type)) {
          return false;
        }
        saw_event_type = true;
      } else if (field.name == "task_id") {
        if (field.type != "string" || !ReadString(&record.task_id)) {
          return false;
        }
        saw_task_id = true;
      } else if (field.name == "sequence") {
        if (field.type != "u64" || !ReadU64(&record.sequence)) {
          return false;
        }
        saw_sequence = true;
      } else if (field.name == "content_values_retained") {
        if (field.type != "bool" ||
            !ReadBool(&record.content_values_retained)) {
          return false;
        }
        saw_content_posture = true;
      } else if (!DecodeValue(field.type, depth + 1u)) {
        return false;
      }
    }
    if (!saw_event_type || !saw_task_id || record.task_id.empty() ||
        !saw_sequence || !saw_content_posture) {
      return Fail("the transaction audit projection is incomplete");
    }
    records_.push_back(std::move(record));
    return true;
  }

  bool DecodeValue(std::string_view type, size_t depth) {
    if (depth > 64u) {
      return Fail("the transaction type nesting is too deep");
    }
    constexpr std::string_view kOptional = "optional<";
    constexpr std::string_view kList = "list<";
    if (type.starts_with(kOptional) && type.ends_with('>')) {
      uint8_t present = 0u;
      if (!ReadU8(&present) || present > 1u) {
        return Fail("the transaction has an invalid optional tag");
      }
      return present == 0u ||
             DecodeValue(type.substr(kOptional.size(),
                                     type.size() - kOptional.size() - 1u),
                         depth + 1u);
    }
    if (type.starts_with(kList) && type.ends_with('>')) {
      uint32_t size = 0u;
      if (!ReadU32(&size) || size > schema_->max_collection_items) {
        return Fail("the transaction has an invalid collection length");
      }
      const std::string_view member =
          type.substr(kList.size(), type.size() - kList.size() - 1u);
      for (uint32_t index = 0u; index < size; ++index) {
        if (!DecodeValue(member, depth + 1u)) {
          return false;
        }
      }
      return true;
    }
    if (type == "u32") {
      uint32_t ignored = 0u;
      return ReadU32(&ignored);
    }
    if (type == "u64") {
      uint64_t ignored = 0u;
      return ReadU64(&ignored);
    }
    if (type == "bool") {
      bool ignored = false;
      return ReadBool(&ignored);
    }
    if (type == "string") {
      return ReadString(nullptr);
    }
    if (type == "bytes32") {
      return Take(32u, nullptr);
    }
    if (type == "bytes") {
      uint32_t size = 0u;
      return ReadU32(&size) && size <= schema_->max_bytes &&
             Take(size, nullptr);
    }
    if (schema_->enums.contains(std::string(type))) {
      return DecodeEnum(type, nullptr, nullptr);
    }
    const auto record = schema_->records.find(std::string(type));
    if (record != schema_->records.end()) {
      if (type == "PersistedAuditRecord") {
        return DecodeAuditRecord(depth);
      }
      for (const TransactionFieldSpec& field : record->second) {
        if (!DecodeValue(field.type, depth + 1u)) {
          return false;
        }
      }
      return true;
    }
    const auto tagged_union = schema_->unions.find(std::string(type));
    uint32_t tag = 0u;
    if (tagged_union == schema_->unions.end() || !ReadU32(&tag)) {
      return Fail("the transaction names an unknown wire type");
    }
    const auto variant = tagged_union->second.find(tag);
    if (variant == tagged_union->second.end()) {
      return Fail("the transaction carries an unknown union tag");
    }
    for (const TransactionFieldSpec& field : variant->second.fields) {
      if (!DecodeValue(field.type, depth + 1u)) {
        return false;
      }
    }
    return true;
  }

  const raw_ref<const TaskTransactionSchema> schema_;
  const base::raw_span<const uint8_t> bytes_;
  const raw_ptr<std::string> error_;
  size_t offset_ = 0u;
  std::vector<TaskBenchmarkAuditRecord> records_;
};

}  // namespace

std::optional<std::vector<TaskBenchmarkAuditRecord>>
DecodeTaskBenchmarkAuditBatchForTesting(const std::vector<uint8_t>& bytes,
                                        std::string* error) {
  if (!error) {
    return std::nullopt;
  }
  error->clear();
  const TaskTransactionSchema* schema =
      internal::GetTaskTransactionSchema(error);
  if (!schema) {
    return std::nullopt;
  }
  return Decoder(*schema, bytes, error).Decode();
}

}  // namespace taffy::test
