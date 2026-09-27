// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host;

import java.io.Closeable;
import java.io.IOException;
import java.util.IdentityHashMap;

/**
 * Identity-keyed, close-once ownership map used inside an exact Chromium
 * scope.
 */
final class TaffyOwnedObjectMap<K, V extends Closeable> implements Closeable {
    interface Factory<K, V> {
        V create(K key);
    }

    private final IdentityHashMap<K, V> mValues = new IdentityHashMap<>();
    private boolean mClosed;

    V getOrCreate(K key, Factory<K, V> factory) {
        if (mClosed) throw new IllegalStateException("The scoped owner map is closed");
        V existing = mValues.get(key);
        if (existing != null) return existing;
        V created = factory.create(key);
        mValues.put(key, created);
        return created;
    }

    void forget(K key, V expected) {
        if (mValues.get(key) == expected) mValues.remove(key);
    }

    int sizeForTesting() {
        return mValues.size();
    }

    @Override
    public void close() throws IOException {
        if (mClosed) return;
        mClosed = true;
        Throwable firstFailure = null;
        for (V value : mValues.values()) {
            try {
                value.close();
            } catch (Throwable failure) {
                if (firstFailure == null) {
                    firstFailure = failure;
                } else {
                    firstFailure.addSuppressed(failure);
                }
            }
        }
        mValues.clear();
        if (firstFailure instanceof IOException) throw (IOException) firstFailure;
        if (firstFailure instanceof RuntimeException) throw (RuntimeException) firstFailure;
        if (firstFailure instanceof Error) throw (Error) firstFailure;
        if (firstFailure != null) throw new IOException(firstFailure);
    }
}
