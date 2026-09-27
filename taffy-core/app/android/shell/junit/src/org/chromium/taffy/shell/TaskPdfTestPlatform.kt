// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import android.app.Application
import android.content.ComponentName
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.ActivityInfo
import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import android.content.pm.ResolveInfo
import android.net.Uri
import androidx.test.core.app.ApplicationProvider
import org.robolectric.Shadows.shadowOf

/** Real handoff and Android intent recording with only the installed viewer roster replaced. */
internal class TaskPdfTestPlatform {
    val application = ApplicationProvider.getApplicationContext<Application>()
    val handoff = TaffyTaskPdfHandoff(application)
    private val registered = mutableListOf<ComponentName>()

    init {
        setHandlers(handler("example.pdfreader"))
    }

    fun setHandlers(vararg handlers: ResolveInfo) {
        val packages = application.packageManager
        val shadow = shadowOf(packages)
        registered.forEach(shadow::removeActivity)
        registered.clear()
        for (handler in handlers) {
            val activity = handler.activityInfo
            val component = ComponentName(activity.packageName, activity.name)
            shadow.addOrUpdateActivity(activity)
            shadow.clearIntentFilterForActivity(component)
            shadow.addIntentFilterForActivity(component, IntentFilter(Intent.ACTION_VIEW).apply {
                addCategory(Intent.CATEGORY_DEFAULT)
                addDataScheme("content")
                addDataType("application/pdf")
            })
            packages.setApplicationEnabledSetting(
                activity.packageName,
                if (activity.applicationInfo.enabled) PackageManager.COMPONENT_ENABLED_STATE_ENABLED
                else PackageManager.COMPONENT_ENABLED_STATE_DISABLED,
                0,
            )
            registered += component
        }
    }

    fun handler(packageName: String) = ResolveInfo().apply {
        isDefault = true
        activityInfo = ActivityInfo().apply {
            this.packageName = packageName
            name = "$packageName.PdfActivity"
            exported = true
            enabled = true
            applicationInfo = ApplicationInfo().apply {
                this.packageName = packageName
                enabled = true
            }
        }
    }

    fun removeViewer() = setHandlers()

    fun takeChooser(): Intent? = shadowOf(application).nextStartedActivity

    companion object {
        val URI: Uri = Uri.parse("content://downloads/report.pdf")
    }
}
