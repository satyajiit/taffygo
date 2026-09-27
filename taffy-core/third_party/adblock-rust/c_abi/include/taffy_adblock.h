// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_THIRD_PARTY_ADBLOCK_RUST_C_ABI_INCLUDE_TAFFY_ADBLOCK_H_
#define TAFFY_THIRD_PARTY_ADBLOCK_RUST_C_ABI_INCLUDE_TAFFY_ADBLOCK_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque matching engine. Immutable after creation; safe to share across
// threads (the crate is built without `single-thread`).
typedef struct TaffyAdblockEngine TaffyAdblockEngine;

typedef struct TaffyAdblockCompileStats {
  uint64_t rules_indexed;
  uint64_t cosmetic_rules;
  uint64_t parse_errors;
} TaffyAdblockCompileStats;

typedef struct TaffyAdblockMatchResult {
  bool matched;
  bool important;
  bool has_exception;
} TaffyAdblockMatchResult;

// A UTF-8 view whose bytes need not be NUL-terminated. Null is valid only for
// an empty view. Matching accepts views so the network hot path does not copy
// every URL and hostname merely to append terminators for this ABI.
typedef struct TaffyAdblockStringView {
  const char* data;
  size_t len;
} TaffyAdblockStringView;

// Builds an engine from EasyList-syntax text. `stats` may be null. Returns
// null when `bytes` is not UTF-8.
TaffyAdblockEngine* taffy_adblock_engine_create_from_list(
    const uint8_t* bytes,
    size_t len,
    TaffyAdblockCompileStats* stats);

// Reloads an engine from `Engine::serialize` bytes. Returns null when the
// buffer fails verification (truncated, edited, or a version this crate
// cannot read).
TaffyAdblockEngine* taffy_adblock_engine_create_from_serialized(
    const uint8_t* bytes,
    size_t len);

void taffy_adblock_engine_destroy(TaffyAdblockEngine* engine);

// Serialised engine bytes owned by the Rust allocator. Caller frees only with
// `taffy_adblock_bytes_free`. Returns null on failure.
uint8_t* taffy_adblock_engine_serialize(const TaffyAdblockEngine* engine,
                                        size_t* len);

void taffy_adblock_bytes_free(uint8_t* ptr, size_t len);

TaffyAdblockMatchResult taffy_adblock_engine_matches(
    const TaffyAdblockEngine* engine,
    TaffyAdblockStringView url,
    TaffyAdblockStringView hostname,
    TaffyAdblockStringView initiator_hostname,
    TaffyAdblockStringView request_type,
    bool third_party,
    TaffyAdblockStringView method,
    bool disable_generic_rules);

// Cosmetic resources for `url` as JSON. Caller frees with
// `taffy_adblock_string_free`.
char* taffy_adblock_engine_url_cosmetic_resources(
    const TaffyAdblockEngine* engine,
    TaffyAdblockStringView url);

// JSON array of hide selectors for observed classes and ids. Caller frees
// with `taffy_adblock_string_free`.
char* taffy_adblock_engine_hidden_class_id_selectors(
    const TaffyAdblockEngine* engine,
    const char* const* classes,
    size_t n_classes,
    const char* const* ids,
    size_t n_ids,
    const char* const* exceptions,
    size_t n_exceptions);

// `$generichide` exception for the document URL.
bool taffy_adblock_engine_generic_hide(const TaffyAdblockEngine* engine,
                                       TaffyAdblockStringView url);

// `$genericblock` exception for the document URL.
bool taffy_adblock_engine_generic_block(const TaffyAdblockEngine* engine,
                                        TaffyAdblockStringView url);

void taffy_adblock_string_free(char* ptr);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // TAFFY_THIRD_PARTY_ADBLOCK_RUST_C_ABI_INCLUDE_TAFFY_ADBLOCK_H_
