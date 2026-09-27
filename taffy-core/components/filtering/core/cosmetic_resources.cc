// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/filtering/core/cosmetic_resources.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/json/json_reader.h"
#include "base/values.h"

namespace taffy::filtering {
namespace {

std::vector<std::string> StringList(const base::DictValue& dict,
                                    const char* key) {
  std::vector<std::string> out;
  const base::ListValue* list = dict.FindList(key);
  if (!list) {
    return out;
  }
  for (const base::Value& entry : *list) {
    if (const std::string* text = entry.GetIfString()) {
      out.push_back(*text);
    }
  }
  return out;
}

std::vector<std::string> StringArray(const base::ListValue& list) {
  std::vector<std::string> out;
  for (const base::Value& entry : list) {
    if (const std::string* text = entry.GetIfString()) {
      out.push_back(*text);
    }
  }
  return out;
}

}  // namespace

std::optional<CosmeticResources> ParseUrlCosmeticResourcesJson(
    std::string_view json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  const base::DictValue& dict = parsed->GetDict();
  CosmeticResources resources;
  resources.hide_selectors = StringList(dict, "hide_selectors");
  resources.exceptions = StringList(dict, "exceptions");
  resources.generichide = dict.FindBool("generichide").value_or(false);
  return resources;
}

std::optional<std::vector<std::string>> ParseSelectorListJson(
    std::string_view json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return std::nullopt;
  }
  return StringArray(parsed->GetList());
}

}  // namespace taffy::filtering
