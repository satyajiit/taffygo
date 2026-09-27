// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The platform matrix an asset is published for.
//!
//! An asset's bytes are almost never portable — a Python standard library is
//! compiled against one ABI and one page size — so a catalog entry publishes
//! per platform and the plane refuses an entry with no variant for the device
//! it is on. That refusal is the whole reason this enumeration is closed: an
//! unknown platform string must not resolve to "probably fine".
//!
//! Android is the platform the product runs on today. macOS and Windows are
//! named here rather than added later because the shape of a catalog row is
//! what a second platform costs, and paying it once at the start costs nothing.

use core::fmt;

/// One target the delivery origin publishes bytes for.
#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub enum Platform {
    /// Android on 64-bit Arm — phones and tablets.
    AndroidArm64,
    /// Android on 64-bit x86 — emulators.
    AndroidX64,
    /// macOS on Apple silicon.
    MacosArm64,
    /// macOS on 64-bit x86.
    MacosX64,
    /// Windows on 64-bit x86.
    WindowsX64,
    /// Windows on 64-bit Arm.
    WindowsArm64,
    /// A build the catalog publishes nothing for.
    ///
    /// A host test binary is one, and so is a target the product has not
    /// shipped to yet. It is a member rather than an absence because the
    /// alternative is a build naming some other platform to have a value at
    /// all, and then asking an origin for bytes compiled for a machine it is
    /// not. No catalog row may name it; nothing is published for it, so a
    /// device on it fetches nothing and is told so.
    Unsupported,
}

impl Platform {
    /// Every platform, in declaration order.
    pub const ALL: [Self; 7] = [
        Self::AndroidArm64,
        Self::AndroidX64,
        Self::MacosArm64,
        Self::MacosX64,
        Self::WindowsX64,
        Self::WindowsArm64,
        Self::Unsupported,
    ];

    /// The name the catalog source and the generated table use.
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::AndroidArm64 => "android-arm64",
            Self::AndroidX64 => "android-x64",
            Self::MacosArm64 => "macos-arm64",
            Self::MacosX64 => "macos-x64",
            Self::WindowsX64 => "windows-x64",
            Self::WindowsArm64 => "windows-arm64",
            Self::Unsupported => "unsupported",
        }
    }

    /// Parses a catalog name, or `None` when it names no platform.
    pub fn parse(text: &str) -> Option<Self> {
        Self::ALL.into_iter().find(|it| it.as_str() == text)
    }
}

impl fmt::Display for Platform {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(self.as_str())
    }
}

#[cfg(test)]
mod tests {
    use super::Platform;

    #[test]
    fn every_platform_round_trips_through_its_name() {
        for platform in Platform::ALL {
            assert_eq!(Platform::parse(platform.as_str()), Some(platform));
        }
    }

    #[test]
    fn every_name_is_distinct() {
        let mut names: Vec<&str> = Platform::ALL.iter().map(|it| it.as_str()).collect();
        names.sort_unstable();
        let count = names.len();
        names.dedup();
        assert_eq!(names.len(), count);
    }

    #[test]
    fn an_unknown_platform_is_none_rather_than_a_default() {
        assert_eq!(Platform::parse("linux-x64"), None);
        assert_eq!(Platform::parse(""), None);
    }

    #[test]
    fn nothing_is_ever_published_for_the_unsupported_platform() {
        // The catalog generator refuses `unsupported` as a variant platform,
        // so this is the plane's half of the same rule: whatever the catalog
        // holds, a build on this platform is answered with nothing.
        let catalog = crate::Catalog::product();
        assert_eq!(catalog.required_for(Platform::Unsupported).count(), 0);
        for entry in catalog.entries() {
            assert!(entry
                .variants()
                .iter()
                .all(|variant| variant.platform() != Platform::Unsupported));
        }
    }
}
