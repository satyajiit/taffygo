// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/download_record.h"

namespace taffy {

double DownloadProgressFraction(const DownloadRecord& record) {
  if (record.total_bytes <= 0) {
    // The server did not say how large the file is. -1 rather than 0 so a
    // progress surface shows a spinner instead of a bar that never moves.
    return -1.0;
  }
  if (record.received_bytes <= 0) {
    return 0.0;
  }
  if (record.received_bytes >= record.total_bytes) {
    return 1.0;
  }
  return static_cast<double>(record.received_bytes) /
         static_cast<double>(record.total_bytes);
}

}  // namespace taffy
