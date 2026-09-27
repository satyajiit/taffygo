// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Everything screen SCR-409 can be asked to do. */
sealed interface HelpIntent {
    /**
     * The screen handed a feedback draft to the phone's email app. [opened] is
     * false when no app took it, which is the only thing this screen can know
     * about an email: whether it was sent is the person's business and their
     * email app's.
     */
    data class FeedbackByEmail(val opened: Boolean) : HelpIntent

    /** Open GitHub's new-issue page for the public repository in a new tab. */
    data object OpenPublicIssue : HelpIntent
    data object Dismiss : HelpIntent
}
