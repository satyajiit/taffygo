// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.model.TaffyPartId
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.async

/**
 * One profile's decoded artwork from an installed pack, behind an exact
 * revision-aware key.
 *
 * It is shared rather than written twice: the country flags and the start
 * page's plates ask the same question of the same delivery plane, and the
 * hard part is not the decode but the revision bookkeeping below. The key
 * names a [member] of a pack rather than a country, which is the only thing
 * that had to widen.
 *
 * [currentRevision] is the authority, not the composition that asked: a row
 * can still be finishing a composition from the old parts snapshot after the
 * repository has published a new one. Every revision transition advances an
 * epoch and prunes both decoded entries and joinable work. The epoch is what
 * closes the v1 -> v2 -> v1 race: even an old v1 completion has no authority
 * in the later v1 epoch.
 *
 * The type parameter keeps the concurrency policy runnable in a plain host
 * test. Production binds it to `ImageBitmap`; tests use content-free tokens.
 */
internal class PackArtworkCache<Artwork : Any>(
    private val scope: CoroutineScope,
    private val dispatcher: CoroutineDispatcher,
    private val maxEntries: Int,
    private val currentRevision: () -> Revision?,
    private val loader: suspend (Key) -> Artwork?,
) {
    /** Exact identity of one decoded member. */
    data class Key(
        val partId: TaffyPartId,
        val version: String,
        val member: String,
    ) {
        val revision: Revision
            get() = Revision(partId, version)
    }

    /** Identity shared by every member of one published pack. */
    data class Revision(val partId: TaffyPartId, val version: String)

    private data class Work<Value : Any>(
        val epoch: Long,
        val deferred: Deferred<Value?>,
    )

    private val lock = Any()
    private val cache = object : LinkedHashMap<Key, Artwork>(16, 0.75f, true) {
        override fun removeEldestEntry(
            eldest: MutableMap.MutableEntry<Key, Artwork>?,
        ): Boolean = size > maxEntries
    }
    private val inFlight = mutableMapOf<Key, Work<Artwork>>()
    private var retainedRevision: Revision? = null
    private var revisionEpoch: Long = 0L

    init {
        require(maxEntries > 0) { "Country flag cache capacity must be positive" }
    }

    /** The current revision's decoded value, pruning superseded state first. */
    fun cached(key: Key?): Artwork? = synchronized(lock) {
        val current = synchronizeRevisionLocked()
        key?.takeIf { it.revision == current }?.let(cache::get)
    }

    /** Read and decode one exact member, or join that same work already running. */
    suspend fun load(key: Key): Artwork? {
        cached(key)?.let { return it }
        val work = synchronized(lock) {
            val current = synchronizeRevisionLocked()
            if (key.revision != current) return null
            cache[key]?.let { return it }
            inFlight[key] ?: startLocked(key)
        }
        work.deferred.start()
        val decoded = try {
            work.deferred.await()
        } catch (failure: Throwable) {
            if (failure is CancellationException) throw failure
            null
        } finally {
            if (work.deferred.isCompleted) forget(key, work)
        }
        return synchronized(lock) {
            val current = synchronizeRevisionLocked()
            if (work.epoch == revisionEpoch && key.revision == current) {
                cache[key] ?: decoded
            } else {
                null
            }
        }
    }

    private fun startLocked(key: Key): Work<Artwork> {
        val startedEpoch = revisionEpoch
        val deferred = scope.async(dispatcher, start = CoroutineStart.LAZY) {
            val decoded = try {
                loader(key)
            } catch (failure: Throwable) {
                if (failure is CancellationException) throw failure
                null
            }
            if (decoded != null) store(key, startedEpoch, decoded)
            decoded
        }
        val work = Work(startedEpoch, deferred)
        inFlight[key] = work
        deferred.invokeOnCompletion { forget(key, work) }
        return work
    }

    private fun store(key: Key, startedEpoch: Long, artwork: Artwork) {
        synchronized(lock) {
            val current = synchronizeRevisionLocked()
            if (startedEpoch == revisionEpoch && key.revision == current) {
                cache[key] = artwork
            }
        }
    }

    private fun forget(key: Key, work: Work<Artwork>) {
        synchronized(lock) {
            if (inFlight[key] === work) inFlight.remove(key)
        }
    }

    private fun synchronizeRevisionLocked(): Revision? {
        val current = currentRevision()
        if (current != retainedRevision) {
            retainedRevision = current
            revisionEpoch += 1
            cache.entries.removeAll { (key) -> key.revision != current }
            val obsolete = inFlight.values.map { work -> work.deferred }
            inFlight.clear()
            obsolete.forEach { work -> work.cancel() }
        }
        return current
    }
}
