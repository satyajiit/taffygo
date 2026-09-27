// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PROFILE_TOOL_HANDLE_STORE_H_
#define TAFFY_BROWSER_PROFILE_TOOL_HANDLE_STORE_H_

#include <stddef.h>

#include <string>

#include "base/containers/flat_map.h"
#include "base/files/file.h"
#include "base/memory/ref_counted.h"
#include "base/sequence_checker.h"
#include "taffy/services/tool-runtime/supervisor/tool_handle_broker.h"

namespace taffy {

// The browser half of `ToolHandleBroker::ResolvePort`: what an identifier the
// broker minted actually stands for.
//
// The two halves are deliberately separate and neither is sufficient alone.
// `ToolHandleBroker` is the authority on *whether* an identifier may be used —
// it minted it, it bound it to one job and one mode, and it revokes it when
// that job ends or the profile's generation changes. This store is the
// authority on *what* it names, and it is where the resource lives. A job that
// clears the broker still reaches nothing here unless the same identifier was
// admitted with a resource, and a resource admitted here is unreachable unless
// the broker minted its identifier for the job asking. Decision
// `docs/decisions/0041-tool-runtime-carries-opened-resources.md` section 7 is
// the record.
//
// This store never opens a file. It holds one that was already opened at the
// moment the resource entered the profile — a picked document, a download, a
// capture — because that moment is asynchronous and on a sequence where
// blocking is allowed, and this one is neither. What a worker receives is the
// descriptor, never the path, and never anything this store was not handed.
//
// `ProfileToolArtifactBroker` is the sole product owner that admits resources.
// It accepts only an exact completed download already attributed to the same
// task, opens that input away from the UI sequence, and creates bounded private
// output files for transforms. It mints the paired handle immediately before
// admission; every refusal and terminal path forgets both halves.
//
// One ask per identifier. Resolution consumes the entry whatever it answers,
// so a resource cannot be handed out twice, cannot be handed out after the
// mode it was admitted for was contradicted, and cannot outlive the one job
// its identifier was minted for.
class ProfileToolHandleStore
    : public base::RefCounted<ProfileToolHandleStore> {
 public:
  ProfileToolHandleStore();
  ProfileToolHandleStore(const ProfileToolHandleStore&) = delete;
  ProfileToolHandleStore& operator=(const ProfileToolHandleStore&) = delete;

  // The port `ProfileToolSupervisor::SetResourcePorts` installs. It holds a
  // reference to this store, so the store outlives the supervisor that was
  // handed it whatever else is destroyed first.
  ToolHandleBroker::ResolvePort GetResolvePort();

  // Records that `handle_id` — an identifier `ToolHandleBroker::Mint` just
  // returned — stands for `resource` in `mode`. False means the admission was
  // refused and `resource` was closed rather than stored: an empty identifier,
  // a descriptor that is not open, an identifier this store already holds, or
  // one more outstanding resource than a profile may hold at once.
  bool Admit(const std::string& handle_id,
             ToolHandleBroker::Mode mode,
             base::File resource);

  // Drops what an identifier stands for without resolving it. The caller that
  // admitted a resource for a job that then never started owns this; the
  // broker's own revocation cannot reach it, because the broker holds no
  // descriptor.
  void Forget(const std::string& handle_id);
  void ForgetAll();

  size_t size_for_testing() const;

 private:
  friend class base::RefCounted<ProfileToolHandleStore>;

  struct Entry {
    Entry();
    Entry(ToolHandleBroker::Mode mode, base::File file);
    Entry(Entry&&);
    Entry& operator=(Entry&&);
    ~Entry();

    ToolHandleBroker::Mode mode = ToolHandleBroker::Mode::kRead;
    base::File file;
  };

  // A profile holds no more outstanding resources than this at once. It is a
  // ceiling on descriptors the browser is keeping open on a caller's behalf,
  // not a concurrency limit: the supervisor already bounds jobs, and a handle
  // that no job ever claims is exactly the leak this bound catches.
  static constexpr size_t kMaxOutstandingHandles = 32u;

  ~ProfileToolHandleStore();

  base::File Resolve(const std::string& handle_id,
                     ToolHandleBroker::Mode mode);

  base::flat_map<std::string, Entry> entries_;

  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_PROFILE_TOOL_HANDLE_STORE_H_
