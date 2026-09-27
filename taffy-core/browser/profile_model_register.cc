// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/profile_model_register.h"

#include <algorithm>
#include <set>
#include <utility>

#include "base/check.h"
#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "crypto/hash.h"
#include "taffy/browser/assets/asset_store.h"

namespace taffy {
namespace {

namespace service_mojom = core_service::mojom;
namespace runtime_mojom = tool_runtime::mojom;

constexpr size_t kSha256Bytes = 32u;

bool DecodeFormat(service_mojom::ToolModelArtifactKind format,
                  runtime_mojom::ToolModelArtifactKind* decoded) {
  switch (format) {
    case service_mojom::ToolModelArtifactKind::kLitertTflite:
      *decoded = runtime_mojom::ToolModelArtifactKind::kLitertTflite;
      return true;
    case service_mojom::ToolModelArtifactKind::kOnnxRuntime:
      *decoded = runtime_mojom::ToolModelArtifactKind::kOnnxRuntime;
      return true;
    case service_mojom::ToolModelArtifactKind::kGguf:
      *decoded = runtime_mojom::ToolModelArtifactKind::kGguf;
      return true;
  }
  return false;
}

bool IsModelKind(service_mojom::AssetKind kind, bool adapter) {
  switch (kind) {
    case service_mojom::AssetKind::kModelWeights:
      return true;
    case service_mojom::AssetKind::kModelTokenizer:
      return !adapter;
    case service_mojom::AssetKind::kPythonStdlib:
    case service_mojom::AssetKind::kPythonPackages:
    case service_mojom::AssetKind::kFilterList:
    case service_mojom::AssetKind::kCountryFlags:
    case service_mojom::AssetKind::kStartScenes:
      return false;
  }
  return false;
}

}  // namespace

ProfileModelRegister::ProfileModelRegister(base::FilePath asset_store_root)
    : asset_store_root_(std::move(asset_store_root)),
      file_runner_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
           base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN})) {}

ProfileModelRegister::~ProfileModelRegister() = default;

ProfileModelRegister::PreparedSnapshot::PreparedSnapshot(
    std::vector<CatalogArtifact> artifacts)
    : artifacts_(std::move(artifacts)) {}

ProfileModelRegister::PreparedSnapshot::PreparedSnapshot(PreparedSnapshot&&) =
    default;

ProfileModelRegister::PreparedSnapshot&
ProfileModelRegister::PreparedSnapshot::operator=(PreparedSnapshot&&) = default;

ProfileModelRegister::PreparedSnapshot::~PreparedSnapshot() = default;

tool_job_resources::ModelArtifactPort ProfileModelRegister::GetArtifactPort() {
  return base::BindRepeating(&ProfileModelRegister::Resolve,
                             base::RetainedRef(this));
}

std::optional<ProfileModelRegister::PreparedSnapshot>
ProfileModelRegister::PrepareSnapshot(
    const std::vector<service_mojom::ModelArtifactRegistrationPtr>& artifacts) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ++decode_invocation_count_;
  std::vector<CatalogArtifact> decoded;
  if (!DecodeSnapshot(artifacts, &decoded)) {
    return std::nullopt;
  }
  std::ranges::sort(
      decoded, [](const CatalogArtifact& left, const CatalogArtifact& right) {
        if (left.asset_id != right.asset_id) {
          return left.asset_id < right.asset_id;
        }
        return left.asset_revision < right.asset_revision;
      });
  return PreparedSnapshot(std::move(decoded));
}

void ProfileModelRegister::CommitPreparedSnapshot(PreparedSnapshot prepared) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (prepared.artifacts_ == catalog_) {
    return;
  }
  catalog_ = prepared.artifacts_;
  ++validation_epoch_;
  verified_.clear();
  validation_pending_ = !prepared.artifacts_.empty();
  if (prepared.artifacts_.empty()) {
    return;
  }
  const uint64_t epoch = validation_epoch_;
  file_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&ProfileModelRegister::ValidateBlocking, asset_store_root_,
                     std::move(prepared.artifacts_)),
      base::BindOnce(&ProfileModelRegister::OnValidated,
                     weak_factory_.GetWeakPtr(), epoch));
}

void ProfileModelRegister::Withdraw() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ++validation_epoch_;
  validation_pending_ = false;
  catalog_.clear();
  verified_.clear();
}

tool_job_resources::ModelArtifactResolution ProfileModelRegister::Resolve(
    const std::string& model_id,
    const std::string& model_revision,
    bool adapter,
    tool_job_resources::ModelArtifactSource* resolved) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!resolved) {
    return tool_job_resources::ModelArtifactResolution::kMissing;
  }
  const auto found =
      std::ranges::find_if(verified_, [&](const VerifiedArtifact& artifact) {
        return artifact.catalog.asset_kind ==
                   service_mojom::AssetKind::kModelWeights &&
               artifact.catalog.asset_id == model_id &&
               artifact.catalog.asset_revision == model_revision &&
               artifact.catalog.adapter == adapter;
      });
  if (found == verified_.end()) {
    return tool_job_resources::ModelArtifactResolution::kMissing;
  }
  base::File duplicate = found->file.Duplicate();
  const int64_t length = duplicate.IsValid() ? duplicate.GetLength() : -1;
  if (length < 0 ||
      static_cast<uint64_t>(length) != found->catalog.byte_length ||
      duplicate.Seek(base::File::FROM_BEGIN, 0) != 0) {
    return tool_job_resources::ModelArtifactResolution::kMissing;
  }
  resolved->file = std::move(duplicate);
  resolved->byte_length = found->catalog.byte_length;
  resolved->digest.assign(found->catalog.digest.begin(),
                          found->catalog.digest.end());
  resolved->kind = found->catalog.format;
  return tool_job_resources::ModelArtifactResolution::kResolved;
}

bool ProfileModelRegister::validation_pending_for_testing() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return validation_pending_;
}

size_t ProfileModelRegister::registered_count_for_testing() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return verified_.size();
}

size_t ProfileModelRegister::decode_invocation_count_for_testing() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return decode_invocation_count_;
}

// static
bool ProfileModelRegister::DecodeSnapshot(
    const std::vector<service_mojom::ModelArtifactRegistrationPtr>& artifacts,
    std::vector<CatalogArtifact>* decoded) {
  if (artifacts.size() > service_mojom::kMaxRegisteredModelArtifacts) {
    return false;
  }
  std::set<std::pair<std::string, std::string>> identities;
  std::vector<CatalogArtifact> result;
  result.reserve(artifacts.size());
  for (const auto& artifact : artifacts) {
    runtime_mojom::ToolModelArtifactKind format;
    if (!artifact || !AssetStore::IsSafeComponent(artifact->asset_id) ||
        !AssetStore::IsSafeComponent(artifact->asset_revision) ||
        !IsModelKind(artifact->asset_kind, artifact->adapter) ||
        !DecodeFormat(artifact->format, &format) ||
        artifact->byte_length == 0u ||
        artifact->byte_length > service_mojom::kMaxToolModelArtifactBytes ||
        artifact->digest.size() != kSha256Bytes ||
        !identities.emplace(artifact->asset_id, artifact->asset_revision)
             .second) {
      return false;
    }
    CatalogArtifact row;
    row.asset_id = artifact->asset_id;
    row.asset_revision = artifact->asset_revision;
    row.asset_kind = artifact->asset_kind;
    row.format = format;
    row.adapter = artifact->adapter;
    row.byte_length = artifact->byte_length;
    std::ranges::copy(artifact->digest, row.digest.begin());
    result.push_back(std::move(row));
  }
  if (decoded) {
    *decoded = std::move(result);
  }
  return true;
}

// static
std::vector<ProfileModelRegister::VerifiedArtifact>
ProfileModelRegister::ValidateBlocking(base::FilePath asset_store_root,
                                       std::vector<CatalogArtifact> artifacts) {
  const AssetStore store(std::move(asset_store_root));
  std::vector<VerifiedArtifact> verified;
  verified.reserve(artifacts.size());
  for (CatalogArtifact& artifact : artifacts) {
    base::File file =
        store.OpenInstalled(artifact.asset_id, artifact.asset_revision);
    const int64_t length = file.IsValid() ? file.GetLength() : -1;
    if (length < 0 || static_cast<uint64_t>(length) != artifact.byte_length) {
      continue;
    }
    std::array<uint8_t, kSha256Bytes> digest{};
    if (!crypto::hash::HashFile(crypto::hash::kSha256, &file, digest) ||
        digest != artifact.digest ||
        file.Seek(base::File::FROM_BEGIN, 0) != 0) {
      continue;
    }
    verified.push_back(VerifiedArtifact{.catalog = std::move(artifact),
                                        .file = std::move(file)});
  }
  return verified;
}

void ProfileModelRegister::OnValidated(uint64_t epoch,
                                       std::vector<VerifiedArtifact> verified) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (epoch != validation_epoch_) {
    return;
  }
  verified_ = std::move(verified);
  validation_pending_ = false;
}

}  // namespace taffy
