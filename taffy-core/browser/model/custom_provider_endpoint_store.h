// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_MODEL_CUSTOM_PROVIDER_ENDPOINT_STORE_H_
#define TAFFY_BROWSER_MODEL_CUSTOM_PROVIDER_ENDPOINT_STORE_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <vector>

#include "taffy/browser/core_model_effect_validation.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"

class PrefService;

namespace taffy {

// One registered address: the provider it belongs to, and the exact string
// the person typed.
//
// The endpoint is a string and not a GURL on purpose. What is registered is
// what was typed; a parsed and re-serialized form would be a second spelling
// of the same address, and the whole value of the register is that there is
// exactly one.
struct CustomProviderEndpoint {
  std::string provider_id;
  std::string endpoint;
};

// Every registered address, in the order the preference dictionary iterates.
//
// A row is either the whole definition or the address alone, and both answer
// here: the address is the field this function reads, wherever it sits. A row
// that does not read back as an address is skipped rather than repaired — the
// file is the browser's own, but it is still a file, and an address nobody
// typed must not become one the core may name because a torn value happened to
// parse. The result is bounded by `kMaxCustomProviders` for the same reason
// the write is — a register with more rows than there can be custom providers
// is a register describing providers that cannot exist.
//
// What is *not* re-checked here is the address policy. A row is dropped for
// being unreadable, never for being unacceptable: judging the address on the
// way out would be a second authority over it, and two authorities that can
// disagree are how an endpoint a person saved becomes one they cannot use.
std::vector<CustomProviderEndpoint>
ReadCustomProviderEndpoints(const PrefService &prefs);

// The address registered for one provider, or nothing.
//
// Answered from `ReadCustomProviderEndpoints` rather than by reaching into the
// dictionary, so the single lookup and the listing can never disagree about a
// row — including about which rows a bound leaves out.
std::optional<std::string>
ReadCustomProviderEndpoint(const PrefService &prefs,
                           const std::string &provider_id);

// One model a person's own provider carries, exactly as the setup screen
// stated it (decision 0096 section 4).
struct CustomProviderModel {
  std::string model_id;
  std::string display_name;
  uint32_t context_window = 0;
  uint32_t max_output_tokens = 0;
  bool reasoning = false;
  bool tool_calling = false;
};

// Everything one save said about a person's own provider, less its key.
//
// The address is the same string `CustomProviderEndpoint` carries and the same
// row holds it: a save states these together, and two rows could come to
// disagree about one provider. Nothing here is secret — the sealed record
// stays in the Android store and this row names it, if it names one at all.
struct CustomProviderDefinition {
  std::string provider_id;
  std::string display_name;
  std::string endpoint;
  core_api::mojom::ProviderWireApiView wire_api =
      core_api::mojom::ProviderWireApiView::kOpenAiCompletions;
  std::optional<std::string> credential_handle;
  std::vector<CustomProviderModel> models;
  std::optional<core_api::mojom::ServerKindView> detected_server;
};

// Every provider a person defined, whole, for the replay that tells a new core
// generation about them (decision 0117).
//
// A row that carries only an address is skipped rather than completed. It is
// still an address this browser registered and `ReadCustomProviderEndpoints`
// still answers with it; what it is not is a provider definition, and
// inventing a name, a wire family and an empty model list for one would file a
// provider a person can select and nothing can route to — the state decision
// 0096 section 4 exists to prevent.
std::vector<CustomProviderDefinition>
ReadCustomProviderDefinitions(const PrefService &prefs);

// Registers one address against one provider, replacing any it already had.
//
// This is where the address policy is applied, because this is registration:
// `custom_provider_endpoint_policy.h` decides what may enter the register and
// nothing decides again afterwards. False means nothing was written, and the
// caller must not forward a save either — a core holding an endpoint this file
// does not hold would name an address the browser would then refuse to send
// to, and the refusal would arrive with nothing a person could read.
bool WriteCustomProviderEndpoint(PrefService *prefs,
                                 const std::string &provider_id,
                                 const std::string &endpoint);

// Registers the whole definition, address included, in one row.
//
// This is what the save writes; `WriteCustomProviderEndpoint` remains for the
// one thing that only has an address to register. The address policy decides
// this write too, so a definition carrying an address the register may not
// hold is refused whole rather than filed without one.
bool WriteCustomProviderDefinition(PrefService *prefs,
                                   const CustomProviderDefinition &definition);

// Removes one provider's registered address. True when the register no longer
// holds one, which includes a provider that never had one: the state
// afterwards is the state that was asked for.
bool ForgetCustomProviderEndpoint(PrefService *prefs,
                                  const std::string &provider_id);

// This register, as the thing that decides whether a model request may be
// sent asks it (`core_model_effect_validation.h`).
//
// The binding is made here rather than at the seam it is installed on so that
// the one place that knows this file reads a preference service is also the
// one place that states the lifetime it relies on: `prefs` must outlive the
// broker the lookup is installed on, which on a profile it does — the pref
// service outlives every keyed service. A null `prefs` yields a null lookup,
// which refuses every request naming a person's own address rather than
// answering one without a register.
RegisteredEndpointLookup CustomProviderEndpointLookup(PrefService *prefs);

} // namespace taffy

#endif // TAFFY_BROWSER_MODEL_CUSTOM_PROVIDER_ENDPOINT_STORE_H_
