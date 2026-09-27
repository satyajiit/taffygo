// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PROFILE_STORE_PROFILE_STORE_READER_H_
#define TAFFY_BROWSER_PROFILE_STORE_PROFILE_STORE_READER_H_

#include <stddef.h>

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "taffy/browser/core_task_store_rows.h"

class Profile;
class KeyedServiceBaseFactory;

namespace taffy {

// The sole adapter from Chromium's history service and bookmark model to the
// entries a store read may answer (decision 0133). It hands back a title, an
// address and a time per entry and nothing else about a visit or a bookmark;
// the caller reduces each to the wire row and applies the cap. Taffy's own
// working trail is left out here, where the visit source is known.
class ProfileStoreReader {
 public:
  using EntriesCallback =
      base::OnceCallback<void(std::vector<TaskStoreEntry>)>;

  virtual ~ProfileStoreReader() = default;

  ProfileStoreReader(const ProfileStoreReader&) = delete;
  ProfileStoreReader& operator=(const ProfileStoreReader&) = delete;

  // Null for private profiles and for a binary with no Chrome profile
  // composition, in which case every store read answers unavailable.
  static std::unique_ptr<ProfileStoreReader> Create(Profile* profile);

  // The Chromium stores whose lifetime must precede this reader's owner.
  static std::vector<KeyedServiceBaseFactory*> GetFactoryDependencies();

  // Visits whose title or address carry `words`, newest first, or the newest
  // visits when `words` is empty; at most `max_entries`.
  virtual void ReadHistory(const std::u16string& words,
                           size_t max_entries,
                           EntriesCallback callback) = 0;

  // Bookmarks whose title or address carry `words`, or the whole tree
  // flattened when `words` is empty, newest first; at most `max_entries`.
  virtual void ReadBookmarks(const std::u16string& words,
                             size_t max_entries,
                             EntriesCallback callback) = 0;

 protected:
  ProfileStoreReader() = default;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_PROFILE_STORE_PROFILE_STORE_READER_H_
