// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_RECOVERY_TASK_PDF_HANDOFF_TEST_SUPPORT_H_
#define TAFFY_TEST_RECOVERY_TASK_PDF_HANDOFF_TEST_SUPPORT_H_

#include <string_view>

#include "base/functional/callback_forward.h"

class Profile;
namespace ui {
class WindowAndroid;
}

namespace taffy::test {

// Runs the shipping task download controller against the live Profile. True
// means Android accepted its chooser, not that the person opened the viewer.
void StartTaskPdfHandoff(Profile* profile,
                         ui::WindowAndroid* window,
                         std::string_view task_id,
                         std::string_view download_guid,
                         base::OnceCallback<void(bool)> callback);

}  // namespace taffy::test

#endif  // TAFFY_TEST_RECOVERY_TASK_PDF_HANDOFF_TEST_SUPPORT_H_
