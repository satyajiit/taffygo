// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.annotation.StringRes
import com.taffygo.browser.ui.core.model.Fact
import com.taffygo.browser.ui.core.model.FactKind

/**
 * The label a fact carries, which is never absent.
 *
 * A correction outranks the kind, because "you entered" is the most important
 * thing to say about a cell the user changed; a conflict outranks the kind,
 * because a disagreement is the thing that needs looking at.
 *
 * WHY THIS IS IN `core/ui` AND NOT IN A FEATURE. It was in two: `feature/assistant`
 * rendered it under a task and `feature/workspaces` rendered it in a workspace
 * table, and the two functions and their six strings were identical in both
 * locales. Gradle hid that — each module gets its own `R`, so two copies of one
 * string name never met. One APK is where they meet: an Android resource table
 * is a single flat namespace, and `aapt2 link` refuses a name defined twice by
 * two peers, identical values included. The build that found it was the first
 * one to package both features together.
 *
 * So the duplication was never a fork problem; it was a product problem the fork
 * made visible. A fact's label is one piece of vocabulary and belongs in one
 * place, next to the controls that render it.
 */
@StringRes
public fun factKindLabel(fact: Fact): Int = when {
    fact.correction != null -> R.string.taffy_fact_kind_you_entered
    fact.needsANewSource -> R.string.taffy_fact_kind_needs_a_new_source
    fact.hasConflict -> R.string.taffy_fact_kind_conflict
    else -> when (fact.kind) {
        FactKind.FROM_THE_PAGE -> R.string.taffy_fact_kind_from_the_page
        FactKind.SUMMARIZED -> R.string.taffy_fact_kind_summarized
        FactKind.TAFFY_INFERENCE -> R.string.taffy_fact_kind_inference
        FactKind.YOU_ENTERED -> R.string.taffy_fact_kind_you_entered
    }
}
