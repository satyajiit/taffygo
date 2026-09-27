// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/profile_tool_handle_store.h"

#include <utility>

#include "base/functional/bind.h"

namespace taffy {

ProfileToolHandleStore::Entry::Entry() = default;
ProfileToolHandleStore::Entry::Entry(ToolHandleBroker::Mode mode,
                                     base::File file)
    : mode(mode), file(std::move(file)) {}
ProfileToolHandleStore::Entry::Entry(Entry&&) = default;
ProfileToolHandleStore::Entry& ProfileToolHandleStore::Entry::operator=(
    Entry&&) = default;
ProfileToolHandleStore::Entry::~Entry() = default;

ProfileToolHandleStore::ProfileToolHandleStore() = default;

ProfileToolHandleStore::~ProfileToolHandleStore() = default;

ToolHandleBroker::ResolvePort ProfileToolHandleStore::GetResolvePort() {
  // base::RetainedRef rather than a weak pointer: the port returns a value, so
  // it cannot be bound weakly, and a store the supervisor can still call after
  // it was destroyed is the one failure mode this must not have.
  return base::BindRepeating(&ProfileToolHandleStore::Resolve,
                             base::RetainedRef(this));
}

bool ProfileToolHandleStore::Admit(const std::string& handle_id,
                                   ToolHandleBroker::Mode mode,
                                   base::File resource) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // A refused admission closes the descriptor with `resource` rather than
  // returning it, because an admission that half-succeeded would leave the
  // caller holding a resource it believes the browser now owns.
  if (handle_id.empty() || !resource.IsValid() ||
      entries_.contains(handle_id) ||
      entries_.size() >= kMaxOutstandingHandles) {
    return false;
  }
  entries_.insert_or_assign(handle_id, Entry(mode, std::move(resource)));
  return true;
}

void ProfileToolHandleStore::Forget(const std::string& handle_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  entries_.erase(handle_id);
}

void ProfileToolHandleStore::ForgetAll() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  entries_.clear();
}

size_t ProfileToolHandleStore::size_for_testing() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return entries_.size();
}

base::File ProfileToolHandleStore::Resolve(const std::string& handle_id,
                                           ToolHandleBroker::Mode mode) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto found = entries_.find(handle_id);
  if (found == entries_.end()) {
    // An identifier nothing admitted names no resource. That covers one this
    // store never held, one it already answered for, and one the broker minted
    // under a generation this profile has left — the broker refuses the last
    // before the port is reached, and this refuses it again if it ever is.
    return base::File();
  }
  Entry entry = std::move(found->second);
  // Consumed either way. A mismatch between the mode the broker recorded and
  // the mode this store recorded is the two halves disagreeing about the same
  // identifier, and the answer to that is to stop trusting it rather than to
  // pick one of the two.
  entries_.erase(found);
  if (entry.mode != mode) {
    return base::File();
  }
  return std::move(entry.file);
}

}  // namespace taffy
