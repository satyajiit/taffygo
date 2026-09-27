// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import androidx.compose.ui.graphics.ImageBitmap
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.assets.CountryFlagMember
import com.taffygo.browser.ui.core.assets.TaffyPartsRepository
import com.taffygo.browser.ui.core.common.AppDispatchers
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import com.taffygo.browser.ui.core.model.TaffyPart
import com.taffygo.browser.ui.core.model.TaffyPartAvailability
import com.taffygo.browser.ui.core.model.TaffyPartHold
import com.taffygo.browser.ui.core.model.TaffyPartId
import com.taffygo.browser.ui.core.ui.CountryFlagSource
import com.taffygo.browser.ui.core.ui.CountryFlagState
import com.taffygo.browser.ui.core.ui.normalizedFlagCode
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.async

/**
 * The published country-flag pack, as the three country screens draw it.
 *
 * The pack itself is asked for by `RequiredPartsInstaller` when the profile
 * starts, rather than from here: a picker that begins a download when it
 * opens is a picker that opens empty. This object turns an installed pack
 * into pictures.
 *
 * ## What was wrong with drawing them lazily on the composing thread
 *
 * Every flag used to read its own member and decode it inside `produceState`,
 * whose block runs on the composition's own context — the main thread. Screen
 * SCR-006's picker lists every country in the corpus, so scrolling it decoded
 * 192×144 WebP images on the thread that draws, one per row, and decoded them
 * again on the way back up.
 *
 * Three things fix that, and they are all here:
 *
 * - **Decoding happens on the default dispatcher.** Reading a member already
 *   suspended across the browser seam; decoding did not, and it was the
 *   expensive half.
 * - **One read per code and pack revision while the profile lives.** A
 *   composer asking for a flag another one is already decoding waits on that
 *   work instead of starting a second read of the same member. A `LazyColumn`
 *   composes the same row more than once as it settles, so this is the ordinary
 *   case rather than a race. The work is owned by the profile and not by the
 *   composer that happened to ask first, because leaving the screen must not
 *   cancel a decode the rest of the list is waiting on. A superseded pack is a
 *   different identity, so its late result can neither replace nor answer for
 *   the current one.
 * - **The cache holds a screenful of scrolling, and says what that costs.**
 *   Every flag in the corpus is 192×144, so a decoded one is 192 × 144 × 4 =
 *   110,592 bytes. [MAX_CACHED_FLAGS] of them is about seven megabytes, which
 *   is the budget this number comes from rather than a round-looking count.
 *
 * Decoded pictures stay on this object rather than in [TaffyPartsRepository],
 * which forbids caching there. The cache itself is [PackArtworkCache], shared
 * with the start page's plates: both read members of an installed pack, and
 * the revision bookkeeping is the part worth having once.
 */
class DeliveryCountryFlagSource(
    private val parts: TaffyPartsRepository,
    private val lifetime: TaffyProfileLifetime,
    private val dispatchers: AppDispatchers,
) : CountryFlagSource {

    private val artworkCache = PackArtworkCache(
        scope = lifetime.scope,
        dispatcher = dispatchers.default,
        maxEntries = MAX_CACHED_FLAGS,
        currentRevision = ::currentRevision,
        loader = { key ->
            parts.readMember(key.partId, key.member)?.let { bytes ->
                PackWebp.decode(
                    bytes,
                    expectedWidth = FLAG_WIDTH_PX,
                    expectedHeight = FLAG_HEIGHT_PX,
                    maxEncodedBytes = MAX_ENCODED_FLAG_BYTES,
                )
            }
        },
    )

    @Composable
    override fun flagFor(code: String): CountryFlagState {
        val iso = normalizedFlagCode(code) ?: return CountryFlagState.Absent
        val member = CountryFlagMember.pathFor(iso) ?: return CountryFlagState.Absent
        val partsState by parts.state.collectAsStateWithLifecycle()
        val part = partsState.parts.firstOrNull {
            it.id.value == CountryFlagMember.ASSET_ID
        }
        val key = part?.let {
            PackArtworkCache.Key(partId = it.id, version = it.version, member = member)
        }
        val cached = artworkCache.cached(key)
        val artwork by produceState(
            initialValue = initialState(part, cached),
            key,
            part?.availability,
            part?.hold,
        ) {
            value = resolve(key, part)
        }
        return artwork
    }

    private fun initialState(part: TaffyPart?, cached: ImageBitmap?): CountryFlagState = when {
        part == null -> CountryFlagState.Absent
        part.hold == TaffyPartHold.NOT_PUBLISHED -> CountryFlagState.Absent
        part.availability != TaffyPartAvailability.READY -> CountryFlagState.Loading
        cached != null -> CountryFlagState.Ready(cached)
        else -> CountryFlagState.Loading
    }

    /** What one code's artwork is, given what the plane says about the pack. */
    private suspend fun resolve(
        key: PackArtworkCache.Key?,
        part: TaffyPart?,
    ): CountryFlagState = when {
        part == null || key == null -> CountryFlagState.Absent
        part.hold == TaffyPartHold.NOT_PUBLISHED -> CountryFlagState.Absent
        part.availability != TaffyPartAvailability.READY -> CountryFlagState.Loading
        else -> artworkCache.cached(key)?.let(CountryFlagState::Ready)
            ?: artworkCache.load(key)?.let(CountryFlagState::Ready)
            ?: CountryFlagState.Absent
    }

    /** The revision the delivery plane currently permits this source to draw. */
    private fun currentRevision(): PackArtworkCache.Revision? =
        parts.state.value.parts.firstOrNull {
            it.id.value == CountryFlagMember.ASSET_ID
        }
            ?.takeIf {
                it.availability == TaffyPartAvailability.READY &&
                    it.hold != TaffyPartHold.NOT_PUBLISHED
            }
            ?.let { PackArtworkCache.Revision(it.id, it.version) }

    private companion object {
        /** About seven megabytes of 192×144 artwork. See the class comment. */
        const val MAX_CACHED_FLAGS: Int = 64

        /** The size every member of the published pack is. */
        const val FLAG_WIDTH_PX: Int = 192
        const val FLAG_HEIGHT_PX: Int = 144

        /**
         * Refusing before the real decode prevents a malformed compressed
         * member from turning the Core API's four-megabyte member allowance
         * into a giant bitmap.
         */
        const val MAX_ENCODED_FLAG_BYTES: Int = 256 * 1024
    }
}
