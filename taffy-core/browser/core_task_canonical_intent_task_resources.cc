// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <vector>

#include "taffy/browser/core_task_canonical_intent.h"
#include "taffy/browser/core_task_canonical_intent_internal.h"

namespace taffy {

using canonical_intent_internal::BytesToString;
using canonical_intent_internal::CanonicalField;
using canonical_intent_internal::HasExactTopLevelShape;
using canonical_intent_internal::IsBoundedCanonicalText;
using canonical_intent_internal::kMaxCanonicalIdentifierBytes;
using canonical_intent_internal::ParseIntent;
using canonical_intent_internal::ReadU64;

std::optional<CanonicalDownloadLinkIntent> ReadCanonicalDownloadLinkIntent(
    const std::vector<uint8_t>& canonical_intent) {
  const auto fields = ParseIntent(canonical_intent);
  if (!fields || fields->size() != 9u || (*fields)[8].tag != 8u ||
      !IsBoundedCanonicalText((*fields)[8].value, kMaxCanonicalIdentifierBytes,
                              true)) {
    return std::nullopt;
  }
  auto target = canonical_intent_internal::ReadObservedNodeHandle(*fields, 30u);
  if (!target || target->expected_origin_is_opaque ||
      target->graph_revision == 0u) {
    return std::nullopt;
  }
  return CanonicalDownloadLinkIntent{*target,
                                     BytesToString((*fields)[8].value)};
}

std::optional<CanonicalTaskTabIntent> ReadCanonicalTaskTabIntent(
    const std::vector<uint8_t>& canonical_intent) {
  const std::optional<std::vector<CanonicalField>> fields =
      ParseIntent(canonical_intent);
  if (!fields || fields->empty() || (*fields)[0].tag != 0u ||
      (*fields)[0].value.size() != 1u) {
    return std::nullopt;
  }
  const uint8_t operation = (*fields)[0].value.front();
  if (operation == 5u) {
    if (fields->size() != 3u || (*fields)[1].tag != 1u ||
        (*fields)[2].tag != 2u ||
        !IsBoundedCanonicalText((*fields)[1].value,
                                kMaxCanonicalIdentifierBytes, true) ||
        !IsBoundedCanonicalText((*fields)[2].value,
                                kMaxCanonicalIdentifierBytes, true)) {
      return std::nullopt;
    }
    return CanonicalTaskTabIntent{
        .operation_tag = operation,
        .context_tab_id = BytesToString((*fields)[1].value),
        .browser_session_id = BytesToString((*fields)[2].value),
    };
  }
  if ((operation != 6u && operation != 7u) || fields->size() != 7u) {
    return std::nullopt;
  }
  for (size_t index = 1u; index <= 5u; ++index) {
    if ((*fields)[index].tag != index ||
        !IsBoundedCanonicalText((*fields)[index].value,
                                kMaxCanonicalIdentifierBytes, true)) {
      return std::nullopt;
    }
  }
  if ((*fields)[6].tag != 6u) {
    return std::nullopt;
  }
  const std::optional<uint64_t> revision = ReadU64((*fields)[6].value);
  if (!revision || *revision == 0u) {
    return std::nullopt;
  }
  return CanonicalTaskTabIntent{
      .operation_tag = operation,
      .context_tab_id = BytesToString((*fields)[1].value),
      .browser_session_id = BytesToString((*fields)[2].value),
      .target_tab_id = BytesToString((*fields)[3].value),
      .frame_id = BytesToString((*fields)[4].value),
      .page_epoch = BytesToString((*fields)[5].value),
      .graph_revision = *revision,
  };
}

bool CanonicalTaskTabListIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& context_tab_id,
    const std::string& browser_session_id) {
  const std::optional<CanonicalTaskTabIntent> parsed =
      ReadCanonicalTaskTabIntent(canonical_intent);
  return parsed && parsed->operation_tag == 5u &&
         parsed->context_tab_id == context_tab_id &&
         parsed->browser_session_id == browser_session_id &&
         !parsed->target_tab_id && !parsed->frame_id && !parsed->page_epoch &&
         !parsed->graph_revision;
}

bool CanonicalTaskTabTargetIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    uint8_t operation_tag,
    const std::string& context_tab_id,
    const std::string& browser_session_id,
    const std::string& target_tab_id,
    const std::string& frame_id,
    const std::string& page_epoch,
    uint64_t graph_revision) {
  const std::optional<CanonicalTaskTabIntent> parsed =
      ReadCanonicalTaskTabIntent(canonical_intent);
  return (operation_tag == 6u || operation_tag == 7u) && parsed &&
         parsed->operation_tag == operation_tag &&
         parsed->context_tab_id == context_tab_id &&
         parsed->browser_session_id == browser_session_id &&
         parsed->target_tab_id == target_tab_id &&
         parsed->frame_id == frame_id && parsed->page_epoch == page_epoch &&
         parsed->graph_revision == graph_revision;
}

std::optional<CanonicalTaskDownloadIntent> ReadCanonicalTaskDownloadIntent(
    const std::vector<uint8_t>& canonical_intent) {
  const std::optional<std::vector<CanonicalField>> fields =
      ParseIntent(canonical_intent);
  if (!fields || fields->empty() || (*fields)[0].tag != 0u ||
      (*fields)[0].value.size() != 1u) {
    return std::nullopt;
  }
  const uint8_t operation = (*fields)[0].value.front();
  if (operation == 15u) {
    if (fields->size() != 4u || (*fields)[1].tag != 1u ||
        (*fields)[2].tag != 2u || (*fields)[3].tag != 3u ||
        !IsBoundedCanonicalText((*fields)[1].value,
                                kMaxCanonicalIdentifierBytes, true) ||
        (*fields)[2].value.empty() ||
        !IsBoundedCanonicalText((*fields)[3].value,
                                kMaxCanonicalIdentifierBytes, true)) {
      return std::nullopt;
    }
    return CanonicalTaskDownloadIntent{
        .operation_tag = operation,
        .context_tab_id = BytesToString((*fields)[1].value),
        .destination_address = BytesToString((*fields)[2].value),
        .browser_session_id = BytesToString((*fields)[3].value),
        .download_id = std::nullopt,
    };
  }
  if (operation == 27u) {
    if (fields->size() != 4u || (*fields)[1].tag != 1u ||
        (*fields)[2].tag != 2u || (*fields)[3].tag != 3u ||
        !IsBoundedCanonicalText((*fields)[1].value,
                                kMaxCanonicalIdentifierBytes, true) ||
        !IsBoundedCanonicalText((*fields)[2].value,
                                kMaxCanonicalIdentifierBytes, true) ||
        !IsBoundedCanonicalText((*fields)[3].value,
                                kMaxCanonicalIdentifierBytes, true)) {
      return std::nullopt;
    }
    return CanonicalTaskDownloadIntent{
        .operation_tag = operation,
        .context_tab_id = BytesToString((*fields)[1].value),
        .browser_session_id = BytesToString((*fields)[2].value),
        .download_id = BytesToString((*fields)[3].value),
    };
  }
  if (operation != 16u || !HasExactTopLevelShape(*fields, operation) ||
      !IsBoundedCanonicalText((*fields)[1].value, kMaxCanonicalIdentifierBytes,
                              true) ||
      !IsBoundedCanonicalText((*fields)[2].value, kMaxCanonicalIdentifierBytes,
                              true)) {
    return std::nullopt;
  }
  return CanonicalTaskDownloadIntent{
      .operation_tag = operation,
      .context_tab_id = BytesToString((*fields)[1].value),
      .browser_session_id = BytesToString((*fields)[2].value),
      .download_id = std::nullopt,
  };
}

bool CanonicalDownloadStartIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& context_tab_id,
    const std::string& destination_address,
    const std::string& browser_session_id) {
  const std::optional<CanonicalTaskDownloadIntent> parsed =
      ReadCanonicalTaskDownloadIntent(canonical_intent);
  return parsed && parsed->operation_tag == 15u &&
         parsed->context_tab_id == context_tab_id &&
         parsed->destination_address == destination_address &&
         parsed->browser_session_id == browser_session_id &&
         !parsed->download_id;
}

bool CanonicalDownloadListIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& context_tab_id,
    const std::string& browser_session_id) {
  const std::optional<CanonicalTaskDownloadIntent> parsed =
      ReadCanonicalTaskDownloadIntent(canonical_intent);
  return parsed && parsed->operation_tag == 16u &&
         parsed->context_tab_id == context_tab_id &&
         !parsed->destination_address &&
         parsed->browser_session_id == browser_session_id &&
         !parsed->download_id;
}

bool CanonicalDownloadCancelIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& context_tab_id,
    const std::string& browser_session_id,
    const std::string& download_id) {
  const std::optional<CanonicalTaskDownloadIntent> parsed =
      ReadCanonicalTaskDownloadIntent(canonical_intent);
  return parsed && parsed->operation_tag == 27u &&
         parsed->context_tab_id == context_tab_id &&
         !parsed->destination_address &&
         parsed->browser_session_id == browser_session_id &&
         parsed->download_id == download_id;
}

}  // namespace taffy
