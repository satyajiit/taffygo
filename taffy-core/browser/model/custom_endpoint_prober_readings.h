// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_MODEL_CUSTOM_ENDPOINT_PROBER_READINGS_H_
#define TAFFY_BROWSER_MODEL_CUSTOM_ENDPOINT_PROBER_READINGS_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <vector>

namespace taffy {

// One model an OpenAI-shaped listing named.
//
// It carries only explicitly stated facts: vLLM's `max_model_len`, or the
// context/output limits and `supported_parameters` that the catalog's
// OpenRouter-shaped listing dialect also reads. Zero and false mean no usable
// claim was read; neither a runtime name nor a model name grants capabilities.
//
// Listings need not carry a display name. The contract projection uses the
// identity, so every retained row has a name the server actually supplied.
struct CustomModelReading {
  std::string model_id;
  uint32_t context_window = 0;
  uint32_t max_output_tokens = 0;
  bool reasoning = false;
  bool tool_calling = false;
};

// What an OpenAI-shaped model listing said.
//
// `understood` is the only thing that says the endpoint answered: a body that
// is not a listing at all is a different fact from a listing with nothing in
// it, and only the second is an answer about the server's models.
//
// `model_count` and `models` are separate facts and stay separate (decision
// 0096 section 5, and decision 0098 section 4): the count is what the server
// named and the list is what survived the bound and the identity rule. Reading
// the list's length as the count is the silent truncation both records refuse.
struct OpenAiListingReading {
  bool understood = false;
  uint32_t model_count = 0;
  // A `max_model_len` on any entry. It is vLLM's own word about itself, which
  // is the only kind of evidence this file accepts — a port number or a
  // banner would be a guess dressed as a reading.
  bool names_vllm = false;
  std::vector<CustomModelReading> models;
};

// Reads `{"data":[...]}`, the listing every OpenAI-compatible server serves.
// The count is bounded by `kMaxCustomModelEntries`, because a saved provider
// cannot carry more than that many models and a count above it would be a
// number no later screen could act on.
OpenAiListingReading ReadOpenAiListing(const std::string& body);

// Reads Ollama's `/api/tags`: `{"models":[...]}`. Nothing means the body was
// not that, which is how this path says "not Ollama" rather than "no models".
std::optional<uint32_t> ReadOllamaTags(const std::string& body);

// Reads llama.cpp's `/props`. The server answering that path with a JSON
// object is the whole of the detection — the path exists on no other runtime
// in this set, and what it contains is a generation-settings dump this
// product has no use for.
bool ReadLlamaCppProps(const std::string& body);

}  // namespace taffy

#endif  // TAFFY_BROWSER_MODEL_CUSTOM_ENDPOINT_PROBER_READINGS_H_
