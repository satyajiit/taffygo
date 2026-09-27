# `:core:credentials`

**Status:** `[Current]` The settings surface's narrow provider-credential port.

The port exposes configured provider identifiers and save/forget intents only.
Raw credential material is handed immediately to the Android profile adapter;
no feature receives a secure-store or browser handle.
