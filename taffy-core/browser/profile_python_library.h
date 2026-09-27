// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PROFILE_PYTHON_LIBRARY_H_
#define TAFFY_BROWSER_PROFILE_PYTHON_LIBRARY_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/task/sequenced_task_runner.h"
#include "taffy/services/tool-runtime/supervisor/tool_job_resources.h"

namespace taffy {

// Opens the one installed `python-stdlib` asset ahead of use and exposes only
// duplicated descriptors plus facts measured from those exact bytes. Jobs do
// not choose a path, revision, digest or alternative library.
class ProfilePythonLibrary : public base::RefCounted<ProfilePythonLibrary> {
 public:
  explicit ProfilePythonLibrary(base::FilePath asset_store_root);
  ProfilePythonLibrary(const ProfilePythonLibrary&) = delete;
  ProfilePythonLibrary& operator=(const ProfilePythonLibrary&) = delete;

  tool_job_resources::PythonLibraryPort GetOpenPort();

  // Call after the delivery plane installs or removes an artifact. The cached
  // descriptor is withdrawn immediately, then replaced only after a blocking
  // scan has opened and hashed the current installed revision.
  void Refresh();

  bool ready_for_testing() const;

 private:
  friend class base::RefCounted<ProfilePythonLibrary>;

  struct Loaded {
    base::File file;
    std::string version;
    uint64_t byte_length = 0u;
    std::vector<uint8_t> digest;
  };

  ~ProfilePythonLibrary();

  static std::optional<Loaded> LoadBlocking(base::FilePath root);
  void OnLoaded(std::optional<Loaded> loaded);
  bool Open(tool_job_resources::PythonLibrarySource* resolved);

  const base::FilePath asset_store_root_;
  const scoped_refptr<base::SequencedTaskRunner> file_runner_;
  std::optional<Loaded> loaded_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<ProfilePythonLibrary> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_PROFILE_PYTHON_LIBRARY_H_
