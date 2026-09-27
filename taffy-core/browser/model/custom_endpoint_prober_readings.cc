// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What the server said about itself, read from the server's own answer.
//
// Decision 0096 section 5: the probe proves an address rather than a
// credential, and it asks the server what it is instead of guessing from a
// port. So every reading here is a fact the body carries — a listing shape, a
// field name, a path that answered — and none is inferred from where the
// request went.
//
// The parse is bounded in both directions: the caller caps the bytes it will
// download, and the depth limit below caps the structure. These bodies are a
// flat list of small objects on every runtime in the set, so a document that
// needs more nesting than that is not one of them.

#include "taffy/browser/model/custom_endpoint_prober_readings.h"

#include <stddef.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

#include "base/json/json_reader.h"
#include "base/values.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr size_t kMaxProbeJsonDepth = 8;

uint32_t BoundedCount(size_t entries) {
  return static_cast<uint32_t>(
      std::min(entries, static_cast<size_t>(mojom::kMaxCustomModelEntries)));
}

// A token count, or zero when the server did not state one this product
// can hold. Zero is the contract's word for "the endpoint did not say", so a
// value that is not a positive integer within the field's own width is
// reported as absence rather than clamped to the nearest number that fits —
// clamping would file a window the server never claimed.
uint32_t BoundedWindow(const base::Value* value) {
  const std::optional<int> stated = value ? value->GetIfInt() : std::nullopt;
  if (!stated || *stated <= 0) {
    return 0u;
  }
  return static_cast<uint32_t>(*stated);
}

// Match the catalog listing dialect: a missing/null top-level limit can be
// stated by `top_provider`. An invalid stated limit stays unknown instead of
// falling back to a different value the server may also have published.
const base::Value* ListedLimit(const base::DictValue& model,
                               std::string_view name) {
  const base::Value* value = model.Find(name);
  if (value && !value->is_none()) {
    return value;
  }
  const base::DictValue* served = model.FindDict("top_provider");
  return served ? served->Find(name) : nullptr;
}

bool NamesParameter(const base::DictValue& model, std::string_view name) {
  const base::ListValue* parameters = model.FindList("supported_parameters");
  return parameters &&
         std::ranges::any_of(*parameters, [name](const base::Value& value) {
           const std::string* stated = value.GetIfString();
           return stated && *stated == name;
         });
}

}  // namespace

OpenAiListingReading ReadOpenAiListing(const std::string& body) {
  OpenAiListingReading reading;
  const std::optional<base::DictValue> parsed = base::JSONReader::ReadDict(
      body, base::JSON_PARSE_RFC, kMaxProbeJsonDepth);
  if (!parsed) {
    return reading;
  }
  const base::ListValue* data = parsed->FindList("data");
  if (!data) {
    return reading;
  }
  reading.understood = true;
  reading.model_count = BoundedCount(data->size());
  for (const base::Value& entry : *data) {
    const base::DictValue* model = entry.GetIfDict();
    if (!model) {
      continue;
    }
    // vLLM's own word about itself. It is read across the whole listing rather
    // than only the entries that survive below, because the runtime a server
    // is does not depend on which of its models this product can carry.
    const base::Value* window = model->Find("max_model_len");
    if (window) {
      reading.names_vllm = true;
    }
    // The list stops at the bound while the count above does not: an entry
    // dropped here is one this product cannot carry, and the number a person
    // is shown stays the number the server named.
    if (reading.models.size() >=
        static_cast<size_t>(mojom::kMaxCustomModelEntries)) {
      continue;
    }
    const std::string* model_id = model->FindString("id");
    // No identity, or one no request could name: dropped rather than carried
    // under a placeholder. A row in a person's picker that refuses every call
    // is worse than a row that is not there.
    if (!model_id || model_id->empty() ||
        model_id->size() > mojom::kMaxModelIdBytes) {
      continue;
    }
    // A runtime's configured window wins over general model metadata. The
    // other spellings follow provider_listing/dialect.rs::openai_listing;
    // neither an absent list nor an unrelated boolean licenses tool calls.
    if (!window || window->is_none()) {
      window = ListedLimit(*model, "context_length");
    }
    reading.models.push_back(CustomModelReading{
        *model_id, BoundedWindow(window),
        BoundedWindow(ListedLimit(*model, "max_completion_tokens")),
        NamesParameter(*model, "reasoning"), NamesParameter(*model, "tools")});
  }
  return reading;
}

std::optional<uint32_t> ReadOllamaTags(const std::string& body) {
  const std::optional<base::DictValue> parsed = base::JSONReader::ReadDict(
      body, base::JSON_PARSE_RFC, kMaxProbeJsonDepth);
  if (!parsed) {
    return std::nullopt;
  }
  const base::ListValue* models = parsed->FindList("models");
  if (!models) {
    return std::nullopt;
  }
  return BoundedCount(models->size());
}

bool ReadLlamaCppProps(const std::string& body) {
  return base::JSONReader::ReadDict(body, base::JSON_PARSE_RFC,
                                    kMaxProbeJsonDepth)
      .has_value();
}

}  // namespace taffy
