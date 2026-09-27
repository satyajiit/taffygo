// The Gradle build root is the repository root: `./gradlew` and the version
// catalog live here (TOOLCHAIN.md, testing-and-delivery section 6.2). Gradle
// owns the Android UI source tree under taffy-core/ui/android; Chromium GN
// consumes that same source authority for the shipping browser.

pluginManagement {
    includeBuild("build-logic")
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "taffy-go"

// Module registration is generated from the same component manifest that
// owns process, trust, source, GN, and exact Gradle dependency edges.
apply(from = "taffy-core/build/generated/android-modules.settings.gradle.kts")
