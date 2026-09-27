// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What a task freezes when it is created, and what it shows before it starts.
//!
//! Domain model section 10.1: updating the assistant's configuration or a
//! global setting does not rewrite an in-flight task. [`TaskSnapshot`] is that
//! frozen configuration; [`ScopePreview`] is the same material shaped for the
//! consent surface, which is why the two live together and change together.

use bip_types::identity::{SkillVersionId, TabId};

use super::scope::SourceScope;
use crate::authority::PolicyVersion;
use crate::budget::TaskBudgets;
use crate::records::{SourceId, WorkspaceId};
use crate::route::ProviderRouteId;
use crate::tool::Milestone;

/// Maximum byte length of one opaque browser-process session identity.
pub const MAX_BROWSER_SESSION_ID_BYTES: usize = 128;
/// Maximum new tuple sources one accepted Web errand may discover.
pub const MAX_WEB_ERRAND_NEW_SOURCE_CAP: u32 = 8;
/// Maximum pages in one approved Library refresh. Kept equal to the durable
/// workspace source bound; neither a task nor a status projection can widen it.
pub const MAX_LIBRARY_REFRESH_SOURCES: usize = 64;
/// Browser canonical-address bound shared with the Core Service contract.
pub const MAX_LIBRARY_REFRESH_LOCATOR_BYTES: usize = 4_096;

/// One page frozen into an approved Library refresh task.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct LibraryRefreshSource {
    pub source_id: SourceId,
    pub title: String,
    pub host: String,
    pub canonical_locator: String,
    pub original_content_digest: [u8; 32],
}

/// Exact preview identity and manifest retained across process recovery.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct LibraryRefreshContext {
    pub preview_id: String,
    pub library_revision: u64,
    pub collection_id: WorkspaceId,
    pub source_workspace_revision: u64,
    pub sources: Vec<LibraryRefreshSource>,
}

impl LibraryRefreshContext {
    pub fn is_well_formed(&self) -> bool {
        self.preview_id.len() == 64
            && self.preview_id.bytes().all(|byte| byte.is_ascii_hexdigit())
            && self.library_revision > 0
            && self.source_workspace_revision > 0
            && !self.sources.is_empty()
            && self.sources.len() <= MAX_LIBRARY_REFRESH_SOURCES
            && self
                .sources
                .windows(2)
                .all(|pair| matches!(pair, [left, right] if left.source_id < right.source_id))
            && self.sources.iter().all(|source| {
                !source.title.is_empty()
                    && !source.host.is_empty()
                    && !source.canonical_locator.is_empty()
                    && source.canonical_locator.len() <= MAX_LIBRARY_REFRESH_LOCATOR_BYTES
                    && !source.canonical_locator.contains(['?', '#', '@'])
                    && source
                        .canonical_locator
                        .strip_prefix("https://")
                        .or_else(|| source.canonical_locator.strip_prefix("http://"))
                        .is_some_and(|rest| locator_has_host(rest, &source.host))
            })
    }
}

fn locator_has_host(rest: &str, host: &str) -> bool {
    let authority = rest.split('/').next().unwrap_or_default();
    authority == host
        || authority.strip_prefix(host).is_some_and(|suffix| {
            suffix.strip_prefix(':').is_some_and(|port| {
                !port.is_empty() && port.bytes().all(|byte| byte.is_ascii_digit())
            })
        })
}

/// Opaque browser-process lifetime used to bind session-local tab identities.
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct BrowserSessionId(String);

impl BrowserSessionId {
    /// Accepts one non-empty bounded identity minted by the browser process.
    pub fn new(value: impl Into<String>) -> Result<Self, BrowserSessionIdError> {
        let value = value.into();
        if value.is_empty() || value.len() > MAX_BROWSER_SESSION_ID_BYTES {
            return Err(BrowserSessionIdError::InvalidLength);
        }
        Ok(Self(value))
    }

    /// The opaque value, for equality and transport only.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

/// Why a browser-session identity was refused.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BrowserSessionIdError {
    /// Empty or larger than the closed protocol bound.
    InvalidLength,
}

/// The reviewed product workflow selected when a task is created.
///
/// This is deliberately a closed domain value rather than an arbitrary UI
/// label. Platforms may render it differently, but a persisted task can only
/// name one workflow the portable reducer understands.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum TaskTemplateId {
    /// Compare evidence about several products or choices.
    CompareProducts,
    /// Summarize evidence with its supporting sources.
    SummarizeEvidence,
    /// Build a source-by-source evidence table.
    BuildSourceTable,
    /// Carry out an errand on a site, which may have to be found first
    /// (decision 0087).
    ///
    /// Unlike the three research templates, this one names no plan: an errand
    /// is walked a step at a time from what the page turns out to say, so
    /// [`crate::template::plan_for`] answers nothing for it deliberately.
    WebErrand,
}

impl TaskTemplateId {
    /// Every reviewed template, in stable product order.
    pub const ALL: &'static [Self] = &[
        Self::CompareProducts,
        Self::SummarizeEvidence,
        Self::BuildSourceTable,
        Self::WebErrand,
    ];

    /// The portable contract label shared by every platform UI.
    pub const fn label(self) -> &'static str {
        match self {
            Self::CompareProducts => "compare_products",
            Self::SummarizeEvidence => "summarize_evidence",
            Self::BuildSourceTable => "build_a_source_table",
            Self::WebErrand => "web_errand",
        }
    }

    /// Whether this template's destination may be discovered rather than named
    /// on the consent surface (decision 0087 section 2).
    ///
    /// This says only that the start shape admits a task with no source; every
    /// origin the task actually reaches is still granted per-origin at proposal
    /// time, which is invariant I-03 and is not weakened by any answer here.
    pub const fn admits_discovered_destination(self) -> bool {
        match self {
            Self::CompareProducts | Self::SummarizeEvidence | Self::BuildSourceTable => false,
            Self::WebErrand => true,
        }
    }
}

/// Stable identity of one compiled built-in skill, in product catalogue order.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum BuiltinSkillId {
    GeneralWebResearch,
    DeepResearch,
    ProductComparison,
    MultiTabComparison,
    WebsiteSummarizer,
    PdfAnalysis,
    DataExtraction,
    FormAssistant,
    Shopping,
    DownloadOrganizer,
    TravelResearch,
    VideoTranscriptAnalyzer,
    ImageUnderstanding,
    LibraryBuilder,
    SpreadsheetBuilder,
    DocumentGenerator,
}

impl BuiltinSkillId {
    /// Every compiled identity in the stable product catalogue order.
    pub const ALL: &'static [Self] = &[
        Self::GeneralWebResearch,
        Self::DeepResearch,
        Self::ProductComparison,
        Self::MultiTabComparison,
        Self::WebsiteSummarizer,
        Self::PdfAnalysis,
        Self::DataExtraction,
        Self::FormAssistant,
        Self::Shopping,
        Self::DownloadOrganizer,
        Self::TravelResearch,
        Self::VideoTranscriptAnalyzer,
        Self::ImageUnderstanding,
        Self::LibraryBuilder,
        Self::SpreadsheetBuilder,
        Self::DocumentGenerator,
    ];
}

/// Exact immutable compiled built-in definition bound to one durable task.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct BuiltinSkillReference {
    pub skill_id: BuiltinSkillId,
    pub version: u32,
}

/// The configuration a task froze when it was created (domain model section
/// 10.1).
///
/// Updating the assistant's configuration or a global setting does not rewrite
/// an in-flight task, so this is captured once and never mutated.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TaskSnapshot {
    /// The reviewed workflow selected by the person.
    pub template_id: TaskTemplateId,
    /// The assistant configuration version the task ran under.
    pub assistant_config_version: u32,
    /// The skill version attached, when one was.
    pub skill_version_id: Option<SkillVersionId>,
    /// The compiled built-in definition attached, when one was. Mutually
    /// exclusive with `skill_version_id`; both are absent for an ordinary task.
    pub builtin_skill: Option<BuiltinSkillReference>,
    /// The internal tool names the task may propose, before any per-action
    /// check.
    pub tool_allowlist: Vec<String>,
    /// The capability policy bundle version the decisions were made under.
    pub capability_policy_version: PolicyVersion,
    /// The provider route the task disclosed.
    pub provider_route: Option<ProviderRouteId>,
    /// Exact browser-resolved source identities and tuple origins accepted on
    /// the initial consent surface. This is durable task scope, not a URL
    /// loader or permission to read a changed document.
    pub consented_sources: Vec<ConsentedSource>,
    /// Whether discovery beyond the accepted source set was enabled.
    pub source_discovery_enabled: bool,
    /// How many browser-issued sources may still be admitted through verified
    /// discovery outcomes. This starts at the visible preview cap and is
    /// decremented atomically with each new source binding.
    pub remaining_new_source_cap: u32,
    /// Task-owned blank/search tab prepared for a zero-source Web errand.
    ///
    /// This is a navigation context, not a source and not page authority. It
    /// becomes readable or actionable only after a verified browser outcome
    /// contributes an exact [`ConsentedSource`] binding.
    pub discovery_tab_id: Option<TabId>,
    /// Approved deterministic Library revisit, absent for ordinary tasks.
    pub library_refresh: Option<LibraryRefreshContext>,
    /// Browser-process session whose BIP tab identities were consented to.
    pub browser_session_id: BrowserSessionId,
    /// The milestone whose tool surface the task resolves names against.
    pub milestone: Milestone,
}

/// The scope and provider preview a person consents to before a task starts.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ScopePreview {
    /// The sources the task proposes to read.
    pub scope: SourceScope,
    /// Exact browser-resolved sources represented by `scope`.
    pub sources: Vec<ConsentedSource>,
    /// Whether discovery was visible and enabled.
    pub source_discovery_enabled: bool,
    /// Maximum browser-issued sources that may be added after start.
    pub new_source_cap: u32,
    /// The route the task proposes to send data to.
    pub provider_route: Option<ProviderRouteId>,
    /// The budgets the task proposes to run under.
    pub budgets: TaskBudgets,
}

/// One browser-resolved source frozen into the accepted task preview.
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub struct ConsentedSource {
    /// Stable opaque source identity used by task/workspace records.
    pub source_id: SourceId,
    /// Live tab identity at consent time. Every later use is revalidated.
    pub tab_id: TabId,
    /// Exact normalized tuple origin at consent time.
    pub normalized_origin: String,
    /// Safe browser-owned last committed page locator, when one exists.
    pub canonical_locator: Option<String>,
}
