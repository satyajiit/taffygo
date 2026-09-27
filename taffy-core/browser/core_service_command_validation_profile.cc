// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/strings/utf_string_conversion_utils.h"
#include "taffy/browser/core_service_command_validation_internal.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

bool IsAsciiAlphanumeric(unsigned char character) {
  return (character >= 'a' && character <= 'z') ||
         (character >= 'A' && character <= 'Z') ||
         (character >= '0' && character <= '9');
}

bool IsAssistantAbility(mojom::AssistantAbility ability) {
  switch (ability) {
    case mojom::AssistantAbility::kPagesLookup:
    case mojom::AssistantAbility::kPagesCompare:
    case mojom::AssistantAbility::kPagesSummarize:
    case mojom::AssistantAbility::kPagesTable:
    case mojom::AssistantAbility::kProducts:
    case mojom::AssistantAbility::kOffers:
    case mojom::AssistantAbility::kForm:
    case mojom::AssistantAbility::kDownloads:
    case mojom::AssistantAbility::kPdf:
    case mojom::AssistantAbility::kSheet:
    case mojom::AssistantAbility::kDocument:
    case mojom::AssistantAbility::kDepth:
    case mojom::AssistantAbility::kTrip:
    case mojom::AssistantAbility::kPictures:
    case mojom::AssistantAbility::kVideo:
    case mojom::AssistantAbility::kKeep:
      return true;
  }
  return false;
}

bool IsPersonalityPreset(mojom::PersonalityPreset preset) {
  switch (preset) {
    case mojom::PersonalityPreset::kCarefulResearcher:
    case mojom::PersonalityPreset::kQuickShopper:
    case mojom::PersonalityPreset::kTripPlanner:
      return true;
  }
  return false;
}

bool AddText(const std::string& value,
             size_t maximum,
             bool allow_newline,
             size_t ceiling,
             size_t* total) {
  if (value.size() > maximum) {
    return false;
  }
  for (size_t index = 0; index < value.size(); ++index) {
    base_icu::UChar32 code_point = 0;
    if (!base::ReadUnicodeCharacter(value, &index, &code_point)) {
      return false;
    }
    const bool control = code_point <= 0x1f ||
                         (code_point >= 0x7f && code_point <= 0x9f);
    if (control &&
        !(allow_newline && (code_point == '\n' || code_point == '\r'))) {
      return false;
    }
  }
  return AddBounded(value.size(), ceiling, total);
}

bool AddSavedIdentifier(const std::string& value,
                        size_t ceiling,
                        size_t* total) {
  return !value.empty() && value.size() <= mojom::kMaxIdentifierBytes &&
         std::all_of(value.begin(), value.end(), [](unsigned char character) {
           return IsAsciiAlphanumeric(character) || character == '-' ||
                  character == '_';
         }) &&
         AddBounded(value.size(), ceiling, total);
}

bool AddSavedHost(const std::string& value,
                  size_t ceiling,
                  size_t* total) {
  return !value.empty() && value.size() <= mojom::kMaxSavedSignInSiteBytes &&
         std::all_of(value.begin(), value.end(), [](unsigned char character) {
           return IsAsciiAlphanumeric(character) || character == '.' ||
                  character == '-' || character == '[' || character == ']' ||
                  character == ':';
         }) &&
         AddBounded(value.size(), ceiling, total);
}

bool ValidAvailability(mojom::SavedDataAvailability availability,
                       uint64_t revision,
                       bool empty) {
  switch (availability) {
    case mojom::SavedDataAvailability::kReady:
      return revision > 0u;
    case mojom::SavedDataAvailability::kLoading:
    case mojom::SavedDataAvailability::kUnavailable:
      return revision == 0u && empty;
  }
  return false;
}

std::optional<size_t> AssistantConfigurationByteSize(
    const mojom::SetAssistantConfigurationCommand& configuration,
    size_t ceiling) {
  size_t total = 0u;
  if (configuration.disabled_abilities.size() >
          mojom::kMaxAssistantAbilities ||
      !AddBounded(configuration.disabled_abilities.size(), ceiling, &total) ||
      !IsPersonalityPreset(configuration.preset) ||
      configuration.pace > mojom::kMaxPersonalityScale ||
      configuration.length > mojom::kMaxPersonalityScale ||
      configuration.check_in > mojom::kMaxPersonalityScale) {
    return std::nullopt;
  }
  std::optional<uint32_t> previous;
  for (mojom::AssistantAbility ability : configuration.disabled_abilities) {
    const uint32_t wire = static_cast<uint32_t>(ability);
    if (!IsAssistantAbility(ability) ||
        (previous && wire <= previous.value())) {
      return std::nullopt;
    }
    previous = wire;
  }
  return total;
}

bool AddSignIns(const std::vector<mojom::SavedSignInMetadataPtr>& records,
                size_t ceiling,
                size_t* total) {
  if (records.size() > mojom::kMaxSavedSignIns) {
    return false;
  }
  std::set<std::string> ids;
  for (const auto& record : records) {
    if (!record || !ids.insert(record->id).second ||
        !AddSavedIdentifier(record->id, ceiling, total) ||
        !AddSavedHost(record->site, ceiling, total) ||
        !AddText(record->username, mojom::kMaxSavedSignInUsernameBytes, false,
                 ceiling, total) ||
        record->last_used_epoch_ms >
            static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
      return false;
    }
  }
  return true;
}

bool AddDetail(const mojom::SavedDetailRecord& record,
               size_t ceiling,
               size_t* total) {
  const bool has_value = !record.given_name.empty() ||
                         !record.family_name.empty() || !record.email.empty() ||
                         !record.phone.empty() || !record.address.empty() ||
                         !record.postcode.empty() || !record.country.empty();
  return has_value && AddSavedIdentifier(record.id, ceiling, total) &&
         AddText(record.given_name, mojom::kMaxSavedDetailNameBytes, false,
                 ceiling, total) &&
         AddText(record.family_name, mojom::kMaxSavedDetailNameBytes, false,
                 ceiling, total) &&
         AddText(record.email, mojom::kMaxSavedDetailEmailBytes, false, ceiling,
                 total) &&
         AddText(record.phone, mojom::kMaxSavedDetailPhoneBytes, false, ceiling,
                 total) &&
         AddText(record.address, mojom::kMaxSavedDetailAddressBytes, true,
                 ceiling, total) &&
         AddText(record.postcode, mojom::kMaxSavedDetailPostcodeBytes, false,
                 ceiling, total) &&
         AddText(record.country, mojom::kMaxSavedDetailCountryBytes, false,
                 ceiling, total);
}

bool AddDetails(const std::vector<mojom::SavedDetailRecordPtr>& records,
                size_t ceiling,
                size_t* total) {
  if (records.size() > mojom::kMaxSavedDetails) {
    return false;
  }
  std::set<std::string> ids;
  for (const auto& record : records) {
    if (!record || !ids.insert(record->id).second ||
        !AddDetail(*record, ceiling, total)) {
      return false;
    }
  }
  return true;
}

std::optional<size_t> SavedDataByteSize(
    const mojom::ReplaceSavedDataSnapshotCommand& snapshot,
    size_t ceiling) {
  if (!ValidAvailability(snapshot.sign_ins_availability,
                         snapshot.sign_ins_revision,
                         snapshot.sign_ins.empty()) ||
      !ValidAvailability(snapshot.details_availability,
                         snapshot.details_revision, snapshot.details.empty())) {
    return std::nullopt;
  }
  size_t total = 0u;
  return AddSignIns(snapshot.sign_ins, ceiling, &total) &&
                 AddDetails(snapshot.details, ceiling, &total)
             ? std::optional<size_t>(total)
             : std::nullopt;
}

}  // namespace

std::optional<size_t> ProfileCommandByteSize(
    const mojom::CoreServiceCommand& command,
    size_t ceiling) {
  if (command.set_assistant_configuration) {
    return AssistantConfigurationByteSize(*command.set_assistant_configuration,
                                          ceiling);
  }
  if (command.replace_saved_data_snapshot) {
    return SavedDataByteSize(*command.replace_saved_data_snapshot, ceiling);
  }
  return std::nullopt;
}

}  // namespace taffy
