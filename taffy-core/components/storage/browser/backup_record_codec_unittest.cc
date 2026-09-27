// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/backup_record_codec.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::storage::backup {
namespace {

namespace mojom = core_service::mojom;
constexpr char kId[] = "11111111111111111111111111111111";
constexpr char kWorkspaceId[] = "33333333333333333333333333333333";
constexpr uint64_t kConfigurationRevision = 0x0102030405060708;

// Independently laid out v1 fixed-LE vectors. Changing a field or its order
// requires a new payload schema, not regeneration of the existing vectors.
constexpr char kConfigurationHex[] =
    "5441464659524543010000000000000017000000617373697374616e742d636f6e666967"
    "75726174696f6e08070605040302010300000000000000050000000f0000000200000001"
    "0000000200000000000000";
constexpr char kLibraryHex[] =
    "544146465952454301000000020000002000000031313131313131313131313131313131"
    "313131313131313131313131313131310100000000000000200000003232323232323232"
    "323232323232323232323232323232323232323232323232080000005265736561726368"
    "200000003333333333333333333333333333333333333333333333333333333333333333"
    "070000000000000020000000343434343434343434343434343434343434343434343434"
    "34343434343434340800000077617272616e74790900000074776f207965617273000000"
    "000001000000200000003535353535353535353535353535353535353535353535353535"
    "3535353535350e00000053706563696669636174696f6e730d0000006d616b65722e6578"
    "616d706c65e803000000000000d007000000000000e80300000000000000";
constexpr char kMemoryHex[] =
    "544146465952454301000000030000002000000031313131313131313131313131313131"
    "313131313131313131313131313131310100000000000000160000005072656665722063"
    "6f6e6369736520616e7377657273000000000000000000000000000000e8030000000000"
    "00d00700000000000000000000000000000000000000000000";

std::vector<uint8_t> Bytes(std::string_view hex) {
  std::vector<uint8_t> bytes;
  EXPECT_TRUE(base::HexStringToBytes(hex, &bytes));
  return bytes;
}

mojom::LibraryEntryRecordPtr Library() {
  std::vector<mojom::LibrarySourceRecordPtr> sources;
  sources.push_back(
      mojom::LibrarySourceRecord::New("55555555555555555555555555555555",
                                      "Specifications", "maker.example", 1000));
  return mojom::LibraryEntryRecord::New(
      kId, 1, "22222222222222222222222222222222", "Research", kWorkspaceId, 7,
      "44444444444444444444444444444444", "warranty", "two years", std::nullopt,
      mojom::LibraryFactKind::kFromPage, std::move(sources), 2000, 1000, false);
}

mojom::MemoryRecordPtr Memory() {
  auto record = mojom::MemoryRecord::New();
  record->memory_id = kId;
  record->revision = 1;
  record->statement = "Prefer concise answers";
  record->source_kind = mojom::MemorySourceKind::kUserEntered;
  record->scope_kind = mojom::MemoryScopeKind::kAllTasks;
  record->sensitivity = mojom::MemorySensitivity::kStandard;
  record->created_at_epoch_ms = 1000;
  record->updated_at_epoch_ms = 2000;
  return record;
}

mojom::AssistantConfigurationPtr Configuration() {
  return mojom::AssistantConfiguration::New(
      kConfigurationRevision,
      std::vector{mojom::AssistantAbility::kPagesLookup,
                  mojom::AssistantAbility::kOffers,
                  mojom::AssistantAbility::kKeep},
      mojom::PersonalityPreset::kTripPlanner, 1, 2, 0);
}

TEST(BackupRecordCodecTest, ThreeKindsMatchFrozenVectorsAndRoundTripExactly) {
  auto library = EncodeLibraryRecordV1(*Library());
  auto memory = EncodeMemoryRecordV1(*Memory());
  auto configuration = EncodeAssistantConfigurationV1(*Configuration());
  ASSERT_TRUE(library.has_value());
  ASSERT_TRUE(memory.has_value());
  ASSERT_TRUE(configuration.has_value());
  EXPECT_EQ(*library, Bytes(kLibraryHex));
  EXPECT_EQ(*memory, Bytes(kMemoryHex));
  EXPECT_EQ(*configuration, Bytes(kConfigurationHex));
  auto decoded_library = DecodeLibraryRecordV1(*library, kId, 1);
  auto decoded_memory = DecodeMemoryRecordV1(*memory, kId, 1);
  auto decoded_configuration =
      DecodeAssistantConfigurationV1(*configuration, kConfigurationRevision);
  ASSERT_TRUE(decoded_library.has_value());
  ASSERT_TRUE(decoded_memory.has_value());
  ASSERT_TRUE(decoded_configuration.has_value());
  EXPECT_TRUE((*decoded_library)->Equals(*Library()));
  EXPECT_TRUE((*decoded_memory)->Equals(*Memory()));
  EXPECT_TRUE((*decoded_configuration)->Equals(*Configuration()));
}

TEST(BackupRecordCodecTest, OptionalValuesRemainExactAndDoNotCreateAuthority) {
  auto library = Library();
  library->correction = "three years";
  library->has_conflict = true;
  library->kind = mojom::LibraryFactKind::kTaffyInference;
  auto memory = Memory();
  memory->statement = "Keep the person\xe2\x80\x99s phrasing";
  memory->source_kind = mojom::MemorySourceKind::kAcceptedTaskSuggestion;
  memory->source_task_id = "completed-task-reference";
  memory->source_workspace =
      mojom::MemoryWorkspaceRecord::New(kWorkspaceId, "Research");
  memory->scope_kind = mojom::MemoryScopeKind::kWorkspace;
  memory->scope_workspace = memory->source_workspace.Clone();
  memory->sensitivity = mojom::MemorySensitivity::kSensitive;
  memory->reviewed_at_epoch_ms = 1500;
  memory->expires_at_epoch_ms = 3000;
  auto library_bytes = EncodeLibraryRecordV1(*library);
  auto memory_bytes = EncodeMemoryRecordV1(*memory);
  ASSERT_TRUE(library_bytes.has_value());
  ASSERT_TRUE(memory_bytes.has_value());
  auto opened_library = DecodeLibraryRecordV1(*library_bytes, kId, 1);
  auto opened_memory = DecodeMemoryRecordV1(*memory_bytes, kId, 1);
  ASSERT_TRUE(opened_library.has_value());
  ASSERT_TRUE(opened_memory.has_value());
  EXPECT_TRUE((*opened_library)->Equals(*library));
  EXPECT_TRUE((*opened_memory)->Equals(*memory));
}

TEST(BackupRecordCodecTest, EveryTruncatedPrefixAndTrailingByteIsRefused) {
  auto library = Bytes(kLibraryHex);
  auto memory = Bytes(kMemoryHex);
  auto configuration = Bytes(kConfigurationHex);
  for (size_t size = 0; size < library.size(); ++size) {
    EXPECT_FALSE(DecodeLibraryRecordV1(base::span(library).first(size), kId, 1)
                     .has_value());
  }
  for (size_t size = 0; size < memory.size(); ++size) {
    EXPECT_FALSE(DecodeMemoryRecordV1(base::span(memory).first(size), kId, 1)
                     .has_value());
  }
  for (size_t size = 0; size < configuration.size(); ++size) {
    EXPECT_FALSE(
        DecodeAssistantConfigurationV1(base::span(configuration).first(size),
                                       kConfigurationRevision)
            .has_value());
  }
  library.push_back(0);
  memory.push_back(0);
  configuration.push_back(0);
  EXPECT_FALSE(DecodeLibraryRecordV1(library, kId, 1).has_value());
  EXPECT_FALSE(DecodeMemoryRecordV1(memory, kId, 1).has_value());
  EXPECT_FALSE(
      DecodeAssistantConfigurationV1(configuration, kConfigurationRevision)
          .has_value());
}

TEST(BackupRecordCodecTest, PayloadIsBoundToManifestIdentityRevisionAndKind) {
  const auto library = Bytes(kLibraryHex);
  const auto memory = Bytes(kMemoryHex);
  const auto configuration = Bytes(kConfigurationHex);
  EXPECT_EQ(DecodeLibraryRecordV1(library, kWorkspaceId, 1).error(),
            BackupRecordCodecError::kDescriptorMismatch);
  EXPECT_EQ(DecodeLibraryRecordV1(library, kId, 2).error(),
            BackupRecordCodecError::kDescriptorMismatch);
  EXPECT_EQ(DecodeMemoryRecordV1(memory, kWorkspaceId, 1).error(),
            BackupRecordCodecError::kDescriptorMismatch);
  EXPECT_EQ(DecodeMemoryRecordV1(memory, kId, 2).error(),
            BackupRecordCodecError::kDescriptorMismatch);
  EXPECT_EQ(DecodeAssistantConfigurationV1(configuration, 2).error(),
            BackupRecordCodecError::kDescriptorMismatch);
  EXPECT_FALSE(DecodeLibraryRecordV1(memory, kId, 1).has_value());
  EXPECT_FALSE(DecodeMemoryRecordV1(library, kId, 1).has_value());
  EXPECT_FALSE(DecodeAssistantConfigurationV1(memory, 1).has_value());
}

TEST(BackupRecordCodecTest, WrongMagicVersionKindAndOversizedLengthFailClosed) {
  for (size_t offset : {0u, 8u, 12u, 16u}) {
    auto library = Bytes(kLibraryHex);
    auto memory = Bytes(kMemoryHex);
    auto configuration = Bytes(kConfigurationHex);
    library[offset] = 0xff;
    memory[offset] = 0xff;
    configuration[offset] = 0xff;
    EXPECT_FALSE(DecodeLibraryRecordV1(library, kId, 1).has_value());
    EXPECT_FALSE(DecodeMemoryRecordV1(memory, kId, 1).has_value());
    EXPECT_FALSE(
        DecodeAssistantConfigurationV1(configuration, kConfigurationRevision)
            .has_value());
  }
}

TEST(BackupRecordCodecTest,
     NoncanonicalBooleansInvalidUtf8AndUnknownEnumsRefused) {
  auto library = Bytes(kLibraryHex);
  library.back() = 2;
  EXPECT_FALSE(DecodeLibraryRecordV1(library, kId, 1).has_value());
  for (size_t offset : {64u, 86u, 90u}) {
    auto memory = Bytes(kMemoryHex);
    // Statement UTF-8, source-kind enum and optional-source presence byte.
    memory[offset] = 0xff;
    EXPECT_FALSE(DecodeMemoryRecordV1(memory, kId, 1).has_value());
  }
  auto configuration = Bytes(kConfigurationHex);
  configuration[55] = 16;
  EXPECT_FALSE(
      DecodeAssistantConfigurationV1(configuration, kConfigurationRevision)
          .has_value());
}

TEST(BackupRecordCodecTest, LibraryInvalidRecordsNeverProducePayloads) {
  auto record = Library();
  record->sources.push_back(record->sources.front().Clone());
  EXPECT_FALSE(EncodeLibraryRecordV1(*record).has_value());
  record = Library();
  record->sources.front()->host = "https://user:secret@example.test/path";
  EXPECT_FALSE(EncodeLibraryRecordV1(*record).has_value());
  record = Library();
  record->kind = static_cast<mojom::LibraryFactKind>(99);
  EXPECT_FALSE(EncodeLibraryRecordV1(*record).has_value());
  record = Library();
  record->revision = 0;
  EXPECT_FALSE(EncodeLibraryRecordV1(*record).has_value());
}

TEST(BackupRecordCodecTest, MemoryInvalidProvenanceScopeAndBoundsNeverEncode) {
  auto record = Memory();
  record->scope_kind = mojom::MemoryScopeKind::kWorkspace;
  EXPECT_FALSE(EncodeMemoryRecordV1(*record).has_value());
  record = Memory();
  record->source_task_id = "unreviewed-reference";
  EXPECT_FALSE(EncodeMemoryRecordV1(*record).has_value());
  record = Memory();
  record->statement.assign(mojom::kMaxMemoryStatementBytes + 1, 'x');
  EXPECT_FALSE(EncodeMemoryRecordV1(*record).has_value());
  record = Memory();
  record->revision = std::numeric_limits<uint64_t>::max();
  EXPECT_FALSE(EncodeMemoryRecordV1(*record).has_value());
}

TEST(BackupRecordCodecTest, ConfigurationIsClosedSortedAndBounded) {
  auto record = Configuration();
  record->disabled_abilities.push_back(mojom::AssistantAbility::kPagesLookup);
  EXPECT_FALSE(EncodeAssistantConfigurationV1(*record).has_value());
  record = Configuration();
  record->disabled_abilities.push_back(
      static_cast<mojom::AssistantAbility>(16));
  EXPECT_FALSE(EncodeAssistantConfigurationV1(*record).has_value());
  record = Configuration();
  record->preset = static_cast<mojom::PersonalityPreset>(3);
  EXPECT_FALSE(EncodeAssistantConfigurationV1(*record).has_value());
  record = Configuration();
  record->pace = 3;
  EXPECT_FALSE(EncodeAssistantConfigurationV1(*record).has_value());
  record = Configuration();
  record->revision = 0;
  EXPECT_FALSE(EncodeAssistantConfigurationV1(*record).has_value());
}

}  // namespace
}  // namespace taffy::storage::backup
