// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNotSame;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertTrue;

import android.app.Activity;
import android.content.ClipData;
import android.content.Intent;
import android.net.Uri;
import android.provider.MediaStore;

import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.ui.base.WindowAndroid;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;

/** Host proof for the TaffyGo-owned boundary around Chromium's file chooser. */
@RunWith(BaseRobolectricTestRunner.class)
public class TaffyFileChooserWindowTest {
    private static final int PERSISTABLE_FLAGS =
            Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION;

    @Test
    public void getContentKeepsSelectionShapeButCannotAskForPersistence() {
        Intent original =
                new Intent(Intent.ACTION_GET_CONTENT)
                        .setType("text/csv")
                        .putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true)
                        .putExtra(Intent.EXTRA_MIME_TYPES, new String[] {"text/csv", "text/plain"})
                        .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | PERSISTABLE_FLAGS);

        Intent prepared = TaffyFileChooserWindow.prepareHtmlFileInputIntent(original);

        assertNotNull(prepared);
        assertNotSame(original, prepared);
        assertEquals(Intent.ACTION_GET_CONTENT, prepared.getAction());
        assertEquals("text/csv", prepared.getType());
        assertTrue(prepared.getBooleanExtra(Intent.EXTRA_ALLOW_MULTIPLE, false));
        assertEquals(2, prepared.getStringArrayExtra(Intent.EXTRA_MIME_TYPES).length);
        assertTrue((prepared.getFlags() & Intent.FLAG_GRANT_READ_URI_PERMISSION) != 0);
        assertEquals(0, prepared.getFlags() & PERSISTABLE_FLAGS);
        assertTrue((original.getFlags() & PERSISTABLE_FLAGS) != 0);
    }

    @Test
    public void chooserKeepsProviderAndCaptureChoicesButSanitizesEveryBranch() {
        Intent target =
                new Intent(Intent.ACTION_GET_CONTENT)
                        .setType("image/*")
                        .addFlags(PERSISTABLE_FLAGS);
        Intent camera =
                new Intent(MediaStore.ACTION_IMAGE_CAPTURE)
                        .addFlags(Intent.FLAG_GRANT_WRITE_URI_PERMISSION | PERSISTABLE_FLAGS);
        Intent chooser =
                new Intent(Intent.ACTION_CHOOSER)
                        .putExtra(Intent.EXTRA_INTENT, target)
                        .putExtra(Intent.EXTRA_INITIAL_INTENTS, new Intent[] {camera})
                        .addFlags(PERSISTABLE_FLAGS);

        Intent prepared = TaffyFileChooserWindow.prepareHtmlFileInputIntent(chooser);

        assertNotNull(prepared);
        Intent preparedTarget = prepared.getParcelableExtra(Intent.EXTRA_INTENT);
        assertNotNull(preparedTarget);
        assertEquals(Intent.ACTION_GET_CONTENT, preparedTarget.getAction());
        assertEquals("image/*", preparedTarget.getType());
        assertEquals(0, prepared.getFlags() & PERSISTABLE_FLAGS);
        assertEquals(0, preparedTarget.getFlags() & PERSISTABLE_FLAGS);
        Intent[] initial =
                (Intent[]) prepared.getParcelableArrayExtra(Intent.EXTRA_INITIAL_INTENTS);
        assertNotNull(initial);
        assertEquals(1, initial.length);
        assertEquals(MediaStore.ACTION_IMAGE_CAPTURE, initial[0].getAction());
        assertEquals(0, initial[0].getFlags() & PERSISTABLE_FLAGS);
        assertTrue((initial[0].getFlags() & Intent.FLAG_GRANT_WRITE_URI_PERMISSION) != 0);
    }

    @Test
    public void onlyHtmlInputPickerAndCaptureActionsAreAccepted() {
        assertNotNull(
                TaffyFileChooserWindow.prepareHtmlFileInputIntent(
                        new Intent(MediaStore.ACTION_PICK_IMAGES)));
        assertNotNull(
                TaffyFileChooserWindow.prepareHtmlFileInputIntent(
                        new Intent(MediaStore.ACTION_VIDEO_CAPTURE)));
        assertNotNull(
                TaffyFileChooserWindow.prepareHtmlFileInputIntent(
                        new Intent(MediaStore.Audio.Media.RECORD_SOUND_ACTION)));

        assertNull(
                TaffyFileChooserWindow.prepareHtmlFileInputIntent(
                        new Intent(Intent.ACTION_OPEN_DOCUMENT)));
        assertNull(
                TaffyFileChooserWindow.prepareHtmlFileInputIntent(
                        new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE)));
        assertNull(
                TaffyFileChooserWindow.prepareHtmlFileInputIntent(
                        new Intent(Intent.ACTION_CREATE_DOCUMENT)));
        assertNull(
                TaffyFileChooserWindow.prepareHtmlFileInputIntent(new Intent(Intent.ACTION_SEND)));
    }

    @Test
    public void chooserWithAnythingExceptGetContentAndCaptureFailsClosed() {
        Intent wrongTarget =
                new Intent(Intent.ACTION_CHOOSER)
                        .putExtra(Intent.EXTRA_INTENT, new Intent(Intent.ACTION_OPEN_DOCUMENT));
        Intent wrongInitial =
                new Intent(Intent.ACTION_CHOOSER)
                        .putExtra(Intent.EXTRA_INTENT, new Intent(Intent.ACTION_GET_CONTENT))
                        .putExtra(
                                Intent.EXTRA_INITIAL_INTENTS,
                                new Intent[] {new Intent(Intent.ACTION_SEND)});

        assertNull(TaffyFileChooserWindow.prepareHtmlFileInputIntent(wrongTarget));
        assertNull(TaffyFileChooserWindow.prepareHtmlFileInputIntent(wrongInitial));
    }

    @Test
    public void resultIsForwardedBeforeEveryContentGrantIsReleased() {
        Uri direct = Uri.parse("content://provider/direct");
        Uri first = Uri.parse("content://provider/first");
        Uri second = Uri.parse("content://provider/second");
        Intent result = new Intent().setData(direct);
        ClipData clip = ClipData.newRawUri("selection", first);
        clip.addItem(new ClipData.Item(second));
        clip.addItem(new ClipData.Item(Uri.parse("file:///not-a-provider-grant")));
        result.setClipData(clip);

        List<String> events = new ArrayList<>();
        WindowAndroid.IntentCallback wrapped =
                TaffyFileChooserWindow.transientResult(
                        (code, data) -> {
                            assertEquals(Activity.RESULT_OK, code);
                            assertSame(result, data);
                            events.add("callback");
                        },
                        (uri, flags) -> events.add(uri + ":" + flags));

        wrapped.onIntentCompleted(Activity.RESULT_OK, result);

        assertEquals("callback", events.get(0));
        assertEquals(7, events.size());
        for (Uri uri : List.of(direct, first, second)) {
            assertTrue(events.contains(uri + ":" + Intent.FLAG_GRANT_READ_URI_PERMISSION));
            assertTrue(events.contains(uri + ":" + Intent.FLAG_GRANT_WRITE_URI_PERMISSION));
        }
    }

    @Test
    public void deniedReleaseCannotReplaceTheChooserResultOrLeakTheUriToLogs() {
        Uri uri = Uri.parse("content://sensitive-provider/private-name");
        Intent result = new Intent().setData(uri);
        AtomicInteger callbacks = new AtomicInteger();
        WindowAndroid.IntentCallback wrapped =
                TaffyFileChooserWindow.transientResult(
                        (code, data) -> callbacks.incrementAndGet(),
                        (ignoredUri, ignoredFlags) -> {
                            throw new SecurityException("provider denied persistence");
                        });

        wrapped.onIntentCompleted(Activity.RESULT_OK, result);

        assertEquals(1, callbacks.get());
    }

    @Test
    public void largeContentUriIsForwardedByReferenceWithoutReadingPayloadBytes() {
        StringBuilder path = new StringBuilder(64 * 1024);
        for (int index = 0; index < 64 * 1024; index++) path.append('a');
        Uri large = Uri.parse("content://provider/" + path);
        Intent result = new Intent().setData(large);
        List<Uri> released = new ArrayList<>();
        WindowAndroid.IntentCallback wrapped =
                TaffyFileChooserWindow.transientResult(
                        (code, data) -> assertSame(large, data.getData()),
                        (uri, ignoredFlags) -> released.add(uri));

        wrapped.onIntentCompleted(Activity.RESULT_OK, result);

        assertEquals(List.of(large, large), released);
    }

    @Test
    public void cancellationIsForwardedAndRetainsNothing() {
        AtomicInteger callbacks = new AtomicInteger();
        AtomicInteger releases = new AtomicInteger();
        WindowAndroid.IntentCallback wrapped =
                TaffyFileChooserWindow.transientResult(
                        (code, data) -> {
                            assertEquals(Activity.RESULT_CANCELED, code);
                            assertNull(data);
                            callbacks.incrementAndGet();
                        },
                        (uri, flags) -> releases.incrementAndGet());

        wrapped.onIntentCompleted(Activity.RESULT_CANCELED, null);

        assertEquals(1, callbacks.get());
        assertEquals(0, releases.get());
    }
}
