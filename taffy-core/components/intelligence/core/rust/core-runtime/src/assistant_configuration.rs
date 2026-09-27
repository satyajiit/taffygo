// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Durable configuration of the one assistant.
//!
//! Ability rows are a closed filter over compiled-in tools and task templates.
//! They never grant authority: this module can only remove a registry row from
//! an already reviewed task set. Response style is a separate closed value and
//! has no method that can inspect or change tools, policy, or permissions.

use core_service_types as wire;
use loop_kernel::turn::ModelResponseStyle;
use task_engine::{EffectiveToolSet, Milestone, REGISTRY};

const ABILITY_COUNT: u32 = 16;
const ALL_ABILITIES: u16 = u16::MAX;
const PAGES_LOOKUP: u16 = 1 << 0;
const PAGES_COMPARE: u16 = 1 << 1;
const PAGES_SUMMARIZE: u16 = 1 << 2;
const PAGES_TABLE: u16 = 1 << 3;
const PRODUCTS: u16 = 1 << 4;
const OFFERS: u16 = 1 << 5;
const FORM: u16 = 1 << 6;
const DOWNLOADS: u16 = 1 << 7;
const PDF: u16 = 1 << 8;
const SHEET: u16 = 1 << 9;
const DOCUMENT: u16 = 1 << 10;
const DEPTH: u16 = 1 << 11;
const TRIP: u16 = 1 << 12;
const PICTURES: u16 = 1 << 13;
const VIDEO: u16 = 1 << 14;
const KEEP: u16 = 1 << 15;

/// Why a bootstrap or mutation could not become a configuration.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AssistantConfigurationError {
    ZeroDurableRevision,
    TooManyAbilities,
    AbilitiesOutOfOrder,
    InvalidScale,
    StaleRevision,
    RevisionOverflow,
    AbilityDisabled,
}

/// Complete profile configuration for the one assistant.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AssistantConfiguration {
    revision: u64,
    disabled: u16,
    preset: wire::PersonalityPreset,
    pace: u32,
    length: u32,
    check_in: u32,
}

impl Default for AssistantConfiguration {
    fn default() -> Self {
        Self {
            revision: 0,
            disabled: 0,
            preset: wire::PersonalityPreset::CarefulResearcher,
            pace: 0,
            length: 1,
            check_in: 0,
        }
    }
}

impl AssistantConfiguration {
    /// Whether the mapped legacy ability remains enabled in this profile.
    pub(crate) const fn ability_is_enabled(&self, ability: wire::AssistantAbility) -> bool {
        self.disabled & (1u16 << ability as u32) == 0
    }

    /// Restores the optional durable row. Absence is the compiled first-run
    /// default; a present row must have a positive revision.
    pub fn restore(
        value: Option<wire::AssistantConfiguration>,
    ) -> Result<Self, AssistantConfigurationError> {
        let Some(value) = value else {
            return Ok(Self::default());
        };
        if value.revision == 0 {
            return Err(AssistantConfigurationError::ZeroDurableRevision);
        }
        let disabled = disabled_mask(&value.disabled_abilities)?;
        validate_scales(value.pace, value.length, value.check_in)?;
        Ok(Self {
            revision: value.revision,
            disabled,
            preset: value.preset,
            pace: value.pace,
            length: value.length,
            check_in: value.check_in,
        })
    }

    /// Validates a whole-record compare-and-set without mutating this value.
    pub fn prepare_update(
        &self,
        command: &wire::SetAssistantConfigurationCommand,
    ) -> Result<Self, AssistantConfigurationError> {
        if command.expected_revision != self.revision {
            return Err(AssistantConfigurationError::StaleRevision);
        }
        let revision = self
            .revision
            .checked_add(1)
            .ok_or(AssistantConfigurationError::RevisionOverflow)?;
        let disabled = disabled_mask(&command.disabled_abilities)?;
        validate_scales(command.pace, command.length, command.check_in)?;
        Ok(Self {
            revision,
            disabled,
            preset: command.preset,
            pace: command.pace,
            length: command.length,
            check_in: command.check_in,
        })
    }

    pub const fn revision(&self) -> u64 {
        self.revision
    }

    /// Installs a configuration only after the browser confirms its durable
    /// compare-and-set. The pending value was already validated at submission.
    pub fn install(&mut self, committed: Self) {
        *self = committed;
    }

    /// Complete generated persistence body. Revisions live on the enclosing
    /// storage effect, beside every other compare-and-set operation.
    pub fn persist_body(&self) -> wire::AssistantConfigurationPersistEffect {
        wire::AssistantConfigurationPersistEffect {
            disabled_abilities: abilities_from_mask(self.disabled),
            preset: self.preset,
            pace: self.pace,
            length: self.length,
            check_in: self.check_in,
        }
    }

    /// Complete Core API projection, with no UI-owned defaults.
    pub fn view(&self) -> core_api_types::AssistantConfigurationView {
        core_api_types::AssistantConfigurationView {
            revision: self.revision,
            disabled_abilities: abilities_from_mask(self.disabled)
                .into_iter()
                .map(api_ability)
                .collect(),
            preset: match self.preset {
                wire::PersonalityPreset::CarefulResearcher => {
                    core_api_types::PersonalityPresetView::CarefulResearcher
                }
                wire::PersonalityPreset::QuickShopper => {
                    core_api_types::PersonalityPresetView::QuickShopper
                }
                wire::PersonalityPreset::TripPlanner => {
                    core_api_types::PersonalityPresetView::TripPlanner
                }
            },
            pace: self.pace,
            length: self.length,
            check_in: self.check_in,
        }
    }

    /// Closed response-style value. It contains no authority-bearing field and
    /// exposes no tool operation.
    pub fn response_style(&self) -> ModelResponseStyle {
        ModelResponseStyle::new(
            match self.preset {
                wire::PersonalityPreset::CarefulResearcher => 0,
                wire::PersonalityPreset::QuickShopper => 1,
                wire::PersonalityPreset::TripPlanner => 2,
            },
            self.pace,
            self.length,
            self.check_in,
        )
        .unwrap_or_default()
    }

    /// Rejects a start whose template belongs to a disabled ability, then
    /// narrows its reviewed tool set. The resulting list is never empty because
    /// the help/escape rows are deliberately configuration-independent.
    pub fn prepare_start(
        &self,
        template: wire::TaskTemplateId,
        milestone: Milestone,
        allowlist: &[String],
    ) -> Result<Vec<String>, AssistantConfigurationError> {
        if self.disabled & template_abilities(template) != 0 {
            return Err(AssistantConfigurationError::AbilityDisabled);
        }
        let configured = self.effective_tool_allowlist();
        Ok(EffectiveToolSet::for_task(milestone, allowlist)
            .narrow_by(&configured)
            .names()
            .into_iter()
            .map(str::to_owned)
            .collect())
    }

    /// Whether a model-authored name still belongs to an enabled ability at
    /// the instant it would be adopted. This second check closes the race where
    /// a turn was sent immediately before a person disabled its ability.
    pub fn admits_tool_call(&self, tool_name: &str) -> bool {
        REGISTRY
            .iter()
            .find(|entry| entry.canonical_name(tool_name).is_some())
            .is_some_and(|entry| {
                let abilities = tool_abilities(entry.name);
                abilities == 0 || abilities & !self.disabled != 0
            })
    }

    /// The allowlist identities the person's configuration still admits, in
    /// registration order and each once.
    ///
    /// Spelled in the names `EffectiveToolSet::narrow_by` compares — a tab
    /// row's shared `browser.tabs`, a store's group — and never the exact
    /// registry row. An exact row name admits nothing there, so a list of them
    /// dropped every grouped row from every configured set: the four tab rows
    /// from each task, and the store rows from a start that attached one, which
    /// is why decision 0133's acceptance condition could not have held.
    pub(crate) fn effective_tool_allowlist(&self) -> Vec<String> {
        let mut names: Vec<String> = Vec::new();
        for entry in REGISTRY.iter().filter(|entry| {
            let abilities = tool_abilities(entry.name);
            abilities == 0 || abilities & !self.disabled != 0
        }) {
            let name = task_engine::tool::allowlist_name(entry.name);
            if !names.iter().any(|held| held == name) {
                names.push(name.to_owned());
            }
        }
        names
    }
}

fn validate_scales(
    pace: u32,
    length: u32,
    check_in: u32,
) -> Result<(), AssistantConfigurationError> {
    let Ok(maximum) = u32::try_from(wire::MAX_PERSONALITY_SCALE) else {
        return Err(AssistantConfigurationError::InvalidScale);
    };
    if pace > maximum || length > maximum || check_in > maximum {
        return Err(AssistantConfigurationError::InvalidScale);
    }
    Ok(())
}

fn disabled_mask(abilities: &[wire::AssistantAbility]) -> Result<u16, AssistantConfigurationError> {
    if abilities.len() > wire::MAX_ASSISTANT_ABILITIES {
        return Err(AssistantConfigurationError::TooManyAbilities);
    }
    let mut mask = 0u16;
    let mut previous = None;
    for ability in abilities {
        let wire = *ability as u32;
        if wire >= ABILITY_COUNT || previous.is_some_and(|value| wire <= value) {
            return Err(AssistantConfigurationError::AbilitiesOutOfOrder);
        }
        mask |= 1u16 << wire;
        previous = Some(wire);
    }
    Ok(mask)
}

fn abilities_from_mask(mask: u16) -> Vec<wire::AssistantAbility> {
    (0..ABILITY_COUNT)
        .filter(|wire| mask & (1u16 << wire) != 0)
        .filter_map(wire::AssistantAbility::from_wire)
        .collect()
}

pub(crate) const fn api_ability(
    value: wire::AssistantAbility,
) -> core_api_types::AssistantAbilityView {
    match value {
        wire::AssistantAbility::PagesLookup => core_api_types::AssistantAbilityView::PagesLookup,
        wire::AssistantAbility::PagesCompare => core_api_types::AssistantAbilityView::PagesCompare,
        wire::AssistantAbility::PagesSummarize => {
            core_api_types::AssistantAbilityView::PagesSummarize
        }
        wire::AssistantAbility::PagesTable => core_api_types::AssistantAbilityView::PagesTable,
        wire::AssistantAbility::Products => core_api_types::AssistantAbilityView::Products,
        wire::AssistantAbility::Offers => core_api_types::AssistantAbilityView::Offers,
        wire::AssistantAbility::Form => core_api_types::AssistantAbilityView::Form,
        wire::AssistantAbility::Downloads => core_api_types::AssistantAbilityView::Downloads,
        wire::AssistantAbility::Pdf => core_api_types::AssistantAbilityView::Pdf,
        wire::AssistantAbility::Sheet => core_api_types::AssistantAbilityView::Sheet,
        wire::AssistantAbility::Document => core_api_types::AssistantAbilityView::Document,
        wire::AssistantAbility::Depth => core_api_types::AssistantAbilityView::Depth,
        wire::AssistantAbility::Trip => core_api_types::AssistantAbilityView::Trip,
        wire::AssistantAbility::Pictures => core_api_types::AssistantAbilityView::Pictures,
        wire::AssistantAbility::Video => core_api_types::AssistantAbilityView::Video,
        wire::AssistantAbility::Keep => core_api_types::AssistantAbilityView::Keep,
    }
}

const fn template_abilities(template: wire::TaskTemplateId) -> u16 {
    match template {
        wire::TaskTemplateId::CompareProducts => PAGES_COMPARE | PRODUCTS,
        wire::TaskTemplateId::SummarizeEvidence => PAGES_SUMMARIZE,
        wire::TaskTemplateId::BuildSourceTable => PAGES_TABLE,
        wire::TaskTemplateId::WebErrand => FORM,
    }
}

/// Primary ability ownership of one canonical registry row.
///
/// A row has one owner so turning off any of the sixteen compiled abilities
/// has a concrete effect on the model's vocabulary. Zero is reserved for the
/// help/escape rows: they stay available but carry no page or file authority.
fn tool_abilities(name: &str) -> u16 {
    match name {
        "browser.search"
        | "browser.back"
        | "browser.forward"
        | "browser.reload"
        | "browser.stop_loading"
        | "browser.dom.query"
        | "browser.dom.scroll"
        | "history.search"
        | "history.recent"
        | "bookmarks.search"
        | "bookmarks.list" => PAGES_LOOKUP,
        "browser.tabs.open"
        | "browser.tabs.list"
        | "browser.tabs.activate"
        | "browser.tabs.close"
        | "open_tabs.list" => PAGES_COMPARE,
        "browser.dom.read" => PAGES_SUMMARIZE,
        "artifact.csv.create" | "core.table.reshape" => PAGES_TABLE,
        "browser.navigate" => PRODUCTS,
        "browser.link.open" => OFFERS,
        "browser.form.inspect"
        | "browser.dom.click"
        | "browser.dom.focus"
        | "browser.form.fill"
        | "browser.form.select"
        | "browser.form.toggle"
        | "browser.form.submit"
        | "user.request_values" => FORM,
        "browser.download.start"
        | "browser.download.from_link"
        | "browser.download.list"
        | "browser.download.cancel" => DOWNLOADS,
        "page.images" | "page.screenshot.inspect" => PICTURES,
        "page.video.inspect"
        | "media.probe"
        | "media.audio.extract"
        | "media.frames.sample"
        | "media.transcode" => VIDEO,
        "page.pdf.inspect" | "artifact.pdf.create" => PDF,
        "artifact.xlsx.create" => SHEET,
        "artifact.markdown.create" | "artifact.docx.create" | "artifact.pptx.create" => DOCUMENT,
        "python.execute" | "run.spawn" => DEPTH,
        "browser.selection.read" => TRIP,
        "library.search" | "library.save" | "library.remove" | "memory.search" | "memory.save"
        | "memory.update" | "memory.delete" => KEEP,
        "user.handover" | "user.ask" | "tool.search" | "tool.activate" => 0,
        _ => ALL_ABILITIES,
    }
}

const fn decode_milestone(value: wire::TaskMilestone) -> Milestone {
    match value {
        wire::TaskMilestone::M0 => Milestone::M0,
        wire::TaskMilestone::M1 => Milestone::M1,
        wire::TaskMilestone::M2 => Milestone::M2,
        wire::TaskMilestone::M3 => Milestone::M3,
        wire::TaskMilestone::M4 => Milestone::M4,
        wire::TaskMilestone::M5 => Milestone::M5,
        wire::TaskMilestone::M6 => Milestone::M6,
        wire::TaskMilestone::M7 => Milestone::M7,
        wire::TaskMilestone::M8 => Milestone::M8,
    }
}

/// Applies template and tool admission directly to the generated start body.
pub fn prepare_configured_start(
    configuration: &AssistantConfiguration,
    start: &mut wire::StartTaskCommand,
) -> Result<(), AssistantConfigurationError> {
    start.tool_allowlist = configuration.prepare_start(
        start.template_id,
        decode_milestone(start.milestone),
        &start.tool_allowlist,
    )?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_shipping_registry_row_has_closed_ability_ownership() {
        for entry in task_engine::available_at(Milestone::M8) {
            let abilities = tool_abilities(entry.name);
            assert!(
                abilities != ALL_ABILITIES,
                "shipping tool {} needs an explicit ability owner",
                entry.name
            );
            assert!(
                abilities.count_ones() <= 1,
                "shipping tool {} has more than one primary ability owner",
                entry.name
            );
        }
    }

    #[test]
    fn every_compiled_ability_removes_a_shipping_tool_when_disabled() {
        let enabled = EffectiveToolSet::for_task(Milestone::M8, &[]).names();
        for ability_wire in 0..ABILITY_COUNT {
            let ability = wire::AssistantAbility::from_wire(ability_wire).unwrap();
            let configured = AssistantConfiguration::restore(Some(wire::AssistantConfiguration {
                revision: 1,
                disabled_abilities: vec![ability],
                preset: wire::PersonalityPreset::CarefulResearcher,
                pace: 0,
                length: 1,
                check_in: 0,
            }))
            .unwrap();
            let configured_names = configured.effective_tool_allowlist();
            let narrowed = EffectiveToolSet::for_task(Milestone::M8, &[])
                .narrow_by(&configured_names)
                .names();
            assert!(
                enabled.iter().any(|name| !narrowed.contains(name)),
                "compiled ability wire {ability_wire} removes no shipping tool"
            );
        }
    }

    /// The configured list is spelled in allowlist identities, so an untouched
    /// configuration narrows no reviewed set — with the tab group, and with
    /// the three store groups a start may attach (decision 0133). Spelled in
    /// exact row names, as it was, it removed every grouped row from every task.
    #[test]
    fn an_untouched_configuration_narrows_no_reviewed_set() {
        use task_engine::tool::{
            BOOKMARKS_ALLOWLIST_GROUP, HISTORY_ALLOWLIST_GROUP, OPEN_TABS_ALLOWLIST_GROUP,
            TABS_ALLOWLIST_GROUP,
        };
        let configured = AssistantConfiguration::default().effective_tool_allowlist();
        assert!(configured.iter().any(|name| name == TABS_ALLOWLIST_GROUP));
        assert!(!configured.iter().any(|name| name == "browser.tabs.open"));
        assert_eq!(
            configured
                .iter()
                .filter(|name| *name == HISTORY_ALLOWLIST_GROUP)
                .count(),
            1
        );

        let errand: Vec<String> = [
            TABS_ALLOWLIST_GROUP,
            "browser.search",
            "browser.navigate",
            "browser.dom.read",
            "page.images",
            "browser.download.start",
            "user.handover",
            "tool.search",
        ]
        .iter()
        .map(|name| (*name).to_owned())
        .collect();
        let mut with_stores = errand.clone();
        with_stores.extend(
            [
                HISTORY_ALLOWLIST_GROUP,
                BOOKMARKS_ALLOWLIST_GROUP,
                OPEN_TABS_ALLOWLIST_GROUP,
            ]
            .map(str::to_owned),
        );
        for allowlist in [Vec::new(), errand, with_stores.clone()] {
            let reviewed = EffectiveToolSet::for_task(Milestone::M8, &allowlist);
            assert_eq!(
                reviewed.narrow_by(&configured).names(),
                reviewed.names(),
                "{allowlist:?}"
            );
        }
        let attached = EffectiveToolSet::for_task(Milestone::M8, &with_stores)
            .narrow_by(&configured)
            .names();
        for row in [
            "browser.tabs.open",
            "history.search",
            "bookmarks.list",
            "open_tabs.list",
        ] {
            assert!(
                attached.contains(&row),
                "{row} fell out of the configured set"
            );
        }
    }

    #[test]
    fn disabled_form_removes_tools_and_its_template() {
        let configured = AssistantConfiguration::restore(Some(wire::AssistantConfiguration {
            revision: 1,
            disabled_abilities: vec![wire::AssistantAbility::Form],
            preset: wire::PersonalityPreset::CarefulResearcher,
            pace: 0,
            length: 1,
            check_in: 0,
        }))
        .unwrap();
        assert!(!configured.admits_tool_call("browser.form.fill"));
        assert!(configured.admits_tool_call("browser.dom.read"));
        assert_eq!(
            configured.prepare_start(wire::TaskTemplateId::WebErrand, Milestone::M8, &[]),
            Err(AssistantConfigurationError::AbilityDisabled)
        );
    }

    #[test]
    fn disabled_page_lookup_removes_dom_query_but_not_document_read() {
        let configured = AssistantConfiguration::restore(Some(wire::AssistantConfiguration {
            revision: 1,
            disabled_abilities: vec![wire::AssistantAbility::PagesLookup],
            preset: wire::PersonalityPreset::CarefulResearcher,
            pace: 0,
            length: 1,
            check_in: 0,
        }))
        .unwrap();
        assert!(!configured.admits_tool_call("browser.dom.query"));
        assert!(configured.admits_tool_call("browser.dom.read"));
    }

    #[test]
    fn mutation_is_sorted_bounded_and_compare_and_set() {
        let configuration = AssistantConfiguration::default();
        let valid = wire::SetAssistantConfigurationCommand {
            expected_revision: 0,
            disabled_abilities: vec![
                wire::AssistantAbility::Downloads,
                wire::AssistantAbility::Video,
            ],
            preset: wire::PersonalityPreset::TripPlanner,
            pace: 1,
            length: 2,
            check_in: 1,
        };
        let updated = configuration.prepare_update(&valid).unwrap();
        assert_eq!(updated.revision(), 1);
        assert!(!updated.admits_tool_call("browser.download.start"));
        let mut invalid = valid;
        invalid.disabled_abilities.reverse();
        assert_eq!(
            configuration.prepare_update(&invalid),
            Err(AssistantConfigurationError::AbilitiesOutOfOrder)
        );
    }
}
