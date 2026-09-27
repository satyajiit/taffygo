// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Admission of sources discovered by completed browser actions.

use bip_types::identity::ActionId;

use super::Reducer;
use crate::action::{ActionIntent, ActionOutcome, ActionProposal, BrowserIntent};
use crate::budget::BudgetKind;
use crate::ids::IdSource;
use crate::records::SourceId;
use crate::task::{ConsentedSource, TaskTemplateId};
use crate::time::Clock;
use crate::transition::RefusalReason;

#[derive(Clone, Debug, PartialEq, Eq)]
pub(super) enum DiscoveredSourceChange {
    None,
    AlreadyBound,
    Admit {
        source: ConsentedSource,
        replaced_source_id: Option<SourceId>,
    },
}

impl<C: Clock, I: IdSource> Reducer<C, I> {
    /// Plans source admission before the action record is mutated, so binding
    /// the page and spending its discovery budget remain one transition.
    pub(super) fn discovered_source_change(
        &self,
        action_id: &ActionId,
        outcome: &ActionOutcome,
    ) -> Result<DiscoveredSourceChange, RefusalReason> {
        let Some(source) = outcome.discovered_source.as_ref() else {
            return Ok(DiscoveredSourceChange::None);
        };
        let Some(action) = self.actions.get(action_id.as_str()) else {
            return Err(RefusalReason::UnknownAction);
        };
        if outcome.code != bip_types::ActionResultCode::Verified
            || outcome.observation.is_some()
            || self.task.snapshot.template_id != TaskTemplateId::WebErrand
            || !self.task.snapshot.source_discovery_enabled
            || !valid_discovered_source(source)
            || !discovered_source_matches_action(
                action.proposal(),
                source,
                &self.task.consented_sources,
                self.task.snapshot.discovery_tab_id.as_ref(),
                self.task.snapshot.library_refresh.is_some(),
            )
            || self.task.scope.included()
                != self
                    .task
                    .consented_sources
                    .iter()
                    .map(|held| held.source_id)
                    .collect::<Vec<_>>()
        {
            return Err(RefusalReason::ActionOutcomeMismatch);
        }
        if let Some(existing) = self
            .task
            .consented_sources
            .iter()
            .find(|held| held.source_id == source.source_id)
        {
            return if existing == source {
                Ok(DiscoveredSourceChange::AlreadyBound)
            } else {
                Err(RefusalReason::ActionOutcomeMismatch)
            };
        }
        if self.task.scope.excluded().contains(&source.source_id)
            || self.task.snapshot.remaining_new_source_cap == 0
            || self
                .task
                .ledger
                .admit(
                    BudgetKind::MaxSources,
                    1,
                    &self.task.budgets,
                    &self.defaults,
                )
                .is_err()
        {
            return Err(RefusalReason::BudgetExhausted);
        }
        let replaced = self
            .task
            .consented_sources
            .iter()
            .find(|held| held.tab_id == source.tab_id)
            .map(|held| held.source_id);
        if replaced.is_some_and(|source_id| {
            self.task.consented_sources.iter().any(|held| {
                held.source_id == source_id && held.normalized_origin == source.normalized_origin
            })
        }) {
            return Err(RefusalReason::ActionOutcomeMismatch);
        }
        Ok(DiscoveredSourceChange::Admit {
            source: source.clone(),
            replaced_source_id: replaced,
        })
    }
}

fn valid_discovered_source(source: &ConsentedSource) -> bool {
    const MAX_ORIGIN_BYTES: usize = 2_048;
    let tab = source.tab_id.as_str();
    let origin = source.normalized_origin.as_str();
    let authority = origin
        .strip_prefix("https://")
        .or_else(|| origin.strip_prefix("http://"));
    !tab.is_empty()
        && tab.len() <= crate::MAX_OBSERVATION_IDENTIFIER_BYTES
        && tab
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'.' | b'-' | b'_'))
        && !origin.is_empty()
        && origin.len() <= MAX_ORIGIN_BYTES
        && origin.bytes().all(|byte| byte.is_ascii_graphic())
        && authority.is_some_and(|value| {
            !value.is_empty()
                && !value.contains(['/', '?', '#', '@'])
                && value.bytes().all(|byte| byte.is_ascii())
        })
        && source.canonical_locator.as_deref().is_none_or(|locator| {
            !locator.is_empty()
                && locator.len() <= crate::MAX_LIBRARY_REFRESH_LOCATOR_BYTES
                && !locator.contains(['?', '#', '@'])
                && locator
                    .strip_prefix("https://")
                    .or_else(|| locator.strip_prefix("http://"))
                    .is_some()
        })
}

/// A browser-issued source is credible only for the operation that produced
/// it and the exact tab authority that operation held.
///
/// This match is the whole of the rule, and it is closed on purpose. It used
/// to sit under a second, coarser gate on the action's class — `OpenLink |
/// CreateTaskTab | SubmitForm` — and the two lists did not say the same thing:
/// a reload, a back and a forward each land their own tab and each are offered
/// a source by the browser (`TaskActionOperationSettlesItsOwnTab` in
/// `//taffy/browser/core_task_action_operations.cc`, and `decode_discovered_source`
/// in `//taffy/services/core/service_bridge_task_effect/action_terminal.rs`), and
/// all three carry `ActionClass::ControlTab`, which that gate refused. The
/// refusal is `ActionOutcomeMismatch`, which ends the core's publication and
/// with it every task in the profile: a phone reloaded a page it had already
/// been moved off by a press, and the errand died on the answer. Decision 0163
/// is the record. One statement of the set, named by the operation rather than
/// by a class that groups it with something else.
fn discovered_source_matches_action(
    proposal: &ActionProposal,
    source: &ConsentedSource,
    held_sources: &[ConsentedSource],
    discovery_tab_id: Option<&bip_types::identity::TabId>,
    is_library_refresh: bool,
) -> bool {
    let ActionIntent::Browser(intent) = proposal.intent() else {
        return false;
    };
    if held_sources.is_empty() {
        // From zero sources the first site comes from the prepared discovery
        // tab, by the search the tab was opened for or by a typed navigate to
        // a site the model already knows (decision 0106 section 2). A library
        // refresh's navigate additionally names the exact locator it refreshed.
        return match intent {
            BrowserIntent::Search { tab, .. } => {
                discovery_tab_id == Some(tab) && source.tab_id == *tab
            }
            BrowserIntent::Navigate {
                tab,
                address,
                new_tab: false,
            } => {
                discovery_tab_id == Some(tab)
                    && source.tab_id == *tab
                    && (!is_library_refresh
                        || source.canonical_locator.as_deref() == Some(address.as_str()))
            }
            _ => false,
        };
    }

    // The task's own blank tab counts here beside the tabs it holds sources
    // for. It is not a source — nothing was consented to and nothing is bound
    // — but it is a tab this task owns, and the move that lands it on a site
    // is exactly the move that turns it into one. Without this clause an
    // errand that also holds a page refuses its own first discovery with
    // `ActionOutcomeMismatch`, which ends the core's publication and every
    // task in the profile with it — the same shape decision 0163 records
    // (decision 0224).
    let proposal_tab_is_bound = held_sources
        .iter()
        .any(|held| held.tab_id == *proposal.tab_id())
        || discovery_tab_id == Some(proposal.tab_id());
    match intent {
        BrowserIntent::Navigate {
            tab,
            address,
            new_tab: false,
        } => {
            proposal_tab_is_bound
                && source.tab_id == *tab
                && (!is_library_refresh
                    || source.canonical_locator.as_deref() == Some(address.as_str()))
        }
        BrowserIntent::Search { tab, .. }
        | BrowserIntent::HistoryBack { tab }
        | BrowserIntent::HistoryForward { tab }
        | BrowserIntent::Reload { tab }
        | BrowserIntent::FormSubmit { tab, .. } => proposal_tab_is_bound && source.tab_id == *tab,
        BrowserIntent::LinkOpen { target } => {
            proposal_tab_is_bound && source.tab_id == *target.tab_id()
        }
        BrowserIntent::TabsOpen { context, .. } => {
            proposal_tab_is_bound && source.tab_id != *context
        }
        // Stopping a load lands nowhere: the tab keeps whatever document it
        // already had, so there is no destination the browser could state.
        // The browser does not offer a source on one and the terminal decoder
        // refuses one, so this arm is what makes the three sets identical.
        BrowserIntent::StopLoading { .. }
        | BrowserIntent::Navigate { new_tab: true, .. }
        | BrowserIntent::TabsList { .. }
        | BrowserIntent::TabsActivate { .. }
        | BrowserIntent::TabsClose { .. }
        | BrowserIntent::DomQuery { .. }
        | BrowserIntent::DomRead { .. }
        | BrowserIntent::DomClick { .. }
        | BrowserIntent::DomFocus { .. }
        | BrowserIntent::DomScroll { .. }
        | BrowserIntent::FormInspect { .. }
        | BrowserIntent::FormFill { .. }
        | BrowserIntent::FormSelect { .. }
        | BrowserIntent::FormToggle { .. }
        | BrowserIntent::DownloadStart { .. }
        | BrowserIntent::DownloadFromLink { .. }
        | BrowserIntent::DownloadList { .. }
        | BrowserIntent::DownloadCancel { .. }
        | BrowserIntent::SelectionRead { .. }
        | BrowserIntent::ImageDescribe { .. }
        | BrowserIntent::ImageReadText { .. }
        | BrowserIntent::VideoInspect { .. }
        | BrowserIntent::PdfInspect { .. }
        | BrowserIntent::PageScreenshotInspect { .. } => false,
    }
}
