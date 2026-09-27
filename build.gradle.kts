// Root build script. It declares plugin versions once (from the version
// catalog) and applies nothing: all shared configuration lives in the
// convention plugins under build-logic/.

plugins {
    alias(libs.plugins.android.application) apply false
    alias(libs.plugins.android.library) apply false
    // No Kotlin Android plugin: Kotlin is built into the Android Gradle Plugin
    // from AGP 9.0, and applying org.jetbrains.kotlin.android to an Android
    // module is a hard configuration error. See gradle/libs.versions.toml.
    alias(libs.plugins.kotlin.jvm) apply false
    alias(libs.plugins.kotlin.compose) apply false
    alias(libs.plugins.ksp) apply false
}
