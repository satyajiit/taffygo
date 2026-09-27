// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The browser's half of decision 0096: the addresses a person typed, held
// where they survive a restart and can be read before the isolated core is up.
//
// The stored address is the typed string itself, unparsed. That is the
// property the whole design rests on: a model request naming a person's own
// address is answered by comparing it with a row here byte for byte, and a
// comparison against a normalized copy would be a comparison against something
// nobody typed.
//
// The row carries the rest of the definition beside it — name, wire family,
// models, detected runtime, and the handle naming the key — because that is
// what a new core generation has to be told to know the provider exists at all
// (decision 0117), and because a save states them together. A row holding only
// an address is still read as one: it registers what it registers, and it is
// not a definition anything can replay.
//
// The two closed enumerations are stored under their contract spelling rather
// than their wire number, for `provider_model_preference_store.cc`'s reason —
// a number on disk is an ordinal, and a renumbered enumeration would read
// every row back as a different member with nothing to say so.

#include "taffy/browser/model/custom_provider_endpoint_store.h"

#include <stddef.h>

#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/values.h"
#include "components/prefs/pref_service.h"
#include "taffy/browser/model/custom_provider_endpoint_policy.h"
#include "taffy/browser/profile_preferences.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

namespace api = core_api::mojom;

constexpr char kEndpointKey[] = "endpoint";
constexpr char kDisplayNameKey[] = "display_name";
constexpr char kWireApiKey[] = "wire_api";
constexpr char kCredentialHandleKey[] = "credential_handle";
constexpr char kModelsKey[] = "models";
constexpr char kDetectedServerKey[] = "detected_server";
constexpr char kModelIdKey[] = "model_id";
constexpr char kContextWindowKey[] = "context_window";
constexpr char kMaxOutputTokensKey[] = "max_output_tokens";
constexpr char kReasoningKey[] = "reasoning";
constexpr char kToolCallingKey[] = "tool_calling";

struct StoredWireApi {
  std::string_view name;
  api::ProviderWireApiView wire_api;
};

struct StoredServerKind {
  std::string_view name;
  api::ServerKindView server_kind;
};

// One table per enumeration and one for both directions, because two would be
// two lists that must agree and nothing would compare them.
constexpr StoredWireApi kWireApis[] = {
    {"ANTHROPIC_MESSAGES", api::ProviderWireApiView::kAnthropicMessages},
    {"OPEN_AI_RESPONSES", api::ProviderWireApiView::kOpenAiResponses},
    {"OPEN_AI_COMPLETIONS", api::ProviderWireApiView::kOpenAiCompletions},
    {"GOOGLE_GENERATIVE_LANGUAGE",
     api::ProviderWireApiView::kGoogleGenerativeLanguage},
    {"MANAGED", api::ProviderWireApiView::kManaged},
    {"OPEN_AI_CODEX_RESPONSES",
     api::ProviderWireApiView::kOpenAiCodexResponses},
    {"GOOGLE_CLOUD_CODE_ASSIST",
     api::ProviderWireApiView::kGoogleCloudCodeAssist},
};

constexpr StoredServerKind kServerKinds[] = {
    {"OPENAI_COMPATIBLE", api::ServerKindView::kOpenaiCompatible},
    {"OLLAMA", api::ServerKindView::kOllama},
    {"LM_STUDIO", api::ServerKindView::kLmStudio},
    {"VLLM", api::ServerKindView::kVllm},
    {"LLAMA_CPP", api::ServerKindView::kLlamaCpp},
};

// Compared against the contract's own maxima rather than against literals, so
// a member added to either closed enumeration fails to compile here instead of
// becoming a member this store silently cannot spell.
static_assert(
    std::size(kWireApis) ==
        static_cast<size_t>(api::ProviderWireApiView::kMaxValue) + 1u,
    "every provider wire family needs a stored name");
static_assert(std::size(kServerKinds) ==
                  static_cast<size_t>(api::ServerKindView::kMaxValue) + 1u,
              "every detected server kind needs a stored name");

std::optional<api::ProviderWireApiView> WireApiFromName(
    std::string_view name) {
  for (const StoredWireApi &known : kWireApis) {
    if (known.name == name) {
      return known.wire_api;
    }
  }
  return std::nullopt;
}

std::string_view NameForWireApi(api::ProviderWireApiView wire_api) {
  for (const StoredWireApi &known : kWireApis) {
    if (known.wire_api == wire_api) {
      return known.name;
    }
  }
  return std::string_view();
}

std::optional<api::ServerKindView> ServerKindFromName(std::string_view name) {
  for (const StoredServerKind &known : kServerKinds) {
    if (known.name == name) {
      return known.server_kind;
    }
  }
  return std::nullopt;
}

std::string_view NameForServerKind(api::ServerKindView server_kind) {
  for (const StoredServerKind &known : kServerKinds) {
    if (known.server_kind == server_kind) {
      return known.name;
    }
  }
  return std::string_view();
}

// Whether a stored value reads back as an address at all.
//
// Length and emptiness only. It is deliberately not the address policy: a row
// is dropped here for being unreadable, and a row the policy would refuse is
// one the write already refused.
bool ReadsBackAsAnAddress(const std::string &value) {
  return !value.empty() && value.size() <= mojom::kMaxProviderEndpointBytes;
}

// The address out of one stored row, whichever shape the row is in.
const std::string *AddressIn(const base::Value &value) {
  if (const std::string *typed = value.GetIfString()) {
    return typed;
  }
  const base::DictValue *row = value.GetIfDict();
  return row ? row->FindString(kEndpointKey) : nullptr;
}

// One stored model row, or nothing.
//
// A field missing or out of its bound drops the model, and the caller drops
// the whole definition with it. A provider whose model list came back one
// model short is a provider a person set up to reach a model that is silently
// no longer offered, which is the truncation decision 0096 section 5 refuses.
std::optional<CustomProviderModel> ModelIn(const base::Value &value) {
  const base::DictValue *row = value.GetIfDict();
  if (!row) {
    return std::nullopt;
  }
  const std::string *model_id = row->FindString(kModelIdKey);
  const std::string *display_name = row->FindString(kDisplayNameKey);
  const std::optional<int> context_window = row->FindInt(kContextWindowKey);
  const std::optional<int> max_output_tokens =
      row->FindInt(kMaxOutputTokensKey);
  const std::optional<bool> reasoning = row->FindBool(kReasoningKey);
  const std::optional<bool> tool_calling = row->FindBool(kToolCallingKey);
  if (!model_id || !display_name || !context_window || !max_output_tokens ||
      !reasoning || !tool_calling) {
    return std::nullopt;
  }
  if (model_id->empty() || model_id->size() > mojom::kMaxModelIdBytes ||
      display_name->empty() ||
      display_name->size() > mojom::kMaxModelDisplayNameBytes) {
    return std::nullopt;
  }
  // A preference file holds signed integers and these two fields are unsigned
  // on the wire. A negative value is a torn row rather than a window nobody
  // meant, so it drops the model instead of wrapping into an enormous one.
  if (*context_window < 0 || *max_output_tokens < 0) {
    return std::nullopt;
  }
  CustomProviderModel model;
  model.model_id = *model_id;
  model.display_name = *display_name;
  model.context_window = static_cast<uint32_t>(*context_window);
  model.max_output_tokens = static_cast<uint32_t>(*max_output_tokens);
  model.reasoning = *reasoning;
  model.tool_calling = *tool_calling;
  return model;
}

} // namespace

std::vector<CustomProviderEndpoint>
ReadCustomProviderEndpoints(const PrefService &prefs) {
  std::vector<CustomProviderEndpoint> registered;
  const base::DictValue &stored =
      prefs.GetDict(profile_preferences::kCustomProviderEndpoints);
  for (const auto [provider_id, value] : stored) {
    if (registered.size() >= static_cast<size_t>(mojom::kMaxCustomProviders)) {
      break;
    }
    const std::string *endpoint = AddressIn(value);
    if (provider_id.empty() || !endpoint || !ReadsBackAsAnAddress(*endpoint)) {
      continue;
    }
    registered.push_back(CustomProviderEndpoint{provider_id, *endpoint});
  }
  return registered;
}

std::optional<std::string>
ReadCustomProviderEndpoint(const PrefService &prefs,
                           const std::string &provider_id) {
  if (provider_id.empty()) {
    return std::nullopt;
  }
  for (const CustomProviderEndpoint &entry :
       ReadCustomProviderEndpoints(prefs)) {
    if (entry.provider_id == provider_id) {
      return entry.endpoint;
    }
  }
  return std::nullopt;
}

std::vector<CustomProviderDefinition>
ReadCustomProviderDefinitions(const PrefService &prefs) {
  std::vector<CustomProviderDefinition> defined;
  const base::DictValue &stored =
      prefs.GetDict(profile_preferences::kCustomProviderEndpoints);
  for (const auto [provider_id, value] : stored) {
    if (defined.size() >= static_cast<size_t>(mojom::kMaxCustomProviders)) {
      break;
    }
    const base::DictValue *row = value.GetIfDict();
    if (provider_id.empty() || !row) {
      continue;
    }
    const std::string *endpoint = row->FindString(kEndpointKey);
    const std::string *display_name = row->FindString(kDisplayNameKey);
    const std::string *wire_api_name = row->FindString(kWireApiKey);
    const base::ListValue *models = row->FindList(kModelsKey);
    if (!endpoint || !ReadsBackAsAnAddress(*endpoint) || !display_name ||
        display_name->empty() ||
        display_name->size() > mojom::kMaxProviderDisplayNameBytes ||
        !wire_api_name || !models ||
        models->size() > static_cast<size_t>(mojom::kMaxCustomModelEntries)) {
      continue;
    }
    const std::optional<api::ProviderWireApiView> wire_api =
        WireApiFromName(*wire_api_name);
    if (!wire_api) {
      continue;
    }
    CustomProviderDefinition definition;
    definition.provider_id = provider_id;
    definition.display_name = *display_name;
    definition.endpoint = *endpoint;
    definition.wire_api = *wire_api;
    if (const std::string *handle = row->FindString(kCredentialHandleKey)) {
      // Absent and empty are different answers here: an endpoint that needs no
      // key is not one whose key is the empty string, and only the first is a
      // shape the save may carry.
      if (handle->empty() ||
          handle->size() > static_cast<size_t>(mojom::kMaxIdentifierBytes)) {
        continue;
      }
      definition.credential_handle = *handle;
    }
    if (const std::string *detected = row->FindString(kDetectedServerKey)) {
      definition.detected_server = ServerKindFromName(*detected);
      // A runtime name this build cannot spell drops the row rather than the
      // field. Absent means the surface recognised nothing, which is a claim,
      // and a name that failed to read is not that claim.
      if (!definition.detected_server) {
        continue;
      }
    }
    bool whole = true;
    for (const base::Value &entry : *models) {
      std::optional<CustomProviderModel> model = ModelIn(entry);
      if (!model) {
        whole = false;
        break;
      }
      definition.models.push_back(*std::move(model));
    }
    if (!whole) {
      continue;
    }
    defined.push_back(std::move(definition));
  }
  return defined;
}

bool WriteCustomProviderDefinition(PrefService *prefs,
                                   const CustomProviderDefinition &definition) {
  if (!prefs || definition.provider_id.empty() ||
      ClassifyCustomProviderEndpoint(definition.endpoint) !=
          CustomEndpointRefusal::kNone ||
      definition.models.size() >
          static_cast<size_t>(mojom::kMaxCustomModelEntries)) {
    return false;
  }
  const std::string_view wire_api_name = NameForWireApi(definition.wire_api);
  if (wire_api_name.empty()) {
    return false;
  }
  base::DictValue stored =
      prefs->GetDict(profile_preferences::kCustomProviderEndpoints).Clone();
  // The bound refuses a new provider and never an existing one, as the address
  // write does and for the same reason.
  if (!stored.contains(definition.provider_id) &&
      stored.size() >= static_cast<size_t>(mojom::kMaxCustomProviders)) {
    return false;
  }
  base::DictValue row;
  row.Set(kEndpointKey, definition.endpoint);
  row.Set(kDisplayNameKey, definition.display_name);
  row.Set(kWireApiKey, wire_api_name);
  if (definition.credential_handle) {
    row.Set(kCredentialHandleKey, *definition.credential_handle);
  }
  if (definition.detected_server) {
    const std::string_view detected =
        NameForServerKind(*definition.detected_server);
    if (detected.empty()) {
      return false;
    }
    row.Set(kDetectedServerKey, detected);
  }
  base::ListValue models;
  for (const CustomProviderModel &model : definition.models) {
    base::DictValue entry;
    entry.Set(kModelIdKey, model.model_id);
    entry.Set(kDisplayNameKey, model.display_name);
    // Written as signed because a preference value is, and read back with the
    // negative case refused rather than wrapped.
    entry.Set(kContextWindowKey, static_cast<int>(model.context_window));
    entry.Set(kMaxOutputTokensKey, static_cast<int>(model.max_output_tokens));
    entry.Set(kReasoningKey, model.reasoning);
    entry.Set(kToolCallingKey, model.tool_calling);
    models.Append(std::move(entry));
  }
  row.Set(kModelsKey, std::move(models));
  stored.Set(definition.provider_id, std::move(row));
  prefs->SetDict(profile_preferences::kCustomProviderEndpoints,
                 std::move(stored));
  return true;
}

bool WriteCustomProviderEndpoint(PrefService *prefs,
                                 const std::string &provider_id,
                                 const std::string &endpoint) {
  if (!prefs || provider_id.empty() ||
      ClassifyCustomProviderEndpoint(endpoint) !=
          CustomEndpointRefusal::kNone) {
    return false;
  }
  base::DictValue stored =
      prefs->GetDict(profile_preferences::kCustomProviderEndpoints).Clone();
  // The bound refuses a new provider and never an existing one. A person who
  // has filled the register may still correct an address they already
  // registered; what they may not do is add a thirty-third provider the
  // roster could not carry.
  if (!stored.contains(provider_id) &&
      stored.size() >= static_cast<size_t>(mojom::kMaxCustomProviders)) {
    return false;
  }
  // The address alone, into whichever shape the row is already in. Replacing a
  // whole definition with a bare address here would leave the provider
  // registered and unreplayable — reachable in this generation and gone from
  // the next one, with nothing to say why.
  if (base::DictValue *row = stored.FindDict(provider_id)) {
    row->Set(kEndpointKey, endpoint);
  } else {
    stored.Set(provider_id, endpoint);
  }
  prefs->SetDict(profile_preferences::kCustomProviderEndpoints,
                 std::move(stored));
  return true;
}

RegisteredEndpointLookup CustomProviderEndpointLookup(PrefService *prefs) {
  if (!prefs) {
    return RegisteredEndpointLookup();
  }
  return base::BindRepeating(
      [](PrefService *held, const std::string &provider_id) {
        return ReadCustomProviderEndpoint(*held, provider_id);
      },
      base::Unretained(prefs));
}

bool ForgetCustomProviderEndpoint(PrefService *prefs,
                                  const std::string &provider_id) {
  if (!prefs || provider_id.empty()) {
    return false;
  }
  base::DictValue stored =
      prefs->GetDict(profile_preferences::kCustomProviderEndpoints).Clone();
  stored.Remove(provider_id);
  prefs->SetDict(profile_preferences::kCustomProviderEndpoints,
                 std::move(stored));
  return true;
}

} // namespace taffy
