// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_task_canonical_intent.h"

#include <optional>
#include <utility>
#include <vector>

#include "taffy/browser/core_task_canonical_intent_internal.h"

namespace taffy {

using canonical_intent_internal::BytesEqual;
using canonical_intent_internal::BytesToString;
using canonical_intent_internal::CanonicalField;
using canonical_intent_internal::HasExactTopLevelShape;
using canonical_intent_internal::IsBoundedCanonicalText;
using canonical_intent_internal::IsCanonicalOptionalDomQueryLimit;
using canonical_intent_internal::IsCanonicalOptionalDomQueryText;
using canonical_intent_internal::IsCanonicalOptionalDomRole;
using canonical_intent_internal::IsCanonicalOptionalIdentifier;
using canonical_intent_internal::kMaxCanonicalIdentifierBytes;
using canonical_intent_internal::ParseIntent;
using canonical_intent_internal::ReadObservedNodeHandle;
using canonical_intent_internal::ReadU32;
using canonical_intent_internal::ReadU64;
using canonical_intent_internal::SearchMatches;

bool CanonicalSearchIntentMatches(const std::vector<uint8_t>& canonical_intent,
                                  const std::string& tab_id,
                                  const std::string& operand_handle,
                                  const std::string& transient_query) {
  if (operand_handle.empty()) {
    return false;
  }
  return SearchMatches(canonical_intent, tab_id,
                       std::optional<std::string_view>(operand_handle),
                       transient_query);
}

bool CanonicalSearchQueryMatches(const std::vector<uint8_t>& canonical_intent,
                                 const std::string& tab_id,
                                 const std::string& transient_query) {
  return SearchMatches(canonical_intent, tab_id, std::nullopt, transient_query);
}

bool CanonicalTabsOpenIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& context_tab_id,
    const std::string& destination_address) {
  const std::optional<std::vector<CanonicalField>> fields =
      ParseIntent(canonical_intent);
  return fields && HasExactTopLevelShape(*fields, 4u) &&
         BytesEqual((*fields)[1].value, context_tab_id) &&
         (*fields)[2].value.size() == destination_address.size() + 1u &&
         (*fields)[2].value.front() == 1u &&
         BytesEqual((*fields)[2].value.subspan(1u), destination_address);
}

bool CanonicalInTabNavigateIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& tab_id,
    const std::string& destination_address) {
  const auto fields = ParseIntent(canonical_intent);
  return !tab_id.empty() && !destination_address.empty() && fields &&
         fields->size() == 4u && (*fields)[0].tag == 0u &&
         (*fields)[0].value.size() == 1u && (*fields)[0].value.front() == 0u &&
         (*fields)[1].tag == 1u && BytesEqual((*fields)[1].value, tab_id) &&
         (*fields)[2].tag == 2u &&
         BytesEqual((*fields)[2].value, destination_address) &&
         (*fields)[3].tag == 3u && (*fields)[3].value.size() == 1u &&
         (*fields)[3].value.front() == 0u;
}

bool CanonicalTabControlIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    uint8_t operation_tag,
    const std::string& context_tab_id) {
  if (operation_tag != 2u && operation_tag != 3u && operation_tag != 28u &&
      operation_tag != 29u) {
    return false;
  }
  const std::optional<std::vector<CanonicalField>> fields =
      ParseIntent(canonical_intent);
  return !context_tab_id.empty() && fields && fields->size() == 2u &&
         (*fields)[0].tag == 0u && (*fields)[0].value.size() == 1u &&
         (*fields)[0].value.front() == operation_tag &&
         (*fields)[1].tag == 1u &&
         BytesEqual((*fields)[1].value, context_tab_id);
}

bool CanonicalLibraryIntentMatches(const std::vector<uint8_t>& canonical_intent,
                                   uint8_t operation_kind,
                                   const std::string& context_tab_id) {
  const std::optional<std::vector<CanonicalField>> fields =
      ParseIntent(canonical_intent);
  if (!fields || fields->size() < 3u || (*fields)[0].tag != 0u ||
      (*fields)[0].value.size() != 1u || (*fields)[0].value.front() != 254u ||
      (*fields)[1].tag != 1u || (*fields)[1].value.size() != 1u ||
      (*fields)[1].value.front() != operation_kind || (*fields)[2].tag != 2u ||
      !IsBoundedCanonicalText((*fields)[2].value, kMaxCanonicalIdentifierBytes,
                              true) ||
      !BytesEqual((*fields)[2].value, context_tab_id)) {
    return false;
  }
  if (operation_kind == 0u) {
    if (fields->size() != 5u || (*fields)[3].tag != 3u ||
        (*fields)[4].tag != 4u) {
      return false;
    }
    const std::optional<std::vector<CanonicalField>> opaque =
        canonical_intent_internal::ParseNestedFields((*fields)[3].value);
    const std::optional<uint32_t> limit = ReadU32((*fields)[4].value);
    return opaque && opaque->size() == 3u && (*opaque)[0].tag == 0u &&
           IsBoundedCanonicalText((*opaque)[0].value, 96u, true) &&
           (*opaque)[1].tag == 1u && (*opaque)[1].value.size() == 1u &&
           (*opaque)[1].value.front() == 2u && (*opaque)[2].tag == 2u &&
           (*opaque)[2].value.size() == 32u && limit && *limit > 0u &&
           *limit <= 32u;
  }
  if (operation_kind == 1u) {
    const std::optional<uint64_t> workspace_revision =
        fields->size() == 7u ? ReadU64((*fields)[4].value) : std::nullopt;
    const std::optional<uint64_t> entry_revision =
        fields->size() == 7u ? ReadU64((*fields)[6].value) : std::nullopt;
    return fields->size() == 7u && (*fields)[3].tag == 3u &&
           IsBoundedCanonicalText((*fields)[3].value,
                                  kMaxCanonicalIdentifierBytes, true) &&
           (*fields)[4].tag == 4u && workspace_revision &&
           *workspace_revision > 0u && (*fields)[5].tag == 5u &&
           IsBoundedCanonicalText((*fields)[5].value,
                                  kMaxCanonicalIdentifierBytes, true) &&
           (*fields)[6].tag == 6u && entry_revision;
  }
  if (operation_kind == 2u) {
    const std::optional<uint64_t> entry_revision =
        fields->size() == 5u ? ReadU64((*fields)[4].value) : std::nullopt;
    return fields->size() == 5u && (*fields)[3].tag == 3u &&
           IsBoundedCanonicalText((*fields)[3].value,
                                  kMaxCanonicalIdentifierBytes, true) &&
           (*fields)[4].tag == 4u && entry_revision && *entry_revision > 0u;
  }
  return false;
}

bool CanonicalDomQueryIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& tab_id) {
  const std::optional<std::vector<CanonicalField>> fields =
      ParseIntent(canonical_intent);
  return !tab_id.empty() && fields && fields->size() == 6u &&
         (*fields)[0].tag == 0u && (*fields)[0].value.size() == 1u &&
         (*fields)[0].value.front() == 8u && (*fields)[1].tag == 1u &&
         BytesEqual((*fields)[1].value, tab_id) && (*fields)[2].tag == 2u &&
         IsCanonicalOptionalIdentifier((*fields)[2].value) &&
         (*fields)[3].tag == 3u &&
         IsCanonicalOptionalDomRole((*fields)[3].value) &&
         (*fields)[4].tag == 4u &&
         IsCanonicalOptionalDomQueryText((*fields)[4].value) &&
         (*fields)[5].tag == 5u &&
         IsCanonicalOptionalDomQueryLimit((*fields)[5].value);
}

std::optional<CanonicalDomActivationIntent> ReadCanonicalDomActivationIntent(
    const std::vector<uint8_t>& canonical_intent) {
  const std::optional<std::vector<CanonicalField>> fields =
      ParseIntent(canonical_intent);
  // Field 8 is the disclosure claim: 0 expanded, 1 collapsed, 2 none. The
  // third value is appended rather than shaped as one of the optional field
  // encodings because this intent is durable — it is journaled and replayed —
  // and changing a field's shape would strand every record already written.
  // An older reader meets 2 and answers InvalidValue, which is the closed-
  // enumeration rule doing its job rather than a compatibility accident.
  if (!fields || fields->size() != 9u || (*fields)[8].tag != 8u ||
      (*fields)[8].value.size() != 1u || (*fields)[8].value.front() > 2u) {
    return std::nullopt;
  }
  std::optional<CanonicalObservedNodeHandle> target =
      ReadObservedNodeHandle(*fields, 10u);
  if (!target) {
    return std::nullopt;
  }
  const uint8_t disclosure = (*fields)[8].value.front();
  return CanonicalDomActivationIntent{
      .target = std::move(*target),
      .expected_expanded = disclosure == 2u
                               ? std::optional<bool>()
                               : std::optional<bool>(disclosure == 0u),
  };
}

bool CanonicalDomActivationIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& tab_id,
    const std::string& node_id) {
  const std::optional<CanonicalDomActivationIntent> parsed =
      ReadCanonicalDomActivationIntent(canonical_intent);
  return parsed && parsed->target.tab_id == tab_id &&
         parsed->target.node_id == node_id;
}

bool CanonicalDomActivationIntentMatchesDocument(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& tab_id,
    const std::string& frame_id,
    const std::string& page_epoch,
    uint64_t graph_revision,
    const std::string& node_id,
    const std::string& expected_origin) {
  const std::optional<CanonicalDomActivationIntent> parsed =
      ReadCanonicalDomActivationIntent(canonical_intent);
  const CanonicalObservedNodeHandle* target =
      parsed ? &parsed->target : nullptr;
  return target && !target->expected_origin_is_opaque &&
         target->tab_id == tab_id && target->frame_id == frame_id &&
         target->page_epoch == page_epoch &&
         target->graph_revision == graph_revision &&
         target->node_id == node_id &&
         target->expected_origin == expected_origin;
}

std::optional<CanonicalObservedNodeHandle> ReadCanonicalDomFocusHandle(
    const std::vector<uint8_t>& canonical_intent) {
  const std::optional<std::vector<CanonicalField>> fields =
      ParseIntent(canonical_intent);
  if (!fields || fields->size() != 8u) {
    return std::nullopt;
  }
  return ReadObservedNodeHandle(*fields, 25u);
}

bool CanonicalDomFocusIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& tab_id,
    const std::string& node_id) {
  const std::optional<CanonicalObservedNodeHandle> target =
      ReadCanonicalDomFocusHandle(canonical_intent);
  return target && target->tab_id == tab_id && target->node_id == node_id;
}

bool CanonicalDomFocusIntentMatchesDocument(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& tab_id,
    const std::string& frame_id,
    const std::string& page_epoch,
    uint64_t graph_revision,
    const std::string& node_id,
    const std::string& expected_origin) {
  const std::optional<CanonicalObservedNodeHandle> target =
      ReadCanonicalDomFocusHandle(canonical_intent);
  return target && !target->expected_origin_is_opaque &&
         target->tab_id == tab_id && target->frame_id == frame_id &&
         target->page_epoch == page_epoch &&
         target->graph_revision == graph_revision &&
         target->node_id == node_id &&
         target->expected_origin == expected_origin;
}

bool CanonicalFormInspectIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& tab_id,
    const std::string& form_node_id) {
  const std::optional<std::vector<CanonicalField>> fields =
      ParseIntent(canonical_intent);
  return !tab_id.empty() && !form_node_id.empty() && fields &&
         HasExactTopLevelShape(*fields, 12u) &&
         BytesEqual((*fields)[1].value, tab_id) &&
         BytesEqual((*fields)[2].value, form_node_id);
}

bool CanonicalFormSuppliedValueIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    uint8_t operation_tag,
    const std::string& tab_id,
    const std::string& field_node_id,
    const std::string& value_request_id,
    uint32_t supplied_value_index) {
  if (operation_tag != 13u && operation_tag != 23u) {
    return false;
  }
  const std::optional<std::vector<CanonicalField>> fields =
      ParseIntent(canonical_intent);
  if (tab_id.empty() || field_node_id.empty() || value_request_id.empty() ||
      !fields || fields->size() != 5u || (*fields)[0].tag != 0u ||
      (*fields)[0].value.size() != 1u ||
      (*fields)[0].value.front() != operation_tag || (*fields)[1].tag != 1u ||
      !BytesEqual((*fields)[1].value, tab_id) || (*fields)[2].tag != 2u ||
      !BytesEqual((*fields)[2].value, field_node_id) ||
      (*fields)[3].tag != 3u ||
      !IsBoundedCanonicalText((*fields)[3].value, kMaxCanonicalIdentifierBytes,
                              true) ||
      !BytesEqual((*fields)[3].value, value_request_id) ||
      (*fields)[4].tag != 4u) {
    return false;
  }
  const std::optional<uint32_t> encoded = ReadU32((*fields)[4].value);
  return encoded && *encoded == supplied_value_index;
}

bool CanonicalFormToggleIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& tab_id,
    const std::string& field_node_id,
    bool checked) {
  const std::optional<std::vector<CanonicalField>> fields =
      ParseIntent(canonical_intent);
  return !tab_id.empty() && !field_node_id.empty() && fields &&
         fields->size() == 4u && (*fields)[0].tag == 0u &&
         (*fields)[0].value.size() == 1u && (*fields)[0].value.front() == 24u &&
         (*fields)[1].tag == 1u && BytesEqual((*fields)[1].value, tab_id) &&
         (*fields)[2].tag == 2u &&
         BytesEqual((*fields)[2].value, field_node_id) &&
         (*fields)[3].tag == 3u && (*fields)[3].value.size() == 1u &&
         (*fields)[3].value.front() == static_cast<uint8_t>(checked);
}

bool CanonicalFormSubmitIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& tab_id,
    const std::string& submit_control_node_id) {
  const std::optional<std::vector<CanonicalField>> fields =
      ParseIntent(canonical_intent);
  return !tab_id.empty() && !submit_control_node_id.empty() && fields &&
         HasExactTopLevelShape(*fields, 14u) &&
         BytesEqual((*fields)[1].value, tab_id) &&
         BytesEqual((*fields)[2].value, submit_control_node_id);
}

bool CanonicalSelectionReadIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& tab_id) {
  const std::optional<std::vector<CanonicalField>> fields =
      ParseIntent(canonical_intent);
  return !tab_id.empty() && fields && fields->size() == 2u &&
         (*fields)[0].tag == 0u && (*fields)[0].value.size() == 1u &&
         (*fields)[0].value.front() == 17u && (*fields)[1].tag == 1u &&
         BytesEqual((*fields)[1].value, tab_id);
}

bool CanonicalMediaReadIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    uint8_t operation_tag,
    const std::string& tab_id,
    const std::optional<std::string>& node_id) {
  if (tab_id.empty() || !((operation_tag >= 18u && operation_tag <= 21u) ||
                          operation_tag == 26u)) {
    return false;
  }
  const bool node_targeted = operation_tag <= 20u;
  if (node_targeted != node_id.has_value() || (node_id && node_id->empty())) {
    return false;
  }
  const std::optional<std::vector<CanonicalField>> fields =
      ParseIntent(canonical_intent);
  if (!fields || (*fields).size() != (node_targeted ? 3u : 2u) ||
      (*fields)[0].tag != 0u || (*fields)[0].value.size() != 1u ||
      (*fields)[0].value.front() != operation_tag || (*fields)[1].tag != 1u ||
      !BytesEqual((*fields)[1].value, tab_id)) {
    return false;
  }
  return !node_targeted ||
         ((*fields)[2].tag == 2u && BytesEqual((*fields)[2].value, *node_id));
}

std::optional<CanonicalLinkOpenHandle> ReadCanonicalLinkOpenHandle(
    const std::vector<uint8_t>& canonical_intent) {
  const std::optional<std::vector<CanonicalField>> fields =
      ParseIntent(canonical_intent);
  if (!fields || fields->size() != 8u) {
    return std::nullopt;
  }
  return ReadObservedNodeHandle(*fields, 22u);
}

bool CanonicalLinkOpenIntentMatchesProjections(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& tab_id,
    const std::string& node_id) {
  const std::optional<CanonicalLinkOpenHandle> handle =
      ReadCanonicalLinkOpenHandle(canonical_intent);
  return handle && handle->tab_id == tab_id && handle->node_id == node_id;
}

bool CanonicalLinkOpenIntentMatchesDocument(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& tab_id,
    const std::string& frame_id,
    const std::string& page_epoch,
    uint64_t graph_revision,
    const std::string& node_id,
    const std::string& expected_origin) {
  const std::optional<CanonicalLinkOpenHandle> handle =
      ReadCanonicalLinkOpenHandle(canonical_intent);
  return handle && !handle->expected_origin_is_opaque &&
         handle->tab_id == tab_id && handle->frame_id == frame_id &&
         handle->page_epoch == page_epoch &&
         handle->graph_revision == graph_revision &&
         handle->node_id == node_id &&
         handle->expected_origin == expected_origin;
}

}  // namespace taffy
