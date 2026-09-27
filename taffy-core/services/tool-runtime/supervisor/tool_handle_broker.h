// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_TOOL_RUNTIME_SUPERVISOR_TOOL_HANDLE_BROKER_H_
#define TAFFY_SERVICES_TOOL_RUNTIME_SUPERVISOR_TOOL_HANDLE_BROKER_H_

#include <stddef.h>

#include <string>

#include "base/containers/flat_map.h"
#include "base/files/file.h"
#include "base/functional/callback.h"

namespace taffy {

// The broker behind the opaque resource identifiers the Tool Runtime contract
// has always called "broker-owned" and which, until this change, nothing
// minted. An identifier arrived inside a job, was checked only for being a
// non-empty string of bounded length, and was passed on to a worker that had
// no way to use it and no way to be told it was wrong.
//
// The property this class makes true is that an identifier carries no
// authority. It is minted here, bound to one job and one mode, and it is
// afterwards only ever *compared* - never dereferenced by anything a worker
// can reach. What a worker receives is an already-open descriptor or nothing
// at all, so an identifier it invents names no file, and an identifier it
// echoes back is a label the broker checks rather than a request the broker
// obeys.
//
// Identifiers are 128 bits from the platform generator, so one is not
// guessable and not derivable from another; and even a guessed identifier is
// refused unless it was minted for that exact job in that exact mode.
class ToolHandleBroker {
 public:
  enum class Mode { kRead, kWrite };

  // Opens a resource this broker minted an identifier for. Injected at the
  // product composition edge, because opening a file is profile ownership and
  // this directory deliberately depends on neither //chrome nor //content.
  // Absent until the media milestone installs it, and absent means a job that
  // names a resource is refused rather than started without one.
  using ResolvePort =
      base::RepeatingCallback<base::File(const std::string& handle_id,
                                         Mode mode)>;

  ToolHandleBroker();
  ToolHandleBroker(const ToolHandleBroker&) = delete;
  ToolHandleBroker& operator=(const ToolHandleBroker&) = delete;
  ~ToolHandleBroker();

  void SetResolvePort(ResolvePort resolve);
  bool can_resolve() const { return !resolve_.is_null(); }

  // Records that `job_id` may reach one resource in `mode`, and returns the
  // identifier that stands for it. The caller is the browser; nothing below it
  // can reach this method.
  std::string Mint(const std::string& job_id, Mode mode);

  // Whether this broker minted `handle_id` for this exact job and mode.
  bool IsMintedFor(const std::string& job_id,
                   const std::string& handle_id,
                   Mode mode) const;

  // The open resource, for an identifier this broker minted for this job in
  // this mode. Every other identifier yields an invalid file, including one
  // minted for a different job, a different mode, or a previous generation.
  base::File Resolve(const std::string& job_id,
                     const std::string& handle_id,
                     Mode mode);

  // Identifiers do not outlive what they were minted for. A job that ends,
  // however it ends, takes its own with it; a generation change takes all of
  // them, because an identifier minted under a previous profile generation
  // names a resource that generation owned.
  void RevokeJob(const std::string& job_id);
  void RevokeAll();

  size_t minted_count_for_testing() const { return records_.size(); }

 private:
  struct Record {
    std::string job_id;
    Mode mode;
  };

  ResolvePort resolve_;
  base::flat_map<std::string, Record> records_;
};

}  // namespace taffy

#endif  // TAFFY_SERVICES_TOOL_RUNTIME_SUPERVISOR_TOOL_HANDLE_BROKER_H_
