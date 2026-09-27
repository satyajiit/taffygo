// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import com.taffygo.browser.ui.core.model.ModelInputModality
import com.taffygo.browser.ui.core.model.ProviderModel

/**
 * What kind of work a model does, read off its catalog entry.
 *
 * The catalog is served (decision 0080), so the set of models changes without a
 * release and a heading chosen by matching a model's name would be right only
 * beside the catalog it was compiled with — and wrong in the quiet way, since a
 * misfiled model still draws a perfectly ordinary row. Every answer here comes
 * from two published facts: whether the model reasons before it answers, and
 * what it accepts as input.
 *
 * The members are ordered as the screen lists them, and a model belongs to
 * exactly one of them. [MULTIMODAL] is asked only of a model that is not
 * already [REASONING], so a reasoning model that also reads pictures is filed
 * once, under the harder thing it does, and says the rest on its own row.
 */
enum class ModelCapability {
    /** It reasons before it answers. */
    REASONING,

    /** It accepts more than text. */
    MULTIMODAL,

    /** Text in, text out. */
    STANDARD,

    ;

    companion object {
        /** Which kind of work [model] does, in that order of precedence. */
        fun of(model: ProviderModel): ModelCapability = when {
            model.reasoning -> REASONING
            ModelInputModality.IMAGE in model.inputModalities -> MULTIMODAL
            else -> STANDARD
        }
    }
}
