// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The compiled-in catalog: what assets exist and what their bytes are.
//!
//! # Adding an asset is one edit
//!
//! Everything about an asset is data. Add a row to
//! `catalog/source/assets.json`, run the generator, and the asset exists: the
//! plane will plan it, the browser will fetch and verify it, the register will
//! resolve it and the Taffy Assets surface will list it. No Rust, C++ or Kotlin
//! change is involved, and `a_new_row_needs_no_code` in this module is the test
//! that keeps it that way.
//!
//! That is the point of the design rather than a convenience of it. An artifact
//! whose delivery needs code is an artifact whose delivery has a second place
//! to be wrong, and the second place is always the one nobody updates.
//!
//! # What a consumer may ask
//!
//! By kind and platform ([`Catalog::published_for`]), or by identity and
//! revision ([`Catalog::find`]). Never by a literal identity written into a
//! consumer: a consumer that names `python-stdlib` is a consumer that has to be
//! edited when a second standard library ships beside the first.

mod entry;

// `rustfmt` collapses a short `Variant::new(...)` onto one line and expands a
// long one, so a generated table it has reformatted no longer matches what the
// generator emits. Without this attribute `cargo fmt --all` and
// `generate_catalog.py --check` contradict each other permanently on a tree
// nobody edited — the same trap `bip-types` carries the same attribute for.
#[rustfmt::skip]
mod generated;

pub use entry::{
    ArtifactRole, Container, Kind, ModelFacts, ModelFormat, Necessity, Publication, Variant,
};

use crate::ids::{AssetId, AssetRevision};
use crate::platform::Platform;

pub use entry::CatalogEntry;

/// Every asset the product knows about.
///
/// One value, built at compile time. There is no constructor that reads
/// anything, and no way to add an entry at run time — a served catalog is the
/// attack this design exists to remove (decision 0045 section 2).
#[derive(Clone, Copy, Debug)]
pub struct Catalog {
    entries: &'static [CatalogEntry],
    fingerprint: u32,
}

impl Catalog {
    /// The catalog this build was compiled with.
    pub const fn product() -> Self {
        Self {
            entries: generated::ENTRIES,
            fingerprint: generated::CATALOG_FINGERPRINT,
        }
    }

    /// Builds a catalog over borrowed entries, for a test.
    pub const fn from_entries(entries: &'static [CatalogEntry], fingerprint: u32) -> Self {
        Self {
            entries,
            fingerprint,
        }
    }

    /// A digest of every row, so a device can tell one catalog from another.
    ///
    /// Derived from the rows rather than declared beside them, so it cannot be
    /// forgotten in a review and cannot disagree with what it names.
    pub const fn fingerprint(&self) -> u32 {
        self.fingerprint
    }

    /// Every entry, in catalog order.
    pub const fn entries(&self) -> &'static [CatalogEntry] {
        self.entries
    }

    /// The entry with this identity and revision, if there is one.
    pub fn find(&self, id: &AssetId, revision: &AssetRevision) -> Option<&'static CatalogEntry> {
        self.entries
            .iter()
            .find(|entry| entry.id() == id.as_str() && entry.revision() == revision.as_str())
    }

    /// Every entry with this identity, newest revision last.
    pub fn revisions_of(&self, id: &AssetId) -> impl Iterator<Item = &'static CatalogEntry> {
        let needle = id.as_str().to_owned();
        self.entries
            .iter()
            .filter(move |entry| entry.id() == needle)
    }

    /// Every entry of `kind` that has published bytes for `platform`.
    ///
    /// An entry with no variant for the platform is absent from the answer
    /// rather than present and unusable, so a caller cannot accidentally plan
    /// an install of bytes that were never built for the device it is on.
    pub fn published_for(
        &self,
        kind: Kind,
        platform: Platform,
    ) -> impl Iterator<Item = (&'static CatalogEntry, &'static Variant)> {
        self.entries.iter().filter_map(move |entry| {
            if entry.kind() != kind {
                return None;
            }
            entry.variant(platform).map(|variant| (entry, variant))
        })
    }

    /// Every entry that is fetched without being asked for, on `platform`.
    pub fn required_for(
        &self,
        platform: Platform,
    ) -> impl Iterator<Item = (&'static CatalogEntry, &'static Variant)> {
        self.entries.iter().filter_map(move |entry| {
            if entry.necessity() != Necessity::Required {
                return None;
            }
            entry.variant(platform).map(|variant| (entry, variant))
        })
    }
}

#[cfg(test)]
mod tests {
    use super::{
        ArtifactRole, Catalog, CatalogEntry, Container, Kind, ModelFacts, ModelFormat, Necessity,
        Publication, Variant,
    };
    use crate::digest::DIGEST_HEX_CHARS;
    use crate::ids::{AssetId, AssetRevision};
    use crate::platform::Platform;

    /// Rows of both model kinds and one that is not, written here rather than
    /// in the catalog source.
    ///
    /// Decision 0065 refuses to publish a model row that names a format and an
    /// upstream URL neither of which exists, so the compiled product table has
    /// nothing of either model kind and will not until SP-08 records a go.
    /// These are what such a row looks like, and they exist so the rules about
    /// model rows are checked against one instead of only against their
    /// absence.
    static MODEL_ROWS: &[CatalogEntry] = &[
        CatalogEntry::new(
            "test-model",
            "1",
            Kind::ModelWeights,
            Necessity::OnDemand,
            Container::Raw,
            // Deliberately the awkward pair: an adapter, and a format that is
            // not the first member of either enumeration, so a body that
            // returned a hard-coded default would be caught.
            Some(ModelFacts::new(
                ModelFormat::OnnxRuntime,
                ArtifactRole::Adapter,
            )),
            UNPUBLISHED_ANDROID,
        ),
        CatalogEntry::new(
            "test-tokenizer",
            "1",
            Kind::ModelTokenizer,
            Necessity::OnDemand,
            Container::Raw,
            Some(ModelFacts::new(
                ModelFormat::LitertTflite,
                ArtifactRole::Whole,
            )),
            UNPUBLISHED_ANDROID,
        ),
        CatalogEntry::new(
            "test-flags",
            "1",
            Kind::CountryFlags,
            Necessity::OnDemand,
            Container::Zip,
            None,
            UNPUBLISHED_ANDROID,
        ),
    ];

    /// Unpublished bytes, because none of these rows is about bytes: what is
    /// under test is what a row *says*, and an unpublished variant is the shape
    /// that names no path, no length and no digest to be wrong about.
    static UNPUBLISHED_ANDROID: &[Variant] = &[Variant::new(
        Platform::AndroidArm64,
        Publication::Unpublished,
        "",
        0,
        0,
        "",
    )];

    #[test]
    fn every_generated_row_carries_a_parseable_identity() {
        for entry in Catalog::product().entries() {
            assert!(
                AssetId::parse(entry.id()).is_ok(),
                "identity {:?} does not parse",
                entry.id()
            );
            assert!(
                AssetRevision::parse(entry.revision()).is_ok(),
                "revision {:?} does not parse",
                entry.revision()
            );
        }
    }

    #[test]
    fn identity_and_revision_together_are_unique() {
        let mut seen: Vec<(&str, &str)> = Catalog::product()
            .entries()
            .iter()
            .map(|entry| (entry.id(), entry.revision()))
            .collect();
        let count = seen.len();
        seen.sort_unstable();
        seen.dedup();
        assert_eq!(
            seen.len(),
            count,
            "two rows share one identity and revision"
        );
    }

    #[test]
    fn a_published_variant_names_bytes_and_an_unpublished_one_names_none() {
        for entry in Catalog::product().entries() {
            for variant in entry.variants() {
                match variant.publication() {
                    Publication::Published => {
                        assert!(variant.transfer_bytes() > 0, "{}: no length", entry.id());
                        assert!(!variant.path().is_empty(), "{}: no path", entry.id());
                        assert_eq!(
                            variant.digest_hex().len(),
                            DIGEST_HEX_CHARS,
                            "{}: digest is not a digest",
                            entry.id()
                        );
                    }
                    Publication::Unpublished => {
                        assert_eq!(variant.transfer_bytes(), 0, "{}: length", entry.id());
                        assert!(variant.path().is_empty(), "{}: path", entry.id());
                        assert!(variant.digest_hex().is_empty(), "{}: digest", entry.id());
                    }
                }
            }
        }
    }

    #[test]
    fn a_new_row_needs_no_code() {
        // The property this design is for: every question a consumer asks is
        // answered from data, so a row nobody wrote code for still resolves.
        // If this test ever needs a match arm added to keep passing, the
        // catalog has grown a hard-coded consumer and the property is gone.
        for entry in Catalog::product().entries() {
            let id = AssetId::parse(entry.id()).unwrap_or_else(|_| unreachable!());
            let revision =
                AssetRevision::parse(entry.revision()).unwrap_or_else(|_| unreachable!());
            assert!(Catalog::product().find(&id, &revision).is_some());
            for platform in Platform::ALL {
                let listed = Catalog::product()
                    .published_for(entry.kind(), platform)
                    .any(|(found, _)| found.id() == entry.id());
                assert_eq!(listed, entry.variant(platform).is_some());
            }
        }
    }

    #[test]
    fn a_required_asset_is_required_on_every_platform_it_publishes() {
        for entry in Catalog::product().entries() {
            if entry.necessity() != Necessity::Required {
                continue;
            }
            for variant in entry.variants() {
                let platform = variant.platform();
                assert!(
                    Catalog::product()
                        .required_for(platform)
                        .any(|(found, _)| found.id() == entry.id()),
                    "{} is required but absent from {platform}",
                    entry.id()
                );
            }
        }
    }

    #[test]
    fn model_facts_are_carried_by_exactly_the_model_kinds() {
        // The generator refuses a row that disagrees, and this is the same
        // rule read back off the compiled table — the half that still holds if
        // somebody hand-edits the generated file the generator says not to.
        //
        // The product catalog has no model row and is not expected to have one
        // (decision 0065), so on its own this loop only ever compares `false`
        // with `false` and would pass unchanged if `is_model` answered `false`
        // for every kind. `MODEL_ROWS` is the half that would not, and the two
        // are chained rather than split because the invariant is one statement.
        for entry in Catalog::product().entries().iter().chain(MODEL_ROWS.iter()) {
            assert_eq!(
                entry.model().is_some(),
                entry.kind().is_model(),
                "{}: model facts and kind disagree",
                entry.id()
            );
        }
    }

    #[test]
    fn a_model_row_reads_back_what_it_was_written_with() {
        // The positive half of the seam, and the only place the accessors a
        // carrier calls are exercised against a positive model row. The
        // shipping catalog still has no model row, so without this fixture an
        // accessor could return the wrong field and every product-row suite
        // would stay green.
        let catalog = Catalog::from_entries(MODEL_ROWS, 0);

        let weights = catalog
            .find(
                &AssetId::parse("test-model").unwrap_or_else(|_| unreachable!()),
                &AssetRevision::parse("1").unwrap_or_else(|_| unreachable!()),
            )
            .unwrap_or_else(|| unreachable!());
        let facts = weights.model().unwrap_or_else(|| unreachable!());
        assert!(weights.kind().is_model());
        assert_eq!(facts.format(), ModelFormat::OnnxRuntime);
        assert_eq!(facts.role(), ArtifactRole::Adapter);
        // The mapping a resolution port asks in, read through the facts rather
        // than through the role, because that is the call a carrier makes.
        assert!(facts.is_adapter());

        let tokenizer = catalog
            .find(
                &AssetId::parse("test-tokenizer").unwrap_or_else(|_| unreachable!()),
                &AssetRevision::parse("1").unwrap_or_else(|_| unreachable!()),
            )
            .unwrap_or_else(|| unreachable!());
        let facts = tokenizer.model().unwrap_or_else(|| unreachable!());
        assert!(tokenizer.kind().is_model());
        assert_eq!(facts.format(), ModelFormat::LitertTflite);
        assert_eq!(facts.role(), ArtifactRole::Whole);
        assert!(!facts.is_adapter());

        // And the refusing direction, from the same table: a row that is not a
        // model kind carries nothing, so a consumer reading `Some` never has to
        // guard against a flag pack that grew a format.
        let flags = catalog
            .find(
                &AssetId::parse("test-flags").unwrap_or_else(|_| unreachable!()),
                &AssetRevision::parse("1").unwrap_or_else(|_| unreachable!()),
            )
            .unwrap_or_else(|| unreachable!());
        assert!(!flags.kind().is_model());
        assert!(flags.model().is_none());
    }

    #[test]
    fn a_model_name_survives_a_round_trip() {
        // The catalog source writes these names and the generator maps them by
        // table, so a rename on one side alone is a row that stops parsing
        // rather than one that parses differently. Cheap to check, and the
        // check is what makes the two tables one vocabulary.
        for format in ModelFormat::ALL {
            assert_eq!(ModelFormat::parse(format.as_str()), Some(format));
        }
        for role in [ArtifactRole::Whole, ArtifactRole::Adapter] {
            assert_eq!(ArtifactRole::parse(role.as_str()), Some(role));
        }
        assert_eq!(ModelFormat::parse("safetensors"), None);
        assert_eq!(ArtifactRole::parse("lora"), None);
        assert!(!ArtifactRole::Whole.is_adapter());
        assert!(ArtifactRole::Adapter.is_adapter());
    }

    #[test]
    fn the_python_standard_library_is_cataloged_by_kind_not_by_name() {
        let any = Catalog::product()
            .entries()
            .iter()
            .any(|entry| entry.kind() == Kind::PythonStdlib);
        assert!(any, "no row of kind PythonStdlib");
    }
}
