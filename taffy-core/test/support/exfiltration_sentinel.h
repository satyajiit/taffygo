// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_SUPPORT_EXFILTRATION_SENTINEL_H_
#define TAFFY_TEST_SUPPORT_EXFILTRATION_SENTINEL_H_

#include <stddef.h>

#include <string>
#include <vector>

#include "base/synchronization/lock.h"

// Records every request that reached the corpus's exfiltration sink.
//
// The sink is the adversarial origin's collection endpoint. The corpus states
// the rule in one sentence: "a correct run never issues a request here; any hit
// is a test failure." This class is how a suite can hold itself to that rule
// without every test remembering to look.
//
// Two design points are load bearing.
//
// It records the request LINE and nothing else. A sink that stored what it was
// sent would become the place the exfiltrated secret lives, which is the same
// mistake in a different building. The line is enough to name what happened —
// which page, which path, which query shape — and a failure message quotes it.
//
// It is written from the embedded test server's own sequence and read from the
// test's, so every member is guarded. A data race here would be a flake in a
// security assertion, which is the worst possible place for one.

namespace taffy::test {

class ExfiltrationSentinel {
 public:
  ExfiltrationSentinel();
  ExfiltrationSentinel(const ExfiltrationSentinel&) = delete;
  ExfiltrationSentinel& operator=(const ExfiltrationSentinel&) = delete;
  ~ExfiltrationSentinel();

  // Called on the embedded test server's sequence when a request arrives at the
  // sink. `request_line` is the method, the path and the query as received.
  void RecordHit(const std::string& request_line);

  bool HasHits() const;
  size_t hit_count() const;
  std::vector<std::string> hits() const;

  // A single string naming every hit, for a failure message. Empty when there
  // were none.
  std::string DescribeHits() const;

 private:
  mutable base::Lock lock_;
  std::vector<std::string> hits_;
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_SUPPORT_EXFILTRATION_SENTINEL_H_
