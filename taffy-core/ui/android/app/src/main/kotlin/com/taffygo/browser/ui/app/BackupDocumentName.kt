// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

/** Reject rather than truncate a misleading deletion label. It never authorizes document access. */
fun isBackupDocumentNameSafe(name: String): Boolean =
    name.length in 1..255 && name.isNotBlank() && name == name.trim() &&
        name.codePoints().allMatch { point ->
            Character.getType(point) !in setOf(
                Character.CONTROL.toInt(), Character.FORMAT.toInt(), Character.SURROGATE.toInt(),
                Character.LINE_SEPARATOR.toInt(), Character.PARAGRAPH_SEPARATOR.toInt(),
            )
        }
