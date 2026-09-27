// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import java.util.TreeMap

/** Bounded newest-first storage with a second index for exact provider identities. */
internal class BoundedNewestFirstIndex<T : Any>(
    private val maxSize: Int,
    private val tokenOf: (T) -> String,
    private val timestampOf: (T) -> Long,
    private val sameVisibleValue: (T, T) -> Boolean,
) {
    private data class Order(val createdAtEpochMillis: Long, val token: String)

    private val byOrder = TreeMap<Order, T>(
        compareByDescending<Order> { it.createdAtEpochMillis }.thenBy(Order::token),
    )
    private val orderByToken = mutableMapOf<String, Order>()
    var overflowed: Boolean = false
        private set

    init {
        require(maxSize > 0)
    }

    operator fun get(token: String?): T? {
        val order = orderByToken[token] ?: return null
        return byOrder[order]
    }

    /** Inserts or updates one row and says whether the public ordered values changed. */
    fun upsert(value: T): Boolean {
        val token = tokenOf(value)
        val nextOrder = Order(timestampOf(value), token)
        val previousOrder = orderByToken[token]
        val previous = previousOrder?.let(byOrder::get)
        if (previous === value) return false

        if (previousOrder != null) byOrder.remove(previousOrder)
        byOrder[nextOrder] = value
        orderByToken[token] = nextOrder

        val evicted = if (byOrder.size > maxSize) {
            overflowed = true
            val oldest = checkNotNull(byOrder.lastEntry())
            byOrder.remove(oldest.key)
            orderByToken.remove(tokenOf(oldest.value))
            oldest.value
        } else {
            null
        }
        if (previous == null) return evicted?.let(tokenOf) != token
        return !sameVisibleValue(previous, value) || previousOrder != nextOrder
    }

    fun remove(token: String): Boolean {
        val order = orderByToken.remove(token) ?: return false
        return byOrder.remove(order) != null
    }

    fun values(): Collection<T> = byOrder.values

    fun replaceWith(other: BoundedNewestFirstIndex<T>) {
        clear()
        other.values().forEach(::upsert)
    }

    fun clear() {
        byOrder.clear()
        orderByToken.clear()
        overflowed = false
    }
}
