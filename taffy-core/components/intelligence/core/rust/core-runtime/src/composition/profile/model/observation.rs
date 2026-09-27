// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Page-observation adoption for one profile runtime.

use bip_types::identity::TaskId;

use crate::context::{MediaObservation, PageArena};
use crate::{ObservationEffectView, ObservationWireError};

use super::ProfileServiceRuntime;

impl ProfileServiceRuntime {
    /// Decodes one observation into this task's live source set.
    ///
    /// The evidence half is durable and content-free; the arena half stays on
    /// the loop state until the next observation replaces it or this
    /// generation ends.
    ///
    /// # Errors
    ///
    /// A compiled-in label naming the refusal. Six different facts used to
    /// reach the browser as one word, `observation_not_adopted`, and a phone
    /// that had just read the site it was sent to ended on it with nothing to
    /// say which one it was. Every name here is a constant in this file or
    /// [`ObservationWireError::label`]; none of them is a fact about the
    /// page.
    pub fn adopt_observation(
        &mut self,
        task_id: &TaskId,
        action_id: &crate::ActionId,
        generation: u64,
        view: ObservationEffectView<'_>,
        media: Option<MediaObservation>,
    ) -> Result<crate::PageObservationEvidence, &'static str> {
        let mut arena = PageArena::new();
        let evidence = crate::codec::observation_wire::decode_page_observation(
            generation,
            view,
            Some(&mut arena),
        )
        .map_err(ObservationWireError::label)?;
        let source_id = self
            .core
            .task(task_id)
            .map(loop_kernel::ports::TaskEnginePort::pre_model_observation_sources)
            .and_then(|sources| {
                // The tab is the match, and exactly one source may claim it.
                //
                // This used to require the origin to be byte-equal as well,
                // and that was a second, weaker copy of a rule the browser
                // owns: it refuses a read whose live document is not the
                // source's own site, and it is the only layer that can decide
                // that, because deciding it needs the registry-controlled
                // domain table and a sandboxed utility has no way to acquire
                // one. The two copies could therefore only ever disagree in
                // one direction — the browser authorizing a read of a site
                // answering on a sibling host of its own registrable domain,
                // and this line then refusing the evidence as an invalid
                // envelope, which refuses the whole completion and ends the
                // core. A portal moving `myaadhaar` to `myaadhaarbeta` on
                // load did exactly that, and took every task in the profile
                // with it.
                let mut matching = sources
                    .into_iter()
                    .filter(|source| source.tab_id == evidence.tab_id);
                let source = matching.next()?;
                matching.next().is_none().then_some(source.source_id)
            })
            .ok_or("adopt_no_single_source_for_tab")?;
        let proposal = self
            .core
            .task(task_id)
            .and_then(|task| task.action_effect_facts(action_id))
            .map(|facts| facts.proposal)
            .ok_or("adopt_no_proposal_for_action")?;
        let query_filter = match proposal.intent() {
            crate::ActionIntent::Browser(crate::BrowserIntent::DomQuery {
                within,
                role,
                text,
                limit,
                ..
            }) => {
                let text = match text {
                    Some(_) => Some(
                        self.core
                            .transient_dom_query_text(task_id.as_str(), &proposal)
                            .map(str::to_owned)
                            .ok_or("adopt_query_text_not_held")?,
                    ),
                    None => None,
                };
                let limit = limit.map_or(Ok(loop_kernel::context::MAX_ARENA_NODES), |value| {
                    usize::try_from(value).map_err(|_| "adopt_query_limit")
                })?;
                Some(
                    loop_kernel::context::DomQueryFilter::new(
                        within.as_ref().map(|node| node.as_str().to_owned()),
                        *role,
                        text,
                        limit,
                    )
                    .ok_or("adopt_query_filter")?,
                )
            }
            _ => None,
        };
        let page = &mut self.core.loop_state_mut(task_id.as_str()).page;
        page.replace_source_with_media(source_id, &evidence, arena, media)
            .map_err(|_| "adopt_source_replace")?;
        if query_filter
            .as_ref()
            .is_some_and(|filter| !page.stage_dom_query(source_id, filter))
        {
            return Err("adopt_query_stage");
        }
        Ok(evidence)
    }
}
