# `:core:preferences`

**Status:** `[Current]` UI-safe profile preference values and their narrow
read/write port.

The module contains no Android storage implementation. Chromium-backed profile
storage is supplied by `:app`, preserving the platform boundary while every
surface shares one typed preference vocabulary.
