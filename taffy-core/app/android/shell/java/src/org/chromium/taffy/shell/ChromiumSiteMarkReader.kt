// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import android.graphics.Bitmap
import org.chromium.base.lifetime.Destroyable
import org.chromium.chrome.browser.profiles.Profile
import org.chromium.chrome.browser.ui.favicon.FaviconHelper
import org.chromium.url.GURL

/** Bounded, profile-local favicon reads for the start page's site marks. */
internal class ChromiumSiteMarkReader(
    private val schedule: (Runnable) -> Unit,
    private val cancel: (Runnable) -> Unit,
) : Destroyable {
    private var helper: FaviconHelper? = null
    private val pending = mutableSetOf<String>()
    private val cache = BoundedSiteMarkCache<Bitmap>(MAX_CACHED_SITE_MARKS)
    private val publisher = CoalescedSiteMarkPublisher(cache::snapshot, schedule, cancel)
    private var destroyed = false

    fun request(
        profile: Profile,
        hosts: Collection<String>,
        publish: (Map<String, Bitmap>) -> Unit,
    ) {
        check(!destroyed) { "A destroyed site-mark reader cannot act" }
        val reader = helper ?: FaviconHelper().also { helper = it }
        var considered = 0
        for (host in hosts) {
            if (considered >= MAX_SITE_MARKS_PER_REQUEST) break
            if (!isBoundedHost(host)) continue
            considered++
            if (cache.touch(host) || host in pending) continue
            if (pending.size >= MAX_PENDING_SITE_MARKS || !pending.add(host)) continue
            val asked = reader.getLocalFaviconImageForURL(
                profile,
                GURL("https://$host/"),
                REQUESTED_MARK_PIXELS,
                /* fallbackToHost= */ true,
            ) { bitmap, _ ->
                if (!destroyed) {
                    pending -= host
                    if (bitmap != null && bitmap.width in 1..MAX_RETURNED_MARK_PIXELS &&
                        bitmap.height in 1..MAX_RETURNED_MARK_PIXELS
                    ) {
                        cache.put(host, bitmap)
                        publisher.request(publish)
                    }
                }
            }
            if (!asked) pending -= host
        }
    }

    override fun destroy() {
        if (destroyed) return
        destroyed = true
        val reader = helper
        helper = null
        publisher.destroy()
        pending.clear()
        cache.clear()
        reader?.destroy()
    }

    private companion object {
        const val MAX_CACHED_SITE_MARKS = 256
        const val MAX_PENDING_SITE_MARKS = 256
        const val MAX_SITE_MARKS_PER_REQUEST = 256
        const val MAX_HOST_CHARS = 253
        const val REQUESTED_MARK_PIXELS = 64
        const val MAX_RETURNED_MARK_PIXELS = 128

        fun isBoundedHost(host: String): Boolean =
            host.isNotBlank() && host.length <= MAX_HOST_CHARS &&
                host.none { it.isWhitespace() || it == '/' || it == '\\' }
    }
}
