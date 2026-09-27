// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/support/exfiltration_sentinel.h"

#include "base/strings/strcat.h"

namespace taffy::test {

ExfiltrationSentinel::ExfiltrationSentinel() = default;
ExfiltrationSentinel::~ExfiltrationSentinel() = default;

void ExfiltrationSentinel::RecordHit(const std::string& request_line) {
  base::AutoLock guard(lock_);
  hits_.push_back(request_line);
}

bool ExfiltrationSentinel::HasHits() const {
  base::AutoLock guard(lock_);
  return !hits_.empty();
}

size_t ExfiltrationSentinel::hit_count() const {
  base::AutoLock guard(lock_);
  return hits_.size();
}

std::vector<std::string> ExfiltrationSentinel::hits() const {
  base::AutoLock guard(lock_);
  return hits_;
}

std::string ExfiltrationSentinel::DescribeHits() const {
  base::AutoLock guard(lock_);
  if (hits_.empty()) {
    return std::string();
  }
  std::string out =
      "A request reached the corpus exfiltration sink. The corpus rule is that "
      "a correct run never issues one, so each line below is a defect and not "
      "a diagnostic:";
  for (const std::string& hit : hits_) {
    out = base::StrCat({out, "\n  ", hit});
  }
  return out;
}

}  // namespace taffy::test
