// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The observed discovery phase before the reviewed public starting page.

use policy_engine::origin::{normalize_serialization, NormalizedOrigin};
use procedure_engine::{ArgumentDescriptor, RecordedValue, StepDescriptor};
use task_engine::action::{ActionIntent, BrowserIntent};
use task_engine::{ActionState, ConsentedSource};

use crate::ports::ActionEffectFacts;

/// This phase retains identities only, never search terms or page content.
#[derive(Clone, Debug, Default)]
pub(super) struct Prelude {
    pending: Vec<String>,
    seen: Vec<String>,
}

impl Prelude {
    pub(super) fn dispatched(&mut self, facts: &ActionEffectFacts) -> bool {
        let allowed = matches!(
            facts.proposal.intent(),
            ActionIntent::Browser(
                BrowserIntent::Search { .. }
                    | BrowserIntent::DomRead { .. }
                    | BrowserIntent::DomQuery { .. }
                    | BrowserIntent::Navigate { new_tab: false, .. }
                    | BrowserIntent::LinkOpen { .. }
            )
        );
        if !allowed
            || self.seen.len() >= super::MAX_RECORDED_STEPS
            || self.seen.contains(&facts.action_id)
        {
            return false;
        }
        self.pending.push(facts.action_id.clone());
        self.seen.push(facts.action_id.clone());
        true
    }

    pub(super) fn settled(
        &mut self,
        facts: &ActionEffectFacts,
        accepted: Option<&ConsentedSource>,
        discovered: Option<&ConsentedSource>,
    ) -> Result<Option<(NormalizedOrigin, StepDescriptor)>, ()> {
        if facts.state != ActionState::Verified {
            return Err(());
        }
        let index = self
            .pending
            .iter()
            .position(|identity| identity == &facts.action_id)
            .ok_or(())?;
        self.pending.remove(index);
        let Some(source) = accepted.filter(|source| &source.tab_id == facts.proposal.tab_id())
        else {
            return Ok(None);
        };
        let address = match facts.proposal.intent() {
            ActionIntent::Browser(BrowserIntent::Navigate {
                address,
                new_tab: false,
                ..
            }) => Some(address.as_str()),
            ActionIntent::Browser(BrowserIntent::LinkOpen { .. }) if discovered == Some(source) => {
                source.canonical_locator.as_deref()
            }
            _ => None,
        };
        let Some(address) = address else {
            return Ok(None);
        };
        let origin = normalize_serialization(&source.normalized_origin).map_err(|_| ())?;
        if !procedure_engine::recording::public_address_is_valid(address, &origin) {
            return Ok(None);
        }
        if !self.pending.is_empty() {
            return Err(());
        }
        let step = StepDescriptor::new(
            "browser.navigate",
            bip_types::action::PostconditionKind::CommittedNavigation,
        )
        .taking(vec![ArgumentDescriptor::new(
            0,
            RecordedValue::PublicAddress(address.to_owned()),
        )]);
        Ok(Some((origin, step)))
    }
}
