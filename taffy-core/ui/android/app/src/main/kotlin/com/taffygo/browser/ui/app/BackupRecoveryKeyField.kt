// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import android.content.Context
import android.graphics.Typeface
import android.os.Build
import android.text.InputFilter
import android.text.InputType
import android.view.View
import android.view.inputmethod.EditorInfo
import androidx.appcompat.widget.AppCompatEditText

/** The sole Android holder of key text: a transient, unsaved trusted view. */
class BackupRecoveryKeyField(context: Context) : AppCompatEditText(context) {
    init {
        isSaveEnabled = false
        isSaveFromParentEnabled = false
        importantForAutofill = View.IMPORTANT_FOR_AUTOFILL_NO_EXCLUDE_DESCENDANTS
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            importantForContentCapture = View.IMPORTANT_FOR_CONTENT_CAPTURE_NO_EXCLUDE_DESCENDANTS
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            setAccessibilityDataSensitive(View.ACCESSIBILITY_DATA_SENSITIVE_YES)
        }
        inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD or
            InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
        imeOptions = EditorInfo.IME_FLAG_NO_PERSONALIZED_LEARNING or EditorInfo.IME_ACTION_DONE
        typeface = Typeface.MONOSPACE
        // Bigger than the native78-character form: extra pasted bytes must
        // cause native refusal, never be truncated into a valid-looking key.
        filters = arrayOf(InputFilter.LengthFilter(256))
        setSingleLine(false)
        maxLines = 5
    }

    /** Copies into the editable buffer, then immediately wipes the native handoff. */
    fun showGeneratedKey(session: BackupRecoveryKeySession): Boolean {
        clearSensitiveText()
        val key = try {
            session.takeGeneratedKeyForDisplay()
        } catch (_: RuntimeException) {
            null
        } ?: return false
        return try {
            if (key.isEmpty() || key.size > 256) return false
            keyListener = null
            transformationMethod = null
            isLongClickable = false
            setTextIsSelectable(false)
            setText(key, 0, key.size)
            true
        } finally {
            key.fill('\u0000')
        }
    }

    /** No immutable String copy or Compose state is made from the entered key. */
    fun submitEnteredKey(session: BackupRecoveryKeySession): BackupRecoveryKeySession.Acceptance {
        val entered = text
        val key = CharArray(entered?.length ?: 0) { entered!![it] }
        return try {
            session.acceptEnteredKey(key)
        } catch (_: RuntimeException) {
            BackupRecoveryKeySession.Acceptance.UNAVAILABLE
        } finally {
            key.fill('\u0000')
            clearSensitiveText()
        }
    }

    fun clearSensitiveText() {
        text?.let { buffer ->
            for (index in 0 until buffer.length) buffer.replace(index, index + 1, "0")
            buffer.clear()
        }
    }

    override fun onDetachedFromWindow() {
        clearSensitiveText()
        super.onDetachedFromWindow()
    }
}
