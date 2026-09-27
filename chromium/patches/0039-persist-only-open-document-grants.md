# 0039 — Persist only returned Open Document grants

**Status:** `[Current]` — exported against Chromium 152.0.7977.42 for
CAP-BR-010 and PAR-FILE-004–006
**Needed by:** HTML file inputs must receive only an activity-lifetime content
grant; process death must not turn a website upload choice into durable browser
storage authority
**Estimated size:** ~90 modified upstream lines, 2 files (one narrow condition
and two Robolectric regressions)

## Upstream files and symbols

| | |
|---|---|
| File | `//ui/android/java/src/org/chromium/ui/base/SelectFileDialog.java` |
| Symbol | `SelectFileDialog.onIntentCompleted` |
| File | `//ui/android/junit/src/org/chromium/ui/base/SelectFileDialogTest.java` |
| Symbol | GET_CONTENT and OPEN_DOCUMENT result-permission regressions |

## The change

`SelectFileDialog` currently calls `takePersistableUriPermission` for every
single `content://` result whenever the broad `SelectFileOpenDocument` feature
is enabled. That feature selects which intent is built, but it does not prove
that this particular result came from `ACTION_OPEN_DOCUMENT`: ordinary HTML
`<input type=file>` uses `ACTION_GET_CONTENT` and can reach the same block.

Gate persistence on the original `mIntentAction` being exactly
`ACTION_OPEN_DOCUMENT`. Within that path, request only the read/write modes the
result actually returned. `GET_CONTENT`, media-picker and camera results remain
transient; create-document and tree selection do not acquire an unrelated
content-read grant through this block.

The tests initialize a mocked application `ContentResolver`, drive the real
`selectFile` and `onIntentCompleted` methods, and prove both directions:
GET_CONTENT never invokes persistence even when a result advertises a
persistable mode, while OPEN_DOCUMENT persists its returned read mode and does
not ask for an absent write mode.

TaffyGo keeps the downstream half at its existing public seam:
`TaffyFileChooserWindow` permits only `SelectFileDialog`'s GET_CONTENT,
photo-picker and capture intents and rejects OPEN_DOCUMENT, CREATE_DOCUMENT and
OPEN_DOCUMENT_TREE. This patch therefore removes the process-death gap without
copying or replacing Chromium's file chooser.

## Why the overlay cannot host it

The persistence call is made inside `SelectFileDialog.onIntentCompleted` from
private `mIntentAction` state, before any embedder callback exists. A
`WindowAndroid` adapter can sanitize the launched intent and release a grant
after the callback returns, but it cannot prevent this private call; a process
kill between take and release would strand the grant. The upstream condition
is the only point that can make the non-persistence guarantee atomic.

## Rebase risk

**Low.** One condition and the modes supplied to an Android API change. A
SelectFileDialog result-path rewrite conflicts loudly. The tests exercise the
public behavior and continue to catch a broad feature-flag-only condition if
the block moves.

**Retirement:** upstream this correctness fix. Until then it is reviewed at
each Chromium milestone rebase and retires when upstream proves persistence is
already scoped to the action and returned modes.

## Verify at export

1. `SELECT_OPEN_FILE` and `SELECT_OPEN_MULTI_FILE` still use GET_CONTENT unless
   `SetOpenWritable(true)` was called.
2. File System Access is still the only Chromium caller of `SetOpenWritable`
   for page content.
3. `SelectFileDialogTest` passes both new persistence tests.
4. TaffyGo's `WindowAndroid` boundary still rejects OPEN_DOCUMENT,
   CREATE_DOCUMENT and OPEN_DOCUMENT_TREE.

## Regenerating the patch

```bash
./tools/chromium/sync
# edit SelectFileDialog.java and SelectFileDialogTest.java in the checkout
# commit with `Taffy-Patch: 0039`
./tools/chromium/export-patches
./tools/check fast --only chromium
```
