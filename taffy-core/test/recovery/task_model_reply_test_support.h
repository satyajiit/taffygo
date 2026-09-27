// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_RECOVERY_TASK_MODEL_REPLY_TEST_SUPPORT_H_
#define TAFFY_TEST_RECOVERY_TASK_MODEL_REPLY_TEST_SUPPORT_H_

#include <string>

#include "base/values.h"

namespace taffy::test {

// Provider-shaped replies at the test network boundary. They do not inject
// task effects, handle identities, policy decisions or storage completions.
std::string StreamedToolReply(std::string id,
                              std::string name,
                              base::DictValue arguments);
std::string StreamedFinalReply(std::string text);

}  // namespace taffy::test

#endif  // TAFFY_TEST_RECOVERY_TASK_MODEL_REPLY_TEST_SUPPORT_H_
