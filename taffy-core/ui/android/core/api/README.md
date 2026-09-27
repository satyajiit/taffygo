# Android Core API

This module compiles the generated Core API Kotlin projection and contains the
thin Android-to-browser facade. It owns no task reducer, policy decision,
network client, account protocol, storage engine, or fallback runtime.

Chromium supplies the native implementation in the browser process. A missing
native binding fails immediately; it is never replaced by a Kotlin task engine.

`SavedFlowReviewRepository` keeps at most four complete reviews in a window's
memory. It fills only omitted review bodies whose generation and all current
metadata still match, and serializes explicit find, full-review and open-page
requests. The browser/core owns exact goal matching, current-version checks
and manual navigation. Reading a review never accepts a draft or starts a task.
