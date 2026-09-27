// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/profile_python_library.h"

#include <array>
#include <utility>

#include "base/files/memory_mapped_file.h"
#include "base/functional/bind.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "crypto/hash.h"
#include "taffy/browser/assets/asset_store.h"

namespace taffy {
namespace {

constexpr char kLibraryId[] = "python-stdlib";

}  // namespace

ProfilePythonLibrary::ProfilePythonLibrary(base::FilePath asset_store_root)
    : asset_store_root_(std::move(asset_store_root)),
      file_runner_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN})) {
  Refresh();
}

ProfilePythonLibrary::~ProfilePythonLibrary() = default;

tool_job_resources::PythonLibraryPort ProfilePythonLibrary::GetOpenPort() {
  return base::BindRepeating(&ProfilePythonLibrary::Open,
                             base::RetainedRef(this));
}

void ProfilePythonLibrary::Refresh() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  loaded_.reset();
  file_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&ProfilePythonLibrary::LoadBlocking, asset_store_root_),
      base::BindOnce(&ProfilePythonLibrary::OnLoaded,
                     weak_factory_.GetWeakPtr()));
}

bool ProfilePythonLibrary::ready_for_testing() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return loaded_.has_value();
}

// static
std::optional<ProfilePythonLibrary::Loaded>
ProfilePythonLibrary::LoadBlocking(base::FilePath root) {
  const AssetStore store(std::move(root));
  const std::string revision = store.InstalledRevision(kLibraryId);
  if (revision.empty()) {
    return std::nullopt;
  }
  base::File file = store.OpenInstalled(kLibraryId, revision);
  if (!file.IsValid()) {
    return std::nullopt;
  }
  base::MemoryMappedFile mapped;
  if (!mapped.Initialize(file.Duplicate()) || mapped.length() == 0u) {
    return std::nullopt;
  }
  const std::array<uint8_t, 32> digest = crypto::hash::Sha256(mapped.bytes());
  Loaded loaded;
  loaded.file = std::move(file);
  loaded.version = revision;
  loaded.byte_length = mapped.length();
  loaded.digest.assign(digest.begin(), digest.end());
  return loaded;
}

void ProfilePythonLibrary::OnLoaded(std::optional<Loaded> loaded) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  loaded_ = std::move(loaded);
}

bool ProfilePythonLibrary::Open(
    tool_job_resources::PythonLibrarySource* resolved) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!resolved || !loaded_) {
    return false;
  }
  base::File duplicate = loaded_->file.Duplicate();
  if (!duplicate.IsValid()) {
    return false;
  }
  resolved->file = std::move(duplicate);
  resolved->library_id = kLibraryId;
  resolved->library_version = loaded_->version;
  resolved->byte_length = loaded_->byte_length;
  resolved->digest = loaded_->digest;
  return true;
}

}  // namespace taffy
