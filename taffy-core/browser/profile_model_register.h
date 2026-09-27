// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PROFILE_MODEL_REGISTER_H_
#define TAFFY_BROWSER_PROFILE_MODEL_REGISTER_H_

#include <stddef.h>
#include <stdint.h>

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/task/sequenced_task_runner.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/tool-runtime/supervisor/tool_job_resources.h"

namespace taffy {

// Browser custody for installed local-model rows the isolated core reconciled
// against the verified delivery catalog.
//
// A state update is a complete replacement snapshot. This class validates its
// closed kind/format/role vocabulary synchronously, then opens and hashes every
// exact profile revision on a MayBlock sequence. Only rows whose descriptor,
// length and SHA-256 all agree become resolvable. The retained descriptor pins
// the verified file identity across an atomic replacement; a job receives a
// duplicate read-only descriptor and catalog facts, never a path.
class ProfileModelRegister : public base::RefCounted<ProfileModelRegister> {
 private:
  struct CatalogArtifact {
    std::string asset_id;
    std::string asset_revision;
    core_service::mojom::AssetKind asset_kind =
        core_service::mojom::AssetKind::kModelWeights;
    tool_runtime::mojom::ToolModelArtifactKind format =
        tool_runtime::mojom::ToolModelArtifactKind::kLitertTflite;
    bool adapter = false;
    uint64_t byte_length = 0u;
    std::array<uint8_t, 32> digest{};

    bool operator==(const CatalogArtifact&) const = default;
  };

 public:
  // One structurally valid, canonical replacement prepared without changing
  // descriptor custody. Only this register can create one, so committing it
  // cannot fail partway through or decode the wire snapshot again.
  class PreparedSnapshot final {
   public:
    PreparedSnapshot(PreparedSnapshot&&);
    PreparedSnapshot& operator=(PreparedSnapshot&&);
    PreparedSnapshot(const PreparedSnapshot&) = delete;
    PreparedSnapshot& operator=(const PreparedSnapshot&) = delete;
    ~PreparedSnapshot();

   private:
    friend class ProfileModelRegister;

    explicit PreparedSnapshot(std::vector<CatalogArtifact> artifacts);

    std::vector<CatalogArtifact> artifacts_;
  };

  explicit ProfileModelRegister(base::FilePath asset_store_root);
  ProfileModelRegister(const ProfileModelRegister&) = delete;
  ProfileModelRegister& operator=(const ProfileModelRegister&) = delete;

  tool_job_resources::ModelArtifactPort GetArtifactPort();

  // Decodes, validates and canonicalizes a candidate without changing the
  // register. A malformed candidate returns nullopt and leaves current facts
  // and descriptors intact.
  std::optional<PreparedSnapshot> PrepareSnapshot(
      const std::vector<core_service::mojom::ModelArtifactRegistrationPtr>&
          artifacts);

  // Commits an already prepared complete snapshot without decoding it again.
  // An unchanged canonical snapshot is a no-op. A changed snapshot withdraws
  // the current set immediately; old facts never remain usable while newer
  // bytes are being checked.
  void CommitPreparedSnapshot(PreparedSnapshot prepared);

  // Withdraws every fact and descriptor, including an in-flight validation.
  void Withdraw();

  tool_job_resources::ModelArtifactResolution Resolve(
      const std::string& model_id,
      const std::string& model_revision,
      bool adapter,
      tool_job_resources::ModelArtifactSource* resolved);

  bool validation_pending_for_testing() const;
  size_t registered_count_for_testing() const;
  size_t decode_invocation_count_for_testing() const;

 private:
  struct VerifiedArtifact {
    CatalogArtifact catalog;
    base::File file;
  };

  friend class base::RefCounted<ProfileModelRegister>;
  ~ProfileModelRegister();

  static bool DecodeSnapshot(
      const std::vector<core_service::mojom::ModelArtifactRegistrationPtr>&
          artifacts,
      std::vector<CatalogArtifact>* decoded);
  static std::vector<VerifiedArtifact> ValidateBlocking(
      base::FilePath asset_store_root,
      std::vector<CatalogArtifact> artifacts);
  void OnValidated(uint64_t epoch, std::vector<VerifiedArtifact> verified);

  const base::FilePath asset_store_root_;
  const scoped_refptr<base::SequencedTaskRunner> file_runner_;
  // Canonical catalog facts are retained separately from verified descriptor
  // custody. Frequent state publications can therefore prove that nothing
  // changed without withdrawing descriptors or reading large files again.
  std::vector<CatalogArtifact> catalog_;
  std::vector<VerifiedArtifact> verified_;
  uint64_t validation_epoch_ = 0u;
  bool validation_pending_ = false;
  size_t decode_invocation_count_ = 0u;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<ProfileModelRegister> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_PROFILE_MODEL_REGISTER_H_
