// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.assets.TaffyPartsRepository
import com.taffygo.browser.ui.core.common.AppDispatchers
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import com.taffygo.browser.ui.core.model.TaffyPart
import com.taffygo.browser.ui.core.model.TaffyPartAvailability
import com.taffygo.browser.ui.core.model.TaffyPartHold
import com.taffygo.browser.ui.core.ui.StartSceneMember
import com.taffygo.browser.ui.core.ui.StartSceneSource
import com.taffygo.browser.ui.core.ui.StartSceneState
import com.taffygo.browser.ui.core.ui.TaffyStartScene

/**
 * The published start-scene pack, as the start page draws it.
 *
 * The pack is asked for by `RequiredPartsInstaller` when the profile starts,
 * never from here: a page that began a download when it opened would open
 * empty. This object turns an installed pack into a picture, and answers
 * [StartSceneState.Fallback] for every other state so the page draws the plate
 * compiled into the installer instead of a hole.
 *
 * It is deliberately thin next to [DeliveryCountryFlagSource]. That one carries
 * the hard case — a list decoding two hundred members while it scrolls — and the
 * machinery it needed for it, [PackArtworkCache], is shared here rather than
 * copied. This surface asks for one member at a time.
 *
 * The cache is small and the arithmetic is the reason rather than a round
 * number: a 640 by 480 plate decoded at the sample the start page's 120 dp
 * draw allows is 160 by 120, and even undersampled at 320 by 240 four of them
 * is about 1.2 megabytes. Four is a day's worth — the four parts of the day a
 * person could cross in one session — so nothing is decoded twice for want of
 * room.
 */
class DeliveryStartSceneSource(
    private val parts: TaffyPartsRepository,
    private val lifetime: TaffyProfileLifetime,
    private val dispatchers: AppDispatchers,
) : StartSceneSource {

    private val artworkCache = PackArtworkCache(
        scope = lifetime.scope,
        dispatcher = dispatchers.default,
        maxEntries = MAX_CACHED_SCENES,
        currentRevision = ::currentRevision,
        loader = { key ->
            parts.readMember(key.partId, key.member)?.let { bytes ->
                PackWebp.decode(
                    bytes,
                    expectedWidth = SCENE_WIDTH_PX,
                    expectedHeight = SCENE_HEIGHT_PX,
                    maxEncodedBytes = MAX_ENCODED_SCENE_BYTES,
                    targetWidthPx = drawnWidthPx,
                )
            }
        },
    )

    /**
     * The width the plate is currently drawn at, for the decode sample alone.
     *
     * Not part of the cache key, and deliberately: the same plate at 120 dp and
     * at 168 dp is the same picture, and keying on the width would decode it
     * twice to answer one question. A stale value costs resolution and never
     * correctness, which is why one field is enough where the key needs a lock.
     */
    @Volatile
    private var drawnWidthPx: Int = SCENE_WIDTH_PX

    @Composable
    override fun sceneFor(scene: TaffyStartScene, targetWidthPx: Int): StartSceneState {
        val member = StartSceneMember.pathFor(scene) ?: return StartSceneState.Fallback
        if (targetWidthPx > 0) drawnWidthPx = targetWidthPx
        val partsState by parts.state.collectAsStateWithLifecycle()
        val part = partsState.parts.firstOrNull { it.id.value == StartSceneMember.ASSET_ID }
        val key = part?.let {
            PackArtworkCache.Key(partId = it.id, version = it.version, member = member)
        }
        val cached = artworkCache.cached(key)
        val artwork by produceState(
            initialValue = cached?.let(StartSceneState::Ready) ?: StartSceneState.Fallback,
            key,
            part?.availability,
            part?.hold,
        ) {
            value = resolve(key, part)
        }
        return artwork
    }

    /** What the plate is, given what the plane says about the pack. */
    private suspend fun resolve(
        key: PackArtworkCache.Key?,
        part: TaffyPart?,
    ): StartSceneState = when {
        part == null || key == null -> StartSceneState.Fallback
        part.hold == TaffyPartHold.NOT_PUBLISHED -> StartSceneState.Fallback
        part.availability != TaffyPartAvailability.READY -> StartSceneState.Fallback
        else -> artworkCache.cached(key)?.let(StartSceneState::Ready)
            ?: artworkCache.load(key)?.let(StartSceneState::Ready)
            ?: StartSceneState.Fallback
    }

    /** The revision the delivery plane currently permits this source to draw. */
    private fun currentRevision(): PackArtworkCache.Revision? =
        parts.state.value.parts.firstOrNull { it.id.value == StartSceneMember.ASSET_ID }
            ?.takeIf {
                it.availability == TaffyPartAvailability.READY &&
                    it.hold != TaffyPartHold.NOT_PUBLISHED
            }
            ?.let { PackArtworkCache.Revision(it.id, it.version) }

    private companion object {
        /** Four plates: the four parts of a day a session could cross. */
        const val MAX_CACHED_SCENES: Int = 4

        /** The size every member of the published pack is. */
        const val SCENE_WIDTH_PX: Int = 640
        const val SCENE_HEIGHT_PX: Int = 480

        /**
         * Refusing before the real decode prevents a malformed compressed
         * member from turning the Core API's four-megabyte member allowance
         * into a giant bitmap. A plate encoded at the recorded recipe is about
         * fifty kilobytes; this is a wide margin around that, not a target.
         */
        const val MAX_ENCODED_SCENE_BYTES: Int = 512 * 1024
    }
}
