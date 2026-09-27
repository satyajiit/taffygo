// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/support/task_transaction_schema.h"

#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"

namespace taffy::test::internal {
namespace {

constexpr base::FilePath::CharType kContractPath[] =
    FILE_PATH_LITERAL("taffy/contracts/core-service/schema/contract.json");
constexpr base::FilePath::CharType kTransactionSchemaPath[] = FILE_PATH_LITERAL(
    "taffy/contracts/core-service/schema/task_transaction.json");

std::optional<base::DictValue> ReadObject(const base::FilePath& path,
                                          std::string* error) {
  std::string bytes;
  if (!base::ReadFileToString(path, &bytes)) {
    *error = "cannot read the committed task transaction schema";
    return std::nullopt;
  }
  std::optional<base::Value> value =
      base::JSONReader::Read(bytes, base::JSON_PARSE_RFC);
  if (!value || !value->is_dict()) {
    *error = "the committed task transaction schema is not strict JSON";
    return std::nullopt;
  }
  return std::move(*value).TakeDict();
}

bool ReadFields(const base::ListValue* values,
                std::vector<TransactionFieldSpec>* fields,
                std::string* error) {
  if (!values) {
    *error = "a task transaction schema type has no fields list";
    return false;
  }
  std::set<std::string> names;
  for (const base::Value& value : *values) {
    const base::DictValue* row = value.GetIfDict();
    const std::string* name = row ? row->FindString("name") : nullptr;
    const std::string* type = row ? row->FindString("type") : nullptr;
    if (!name || name->empty() || !type || type->empty() ||
        !names.insert(*name).second) {
      *error = "a task transaction schema field is malformed or duplicated";
      return false;
    }
    fields->push_back({*name, *type});
  }
  return true;
}

bool ReadNamedTypes(const base::DictValue& types,
                    TaskTransactionSchema* schema,
                    std::string* error) {
  const base::ListValue* enums = types.FindList("enums");
  // The committed schema names its fixed-field types "structs"; the decoder
  // that reads this graph calls the same thing a record. Read the document's
  // own key and keep the decoder's vocabulary for the in-memory map.
  const base::ListValue* records = types.FindList("structs");
  const base::ListValue* unions = types.FindList("unions");
  if (!enums || !records || !unions) {
    *error = "the task transaction schema has no complete type registry";
    return false;
  }
  std::set<std::string> type_names;
  for (const base::Value& value : *enums) {
    const base::DictValue* row = value.GetIfDict();
    const std::string* name = row ? row->FindString("name") : nullptr;
    const base::ListValue* members = row ? row->FindList("members") : nullptr;
    if (!name || name->empty() || !members ||
        !type_names.insert(*name).second) {
      *error = "a task transaction enumeration is malformed or duplicated";
      return false;
    }
    auto& by_wire = schema->enums[*name];
    for (const base::Value& member_value : *members) {
      const base::DictValue* member = member_value.GetIfDict();
      const std::string* member_name =
          member ? member->FindString("name") : nullptr;
      const std::optional<int> wire =
          member ? member->FindInt("wire") : std::nullopt;
      if (!member_name || member_name->empty() || !wire || *wire < 0 ||
          !by_wire.emplace(static_cast<uint32_t>(*wire), *member_name).second) {
        *error = "a task transaction enumeration member is malformed";
        return false;
      }
    }
  }
  for (const base::Value& value : *records) {
    const base::DictValue* row = value.GetIfDict();
    const std::string* name = row ? row->FindString("name") : nullptr;
    if (!name || name->empty() || !type_names.insert(*name).second) {
      *error = "a task transaction record is malformed or duplicated";
      return false;
    }
    if (!ReadFields(row->FindList("fields"), &schema->records[*name], error)) {
      return false;
    }
  }
  for (const base::Value& value : *unions) {
    const base::DictValue* row = value.GetIfDict();
    const std::string* name = row ? row->FindString("name") : nullptr;
    const base::ListValue* variants = row ? row->FindList("variants") : nullptr;
    if (!name || name->empty() || !variants ||
        !type_names.insert(*name).second) {
      *error = "a task transaction union is malformed or duplicated";
      return false;
    }
    auto& by_wire = schema->unions[*name];
    for (const base::Value& variant_value : *variants) {
      const base::DictValue* variant = variant_value.GetIfDict();
      const std::optional<int> wire =
          variant ? variant->FindInt("wire") : std::nullopt;
      TransactionVariantSpec spec;
      if (!wire || *wire < 0 ||
          !ReadFields(variant ? variant->FindList("fields") : nullptr,
                      &spec.fields, error) ||
          !by_wire.emplace(static_cast<uint32_t>(*wire), std::move(spec))
               .second) {
        if (error->empty()) {
          *error = "a task transaction union variant is malformed";
        }
        return false;
      }
    }
  }
  return true;
}

std::optional<TaskTransactionSchema> LoadSchema(std::string* error) {
  // Read on whichever thread asks first, and that thread is not always allowed
  // to block. `GetTaskTransactionSchema` caches behind a function-local static,
  // so the two file reads happen inside the first call — and the first call in
  // the Task Benchmark verticals arrives on the UI thread, inside the reply of
  // a `PostTaskAndReplyWithResult`, where `AssertBlockingAllowed` is fatal in
  // every build this project produces. Allowed rather than made asynchronous
  // because this is test support reading two committed schema documents whose
  // absence stops the run either way; the alternative is threading a warm-up
  // through every caller so the cache is filled somewhere it is permitted.
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::FilePath root;
  if (!base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root)) {
    *error = "no Chromium test-data root is available";
    return std::nullopt;
  }
  std::optional<base::DictValue> contract =
      ReadObject(root.Append(kContractPath), error);
  std::optional<base::DictValue> document =
      ReadObject(root.Append(kTransactionSchemaPath), error);
  if (!contract || !document) {
    return std::nullopt;
  }
  const std::string* codec = contract->FindString("binary_codec_schema");
  const std::string* limit_name = document->FindString("max_bytes_limit");
  const base::DictValue* limits = contract->FindDict("limits");
  const std::optional<int> max_bytes =
      limits && limit_name ? limits->FindInt(*limit_name) : std::nullopt;
  const std::optional<int> version = document->FindInt("schema_version");
  const std::optional<int> max_items =
      document->FindInt("max_collection_items");
  const std::optional<int> max_string = document->FindInt("max_string_bytes");
  const std::string* root_name = document->FindString("root");
  const std::string* version_field =
      document->FindString("schema_version_field");
  const std::string* magic = document->FindString("magic");
  const base::DictValue* types = document->FindDict("types");
  if (!codec || *codec != "task_transaction.json" || !max_bytes ||
      *max_bytes <= 0 || !version || *version < 0 || !max_items ||
      *max_items <= 0 || !max_string || *max_string <= 0 || !root_name ||
      root_name->empty() || !version_field || version_field->empty() ||
      !magic || magic->empty() || !types) {
    *error = "the task transaction schema header is malformed";
    return std::nullopt;
  }
  TaskTransactionSchema schema;
  schema.root = *root_name;
  schema.version_field = *version_field;
  schema.magic = *magic;
  schema.version = static_cast<uint32_t>(*version);
  schema.max_bytes = static_cast<size_t>(*max_bytes);
  schema.max_collection_items = static_cast<size_t>(*max_items);
  schema.max_string_bytes = static_cast<size_t>(*max_string);
  if (!ReadNamedTypes(*types, &schema, error)) {
    return std::nullopt;
  }
  const auto root_record = schema.records.find(schema.root);
  if (root_record == schema.records.end() || root_record->second.empty() ||
      root_record->second.front().name != schema.version_field ||
      root_record->second.front().type != "u32") {
    *error = "the transaction root does not begin with its version field";
    return std::nullopt;
  }
  return schema;
}

struct SchemaCache {
  SchemaCache() { schema = LoadSchema(&error); }
  std::optional<TaskTransactionSchema> schema;
  std::string error;
};

}  // namespace

const TaskTransactionSchema* GetTaskTransactionSchema(std::string* error) {
  static const base::NoDestructor<SchemaCache> cache;
  if (!cache->schema) {
    *error = cache->error;
    return nullptr;
  }
  return &*cache->schema;
}

}  // namespace taffy::test::internal
