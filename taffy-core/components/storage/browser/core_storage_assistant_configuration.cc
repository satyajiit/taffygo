// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/core_storage_assistant_configuration.h"

#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "sql/statement.h"
#include "sql/transaction.h"
#include "taffy/components/storage/browser/core_storage_task_seed.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr uint32_t kAbilityCount = 16u;
constexpr uint32_t kAllAbilityBits = (1u << kAbilityCount) - 1u;

std::optional<mojom::AssistantAbility> AbilityFromWire(uint32_t wire) {
  switch (wire) {
    case 0:
      return mojom::AssistantAbility::kPagesLookup;
    case 1:
      return mojom::AssistantAbility::kPagesCompare;
    case 2:
      return mojom::AssistantAbility::kPagesSummarize;
    case 3:
      return mojom::AssistantAbility::kPagesTable;
    case 4:
      return mojom::AssistantAbility::kProducts;
    case 5:
      return mojom::AssistantAbility::kOffers;
    case 6:
      return mojom::AssistantAbility::kForm;
    case 7:
      return mojom::AssistantAbility::kDownloads;
    case 8:
      return mojom::AssistantAbility::kPdf;
    case 9:
      return mojom::AssistantAbility::kSheet;
    case 10:
      return mojom::AssistantAbility::kDocument;
    case 11:
      return mojom::AssistantAbility::kDepth;
    case 12:
      return mojom::AssistantAbility::kTrip;
    case 13:
      return mojom::AssistantAbility::kPictures;
    case 14:
      return mojom::AssistantAbility::kVideo;
    case 15:
      return mojom::AssistantAbility::kKeep;
  }
  return std::nullopt;
}

std::optional<uint32_t> AbilityWire(mojom::AssistantAbility ability) {
  switch (ability) {
    case mojom::AssistantAbility::kPagesLookup:
      return 0u;
    case mojom::AssistantAbility::kPagesCompare:
      return 1u;
    case mojom::AssistantAbility::kPagesSummarize:
      return 2u;
    case mojom::AssistantAbility::kPagesTable:
      return 3u;
    case mojom::AssistantAbility::kProducts:
      return 4u;
    case mojom::AssistantAbility::kOffers:
      return 5u;
    case mojom::AssistantAbility::kForm:
      return 6u;
    case mojom::AssistantAbility::kDownloads:
      return 7u;
    case mojom::AssistantAbility::kPdf:
      return 8u;
    case mojom::AssistantAbility::kSheet:
      return 9u;
    case mojom::AssistantAbility::kDocument:
      return 10u;
    case mojom::AssistantAbility::kDepth:
      return 11u;
    case mojom::AssistantAbility::kTrip:
      return 12u;
    case mojom::AssistantAbility::kPictures:
      return 13u;
    case mojom::AssistantAbility::kVideo:
      return 14u;
    case mojom::AssistantAbility::kKeep:
      return 15u;
  }
  return std::nullopt;
}

std::optional<mojom::PersonalityPreset> PresetFromWire(int value) {
  switch (value) {
    case 0:
      return mojom::PersonalityPreset::kCarefulResearcher;
    case 1:
      return mojom::PersonalityPreset::kQuickShopper;
    case 2:
      return mojom::PersonalityPreset::kTripPlanner;
  }
  return std::nullopt;
}

bool ValidScale(uint32_t value) {
  return value <= mojom::kMaxPersonalityScale;
}

std::optional<uint32_t> DisabledMask(
    const std::vector<mojom::AssistantAbility>& disabled) {
  if (disabled.size() > mojom::kMaxAssistantAbilities) {
    return std::nullopt;
  }
  uint32_t mask = 0u;
  std::optional<uint32_t> previous;
  for (mojom::AssistantAbility ability : disabled) {
    const std::optional<uint32_t> wire = AbilityWire(ability);
    if (!wire || (previous && *wire <= *previous)) {
      return std::nullopt;
    }
    mask |= 1u << *wire;
    previous = wire;
  }
  return mask;
}

bool HasOnlyConfigurationBody(const mojom::StorageCommitEffect& body) {
  return body.assistant_configuration && !body.workspace &&
         !body.install_skill && !body.skill_status && !body.skill_run &&
         !body.forget_skill && !body.source_deletion && body.task_id.empty() &&
         !body.workspace_deletion && !body.library_entry &&
         !body.library_deletion && body.transaction_batch.empty() &&
         storage_internal::IsNeutralTaskIdSeed(body.task_id_seed);
}

}  // namespace

bool LoadAssistantConfigurationRecord(
    sql::Database* database,
    mojom::AssistantConfigurationPtr* output) {
  output->reset();
  sql::Statement query(database->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT revision,disabled_mask,personality_preset,pace,"
      "response_length,check_in,effect_id FROM core_assistant_configuration "
      "WHERE singleton=1"));
  if (!query.Step()) {
    return query.Succeeded();
  }
  const int64_t revision = query.ColumnInt64(0);
  const int64_t stored_mask = query.ColumnInt64(1);
  const std::optional<mojom::PersonalityPreset> preset =
      PresetFromWire(query.ColumnInt(2));
  const int64_t pace = query.ColumnInt64(3);
  const int64_t response_length = query.ColumnInt64(4);
  const int64_t check_in = query.ColumnInt64(5);
  const std::string effect_id = query.ColumnString(6);
  if (revision <= 0 || stored_mask < 0 ||
      stored_mask > static_cast<int64_t>(kAllAbilityBits) || !preset ||
      pace < 0 || response_length < 0 || check_in < 0 ||
      !ValidScale(static_cast<uint32_t>(pace)) ||
      !ValidScale(static_cast<uint32_t>(response_length)) ||
      !ValidScale(static_cast<uint32_t>(check_in)) || effect_id.empty() ||
      effect_id.size() > mojom::kMaxIdentifierBytes) {
    return false;
  }
  auto configuration = mojom::AssistantConfiguration::New();
  configuration->revision = static_cast<uint64_t>(revision);
  configuration->preset = *preset;
  configuration->pace = static_cast<uint32_t>(pace);
  configuration->length = static_cast<uint32_t>(response_length);
  configuration->check_in = static_cast<uint32_t>(check_in);
  const uint32_t mask = static_cast<uint32_t>(stored_mask);
  for (uint32_t wire = 0u; wire < kAbilityCount; ++wire) {
    if ((mask & (1u << wire)) == 0u) {
      continue;
    }
    const std::optional<mojom::AssistantAbility> ability =
        AbilityFromWire(wire);
    if (!ability) {
      return false;
    }
    configuration->disabled_abilities.push_back(*ability);
  }
  *output = std::move(configuration);
  return true;
}

bool LoadAssistantConfiguration(sql::Database* database,
                                mojom::CoreBootstrap* bootstrap) {
  return LoadAssistantConfigurationRecord(database,
                                          &bootstrap->assistant_configuration);
}

bool CommitAssistantConfiguration(sql::Database* database,
                                  const mojom::EffectEnvelope& effect) {
  const mojom::StorageCommitEffect& body = *effect.storage_commit;
  if (body.operation_kind !=
          mojom::StorageOperation::kSetAssistantConfiguration ||
      !HasOnlyConfigurationBody(body) || effect.effect_id.empty() ||
      effect.effect_id.size() > mojom::kMaxIdentifierBytes ||
      body.expected_revision >=
          static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
      body.resulting_revision != body.expected_revision + 1u) {
    return false;
  }
  const mojom::AssistantConfigurationPersistEffect& configuration =
      *body.assistant_configuration;
  const std::optional<mojom::PersonalityPreset> preset =
      PresetFromWire(static_cast<int>(configuration.preset));
  const std::optional<uint32_t> mask =
      DisabledMask(configuration.disabled_abilities);
  if (!preset || !mask || !ValidScale(configuration.pace) ||
      !ValidScale(configuration.length) ||
      !ValidScale(configuration.check_in)) {
    return false;
  }

  sql::Transaction transaction(database);
  if (!transaction.Begin()) {
    return false;
  }
  sql::Statement duplicate(database->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT revision,disabled_mask,personality_preset,pace,"
      "response_length,check_in FROM core_assistant_configuration "
      "WHERE effect_id=?"));
  duplicate.BindString(0, effect.effect_id);
  if (duplicate.Step()) {
    return duplicate.ColumnInt64(0) ==
               static_cast<int64_t>(body.resulting_revision) &&
           duplicate.ColumnInt64(1) == static_cast<int64_t>(*mask) &&
           duplicate.ColumnInt(2) == static_cast<int>(*preset) &&
           duplicate.ColumnInt64(3) ==
               static_cast<int64_t>(configuration.pace) &&
           duplicate.ColumnInt64(4) ==
               static_cast<int64_t>(configuration.length) &&
           duplicate.ColumnInt64(5) ==
               static_cast<int64_t>(configuration.check_in) &&
           transaction.Commit();
  }
  if (!duplicate.Succeeded()) {
    return false;
  }

  sql::Statement current(database->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT revision FROM core_assistant_configuration WHERE singleton=1"));
  const bool exists = current.Step();
  if ((!exists && !current.Succeeded()) ||
      (exists && current.ColumnInt64(0) !=
                     static_cast<int64_t>(body.expected_revision)) ||
      (!exists && body.expected_revision != 0u)) {
    return false;
  }

  // Two call sites, one statement each, and that is the whole point rather
  // than a style preference. A cached statement is keyed by SQL_FROM_HERE,
  // which is the file and the line and nothing else — the text is not in the
  // key. One line holding a runtime choice between an UPDATE and an INSERT
  // therefore files two statements under one key, and `sql::Database` refuses
  // the second with `GetCachedStatement used with same ID but different SQL`.
  // That refusal is a DCHECK, so on a phone built with `dcheck_always_on` it
  // aborted the browser process — the whole application, not a tab — on the
  // second assistant-configuration change of a session, which is the first
  // one that takes the other branch. Splitting the branches restores the
  // promise the macro is making: a line means one statement, for the life of
  // the database. The arity differed too (eight placeholders against seven),
  // which a conditional bind was papering over.
  if (exists) {
    sql::Statement update(database->GetCachedStatement(
        SQL_FROM_HERE,
        "UPDATE core_assistant_configuration SET revision=?,"
        "disabled_mask=?,personality_preset=?,pace=?,response_length=?,"
        "check_in=?,effect_id=? WHERE singleton=1 AND revision=?"));
    update.BindInt64(0, static_cast<int64_t>(body.resulting_revision));
    update.BindInt64(1, static_cast<int64_t>(*mask));
    update.BindInt(2, static_cast<int>(*preset));
    update.BindInt64(3, static_cast<int64_t>(configuration.pace));
    update.BindInt64(4, static_cast<int64_t>(configuration.length));
    update.BindInt64(5, static_cast<int64_t>(configuration.check_in));
    update.BindString(6, effect.effect_id);
    update.BindInt64(7, static_cast<int64_t>(body.expected_revision));
    return update.Run() && database->GetLastChangeCount() == 1 &&
           transaction.Commit();
  }
  sql::Statement insert(database->GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT INTO core_assistant_configuration(singleton,revision,"
      "disabled_mask,personality_preset,pace,response_length,check_in,"
      "effect_id) VALUES(1,?,?,?,?,?,?,?)"));
  insert.BindInt64(0, static_cast<int64_t>(body.resulting_revision));
  insert.BindInt64(1, static_cast<int64_t>(*mask));
  insert.BindInt(2, static_cast<int>(*preset));
  insert.BindInt64(3, static_cast<int64_t>(configuration.pace));
  insert.BindInt64(4, static_cast<int64_t>(configuration.length));
  insert.BindInt64(5, static_cast<int64_t>(configuration.check_in));
  insert.BindString(6, effect.effect_id);
  return insert.Run() && database->GetLastChangeCount() == 1 &&
         transaction.Commit();
}

}  // namespace taffy
