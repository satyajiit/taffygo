# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Shrinker rules for the Gradle Android UI assembly.
#
# The convention plugin turns minification and resource shrinking on for the
# release build type and names this file, so it has to exist even when it says
# almost nothing: a `proguardFiles` entry pointing at a missing file fails the
# build rather than being skipped.
#
# It says almost nothing on purpose. Every external library ships its own
# consumer rules inside its artifact — Compose, Dagger, AppCompat, Lifecycle,
# and App Startup all do — and a
# keep rule copied here would be a second, staler copy of a rule the library
# already maintains. A rule belongs in this file only when it protects
# something this application knows about and no library can see.
#
# The shipping browser is assembled by Chromium GN and has its own shrinker
# closure. This file is for the Gradle verification assembly only.

# Kotlin puts the names the shrinker cannot infer into these attributes;
# without them a stack trace from a release build cannot be read back to a
# line, and a reflective Compose or serialization lookup fails at run time
# rather than at build time.
-keepattributes SourceFile,LineNumberTable
-keepattributes Signature,InnerClasses,EnclosingMethod
-keepattributes *Annotation*

# Leave the original source file name in place rather than replacing it with a
# placeholder so local verification crashes retain a useful source name.
-renamesourcefileattribute SourceFile
