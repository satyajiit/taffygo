// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One catalog row and the vocabulary it is written in.
//!
//! Every field is a constant of the build, so an entry is `&'static` and copies
//! nothing. The four enumerations here are closed and fail closed on an unknown
//! name, which is what makes a catalog that no longer matches its generator a
//! build failure rather than an asset that silently stops being fetched.
//!
//! Adding a member to any of them is a small, ordinary change — a name, an arm,
//! and whatever consumes it. Adding a *row* that uses existing members is no
//! change at all, which is the split the design is for.

use crate::digest::Digest;
use crate::platform::Platform;

/// What an asset is for, and therefore which part of the product consumes it.
///
/// The kind is how a consumer finds an asset, so a new artifact of an existing
/// kind is invisible to code. A genuinely new kind of thing — a filter list
/// where there were only runtimes — is one member and one consumer.
///
/// It is also how a person is told what an asset is. A row carries no name of
/// its own, because a name is copy and copy belongs in the string catalog:
/// a surface renders the kind's catalogued string, and the row's identity and
/// revision beside it as the data they are. That is what makes a new row cost
/// no string, no translation and no screen change.
#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub enum Kind {
    /// The Python standard library a sandboxed interpreter imports from.
    PythonStdlib,
    /// An allowlisted Python package set the interpreter may import.
    PythonPackages,
    /// Weights for an on-device model.
    ModelWeights,
    /// A tokenizer belonging to an on-device model.
    ModelTokenizer,
    /// A content-blocking rule set.
    FilterList,
    /// The country flag artwork a picker draws.
    ///
    /// Named for what it is rather than for the shape it arrives in. Every
    /// other kind here is too, and a second pack of pictures would get its own
    /// name for the same reason: a person is never shown "image pack", and a
    /// catalog row that describes its container tells the product nothing it
    /// could act on.
    CountryFlags,
    /// The start page's own painted plates, one for each part of the day.
    ///
    /// A second pack of pictures, named the way the paragraph above says it
    /// would be: what the artwork is for, not that it is artwork. The start
    /// page draws one of these behind its wordmark, chosen from the device's
    /// own clock; the plate compiled into the installer stands in until this
    /// has arrived.
    StartScenes,
}

impl Kind {
    /// Every kind, in declaration order.
    pub const ALL: [Self; 7] = [
        Self::PythonStdlib,
        Self::PythonPackages,
        Self::ModelWeights,
        Self::ModelTokenizer,
        Self::FilterList,
        Self::CountryFlags,
        Self::StartScenes,
    ];

    /// The name the catalog source and the generated table use.
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::PythonStdlib => "python-stdlib",
            Self::PythonPackages => "python-packages",
            Self::ModelWeights => "model-weights",
            Self::ModelTokenizer => "model-tokenizer",
            Self::FilterList => "filter-list",
            Self::CountryFlags => "country-flags",
            Self::StartScenes => "start-scenes",
        }
    }

    /// Parses a catalog name, or `None` when it names no kind.
    pub fn parse(text: &str) -> Option<Self> {
        Self::ALL.into_iter().find(|it| it.as_str() == text)
    }

    /// Whether a row of this kind describes part of an on-device model.
    ///
    /// Exactly the kinds that must carry [`ModelFacts`]. The generator refuses
    /// a row that disagrees in either direction, so this predicate and that
    /// rule are one statement made in two languages. Written as an exhaustive
    /// match rather than as a `matches!` over two names, so an eighth kind is a
    /// compile error here and whoever adds it has to say which side of the line
    /// it falls on instead of inheriting `false`.
    pub const fn is_model(self) -> bool {
        match self {
            Self::ModelWeights | Self::ModelTokenizer => true,
            Self::PythonStdlib
            | Self::PythonPackages
            | Self::FilterList
            | Self::CountryFlags
            | Self::StartScenes => false,
        }
    }
}

/// Whether an asset is fetched without being asked for.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Necessity {
    /// Fetched when the profile starts, subject to the network policy.
    Required,
    /// Fetched when something asks for it, or when a person says so.
    OnDemand,
}

impl Necessity {
    /// The name the catalog source and the generated table use.
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Required => "required",
            Self::OnDemand => "on-demand",
        }
    }

    /// Parses a catalog name, or `None`.
    pub fn parse(text: &str) -> Option<Self> {
        match text {
            "required" => Some(Self::Required),
            "on-demand" => Some(Self::OnDemand),
            _ => None,
        }
    }
}

/// What the transferred bytes are, and therefore what installing them means.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Container {
    /// The bytes are the artifact. Installing is a rename.
    Raw,
    /// The bytes are a zip archive the consumer reads without unpacking.
    ///
    /// Nothing extracts one. A zip that stays a zip is one file to verify, one
    /// file to open read-only, and one descriptor to hand a sandboxed worker —
    /// where an unpacked tree is thousands of files the sandbox cannot reach
    /// anyway.
    Zip,
}

impl Container {
    /// The name the catalog source and the generated table use.
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Raw => "raw",
            Self::Zip => "zip",
        }
    }

    /// Parses a catalog name, or `None`.
    pub fn parse(text: &str) -> Option<Self> {
        match text {
            "raw" => Some(Self::Raw),
            "zip" => Some(Self::Zip),
            _ => None,
        }
    }
}

/// Whether a variant's bytes exist at the delivery origin yet.
///
/// A variant is specified before it is built. Saying so is what lets the
/// product name an artifact it cannot yet fetch without inventing a digest for
/// it, which is the one thing a pinned catalog must never contain.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Publication {
    /// Bytes exist, and the row names their length, path and digest.
    Published,
    /// Bytes do not exist yet. The row names no length, path or digest, and
    /// the plane refuses to fetch it.
    Unpublished,
}

impl Publication {
    /// The name the catalog source and the generated table use.
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Published => "published",
            Self::Unpublished => "unpublished",
        }
    }

    /// Parses a catalog name, or `None`.
    pub fn parse(text: &str) -> Option<Self> {
        match text {
            "published" => Some(Self::Published),
            "unpublished" => Some(Self::Unpublished),
            _ => None,
        }
    }
}

/// The format an on-device runtime must load a model artifact as.
///
/// This is what the artifact *is*, where [`Kind`] is what it is *for*. Nothing
/// derives it and nothing may: reading a magic number would make the catalog
/// agree with whatever bytes happen to be on a disk rather than with what was
/// published, and a register that guessed would be worth less than no register
/// (decision `docs/decisions/0060-no-on-device-model-runtime-is-selected.md`).
///
/// The members and their names are the core-service contract's
/// `ToolModelArtifactKind`, lowercased and hyphenated. Spelling them a second
/// way would turn decision 0101's direct carrier mapping into a translation
/// with a table to keep in step, and a translation table between two closed
/// enumerations is where a fourth member gets added to one side only.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ModelFormat {
    /// A LiteRT/TensorFlow Lite flatbuffer.
    LitertTflite,
    /// An ONNX Runtime graph.
    OnnxRuntime,
    /// A GGUF container.
    Gguf,
}

impl ModelFormat {
    /// Every format, in declaration order.
    pub const ALL: [Self; 3] = [Self::LitertTflite, Self::OnnxRuntime, Self::Gguf];

    /// The name the catalog source and the generated table use.
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::LitertTflite => "litert-tflite",
            Self::OnnxRuntime => "onnx-runtime",
            Self::Gguf => "gguf",
        }
    }

    /// Parses a catalog name, or `None`.
    pub fn parse(text: &str) -> Option<Self> {
        Self::ALL.into_iter().find(|it| it.as_str() == text)
    }
}

/// Whether a model artifact is a model or a modification of one.
///
/// A closed pair rather than a `bool`, for the reason [`Necessity`] and
/// [`Container`] are: the source says `whole` or `adapter` in words, so a row
/// states which it is when it is read aloud. The resolution port a worker is
/// bound through asks in a `bool`, and [`ArtifactRole::is_adapter`] is the
/// whole of that mapping.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ArtifactRole {
    /// The artifact is the model.
    Whole,
    /// The artifact modifies a model delivered under its own row.
    Adapter,
}

impl ArtifactRole {
    /// The name the catalog source and the generated table use.
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Whole => "whole",
            Self::Adapter => "adapter",
        }
    }

    /// Parses a catalog name, or `None`.
    pub fn parse(text: &str) -> Option<Self> {
        match text {
            "whole" => Some(Self::Whole),
            "adapter" => Some(Self::Adapter),
            _ => None,
        }
    }

    /// Whether this is an adapter, in the shape a resolution asks in.
    pub const fn is_adapter(self) -> bool {
        match self {
            Self::Whole => false,
            Self::Adapter => true,
        }
    }
}

/// What a model artifact is, beyond its identity, revision and bytes.
///
/// Decision 0060 names four facts a sandboxed worker needs about a model
/// artifact: the byte length, the digest, the format, and whether it is an
/// adapter. Two of them belong to every row already — [`Variant`] carries a
/// length and a digest whatever the row is for — so these are the other two.
/// They exist on model rows only, because on any other row they would be two
/// fields with no true value, and a defaulted field is a claim.
///
/// `core-runtime`'s production delivery adapter consumes these facts into
/// decision 0101's bounded complete Core State snapshot; the browser then
/// opens and hashes the exact installed revision before registering it. The
/// product catalog deliberately carries no model row until a runtime and a
/// provenance-complete artifact have been selected, so synthetic catalog rows
/// remain the positive proof of this shape.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ModelFacts {
    format: ModelFormat,
    role: ArtifactRole,
}

impl ModelFacts {
    /// Builds the facts. The generator is the only caller that matters.
    pub const fn new(format: ModelFormat, role: ArtifactRole) -> Self {
        Self { format, role }
    }

    /// What a runtime must load the artifact as.
    pub const fn format(self) -> ModelFormat {
        self.format
    }

    /// Whether the artifact is a model or a modification of one.
    pub const fn role(self) -> ArtifactRole {
        self.role
    }

    /// Whether this is an adapter, in the shape a resolution asks in.
    pub const fn is_adapter(self) -> bool {
        self.role.is_adapter()
    }
}

/// One platform's bytes for one asset.
#[derive(Clone, Copy, Debug)]
pub struct Variant {
    platform: Platform,
    publication: Publication,
    path: &'static str,
    transfer_bytes: u64,
    installed_bytes: u64,
    digest_hex: &'static str,
}

impl Variant {
    /// Builds a variant. The generator is the only caller that matters.
    pub const fn new(
        platform: Platform,
        publication: Publication,
        path: &'static str,
        transfer_bytes: u64,
        installed_bytes: u64,
        digest_hex: &'static str,
    ) -> Self {
        Self {
            platform,
            publication,
            path,
            transfer_bytes,
            installed_bytes,
            digest_hex,
        }
    }

    /// The platform these bytes were built for.
    pub const fn platform(&self) -> Platform {
        self.platform
    }

    /// Whether the bytes exist at the origin.
    pub const fn publication(&self) -> Publication {
        self.publication
    }

    /// The origin-relative path, empty when unpublished.
    pub const fn path(&self) -> &'static str {
        self.path
    }

    /// Bytes on the wire, zero when unpublished.
    pub const fn transfer_bytes(&self) -> u64 {
        self.transfer_bytes
    }

    /// Bytes on disk once installed, zero when unpublished.
    pub const fn installed_bytes(&self) -> u64 {
        self.installed_bytes
    }

    /// The digest as the catalog wrote it, empty when unpublished.
    pub const fn digest_hex(&self) -> &'static str {
        self.digest_hex
    }

    /// The digest, or `None` when unpublished or unparseable.
    pub fn digest(&self) -> Option<Digest> {
        Digest::parse_hex(self.digest_hex)
    }

    /// Whether the plane may ask for these bytes.
    pub fn is_fetchable(&self) -> bool {
        matches!(self.publication, Publication::Published)
            && self.transfer_bytes > 0
            && !self.path.is_empty()
            && self.digest().is_some()
    }
}

/// One asset: an identity, a revision, and its per-platform bytes.
#[derive(Clone, Copy, Debug)]
pub struct CatalogEntry {
    id: &'static str,
    revision: &'static str,
    kind: Kind,
    necessity: Necessity,
    container: Container,
    model: Option<ModelFacts>,
    variants: &'static [Variant],
}

impl CatalogEntry {
    /// Builds an entry. The generator is the only caller that matters.
    pub const fn new(
        id: &'static str,
        revision: &'static str,
        kind: Kind,
        necessity: Necessity,
        container: Container,
        model: Option<ModelFacts>,
        variants: &'static [Variant],
    ) -> Self {
        Self {
            id,
            revision,
            kind,
            necessity,
            container,
            model,
            variants,
        }
    }

    /// The stable identity.
    pub const fn id(&self) -> &'static str {
        self.id
    }

    /// The published revision.
    pub const fn revision(&self) -> &'static str {
        self.revision
    }

    /// What the asset is for.
    pub const fn kind(&self) -> Kind {
        self.kind
    }

    /// Whether it is fetched without being asked for.
    pub const fn necessity(&self) -> Necessity {
        self.necessity
    }

    /// What the transferred bytes are.
    pub const fn container(&self) -> Container {
        self.container
    }

    /// What the artifact is, when this row is a model row.
    ///
    /// `None` on every other kind, and checked in both directions: a model row
    /// without facts and a flag pack that grew them are both refused by the
    /// generator, so a consumer reading `Some` already knows the kind agrees
    /// and never has to guard against a row that carries one without the
    /// other.
    pub const fn model(&self) -> Option<ModelFacts> {
        self.model
    }

    /// Every platform's bytes.
    pub const fn variants(&self) -> &'static [Variant] {
        self.variants
    }

    /// This platform's bytes, if the asset publishes any.
    pub fn variant(&self, platform: Platform) -> Option<&'static Variant> {
        self.variants.iter().find(|it| it.platform == platform)
    }
}
