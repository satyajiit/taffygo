// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_TASK_NAVIGATION_REFUSAL_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_TASK_NAVIGATION_REFUSAL_H_

#include "content/public/browser/navigation_handle_user_data.h"
#include "taffy/common/public/bip_result.h"

namespace content {
class NavigationHandle;
}  // namespace content

namespace taffy {

// The browser's own refusal of a task navigation, carried on the navigation
// it refused.
//
// `TaskNavigationThrottle` cancels a task's request that its authority does
// not reach, and a cancelled request commits nothing. The verifier waiting on
// that navigation sees only that it did not commit, and on its own that is
// not a fact about the page: a navigation also ends uncommitted when another
// overtakes it or its response turns out to be a download. So the throttle
// says which refusal it was, here, before it cancels, and the verifier
// answers with that refusal's code - one whose side effect is "not
// performed" - instead of a contradiction the task can only read as an
// outcome nobody knows (decision 0228).
class TaskNavigationRefusal final
    : public content::NavigationHandleUserData<TaskNavigationRefusal> {
 public:
  TaskNavigationRefusal(const TaskNavigationRefusal&) = delete;
  TaskNavigationRefusal& operator=(const TaskNavigationRefusal&) = delete;
  ~TaskNavigationRefusal() override;

  // `kEgressNotAuthorized` or `kDestinationClassRestricted`. Both say the
  // request never reached a document, which is the whole content of a
  // cancellation at the throttle.
  ActionResultCode code() const { return code_; }

 private:
  friend class content::NavigationHandleUserData<TaskNavigationRefusal>;

  TaskNavigationRefusal(content::NavigationHandle& handle,
                        ActionResultCode code);

  const ActionResultCode code_;

  NAVIGATION_HANDLE_USER_DATA_KEY_DECL();
};

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_TASK_NAVIGATION_REFUSAL_H_
