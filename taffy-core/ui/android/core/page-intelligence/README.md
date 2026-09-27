# Android page intelligence

`:core:page-intelligence` is the Android projection of browser-owned page
intelligence. It exposes only generated Core API records through a narrow
window repository over the selected tab's independently owned client.

The Window-scoped Dagger binding never owns a WebContents or extends a tab
lifetime. This module contains no raw BIP type, task reducer, policy decision,
or native Rust bridge.
