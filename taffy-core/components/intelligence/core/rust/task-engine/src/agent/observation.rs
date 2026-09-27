// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The deterministic whole-document read that precedes a paid model turn.
//!
//! The page arena is deliberately transient. A newly started task and a task
//! restored after utility-process death therefore have the same local fact:
//! no page bytes exist to send. This module turns that fact into an ordinary
//! proposal. It neither reads a page nor grants authority; policy, dispatch,
//! and the verified outcome use the same reducer path as a model tool call.

use bip_types::identity::{ContentDigest, DigestAlgorithm};
use bip_types::ActionResultCode;

use super::AgentError;
use crate::action::{ActionIntent, ActionProposal, ActionState, BrowserIntent};
use crate::command::Command;
use crate::observation::PageObservationEvidence;
use crate::proposal::hex_digest;
use crate::reducer::Reducer;
use crate::task::{ConsentedSource, FailureReason, TaskTemplateId};
use crate::workflow::{WorkflowDigest, REVIEWED_NO_MODEL_ROUTE_ID, REVIEWED_OBSERVATION_TOOL};
use crate::{Clock, IdSource, IdempotencyKey};

const OBSERVATION_DOMAIN: &[u8] = b"taffy.agent.pre-model-observation.v1";
const OBSERVATION_KEY_PREFIX: &str = "agent-observation-";

/// Whether this proposal is the walk's own whole-document read.
///
/// The key prefix is minted a few lines below and by nothing else, so this is
/// a question about who wrote the proposal rather than about what it asks for.
/// It matters because the two bounds on repeating a call are about different
/// authors. The repetition register counts what the *model* asked for, and its
/// ladder is addressed to a model: told to ask for less, then told to report
/// what it has. The walk cannot ask for less and has nothing to report until
/// this read succeeds, so its own bound is the one that fits —
/// [`MAX_SOURCE_BOOTSTRAP_READS`] attempts at one source (decision 0196).
pub(crate) fn is_agent_observation(proposal: &ActionProposal) -> bool {
    proposal
        .idempotency_key
        .as_str()
        .starts_with(OBSERVATION_KEY_PREFIX)
}

// One accepted seed plus the product's maximum eight-source discovery grant.
// Start admission owns the separate `0..=1` and `1..=8` limits; this is the
// fail-closed ceiling for a restored task whose durable source set has grown.
const MAX_WEB_ERRAND_SOURCES: usize = 9;

/// Independent consented pages that may be read before one paid turn.
///
/// Each read still commits its own proposal, policy decision and outcome.
/// The page arena folds these results in stable source order, regardless
/// of which browser reply arrives first. Mutating tools do not use this cap.
pub const MAX_PARALLEL_SOURCE_READS: usize = 4;

/// Verified whole-document reads one source may produce before the walk stops
/// asking for another.
///
/// The prerequisite this bounds is "a reading of this source is current", and
/// every clause of it is a comparison: a reading that verifies and is still
/// not current asks for a replacement with nothing spent and nothing refused,
/// which is a loop no budget and no refusal ledger can see. Four is generous
/// for the case this exists for — a page that genuinely moves under each read
/// — and small enough that the phone stops rather than spins.
pub const MAX_SOURCE_BOOTSTRAP_READS: u32 = 4;

/// The bound counts every bootstrap read a source has spent, verified or
/// refused, and it is the only thing that ever calls a source unreadable.
/// A refusal used to do that on its own: any terminal state the recovery
/// ladder would not retry made the source `Refused` at once, which the walk
/// answers with `FailTask(SourcesUnavailable)` — one refusal, one dead errand,
/// on a page that was a second away from being readable. See
/// [`Reducer::source_observation_state`].
const _: () = assert!(MAX_SOURCE_BOOTSTRAP_READS > 0);

/// One transient arena and the exact browser evidence it was decoded from.
///
/// This is not an authority receipt. The reducer still requires a durable
/// verified action outcome carrying byte-for-byte equal evidence before the
/// arena may open a paid turn. Keeping the source identity beside the full
/// document tuple prevents a receipt for one page epoch or revision from being
/// paired with newer, unverified bytes from the same tab and origin.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct LiveSourceObservation {
    /// Browser-issued opaque source identity.
    pub source_id: crate::SourceId,
    /// Content-free identity and completeness facts decoded with the arena.
    pub evidence: PageObservationEvidence,
}

/// What the transient-page prerequisite asks the walk to do next.
#[derive(Clone, Debug, PartialEq)]
pub enum PreModelObservation {
    /// A matching observation is both live and durably verified.
    Ready,
    /// The ordinary action path is still deciding or dispatching the read.
    Waiting,
    /// One replayable reducer command must be committed before the model turn.
    Command(Command),
}

impl<C: Clock, I: IdSource> Reducer<C, I> {
    /// Ensures a paid turn has every matching, verified whole-document page.
    ///
    /// `live_observations` is transient and says only which arenas currently
    /// name this task's exact sources. Durable verification is checked again
    /// here from action records; an observation decoded ahead of its terminal
    /// commit therefore waits and can never open the paid-call path alone.
    pub fn next_pre_model_observation(
        &self,
        live_observations: &[LiveSourceObservation],
        digest: &dyn WorkflowDigest,
    ) -> Result<PreModelObservation, AgentError> {
        let snapshot = self.task().snapshot();
        if snapshot.template_id == TaskTemplateId::WebErrand
            && self.task().consented_sources().is_empty()
            && snapshot.source_discovery_enabled
            && (1..=crate::MAX_WEB_ERRAND_NEW_SOURCE_CAP)
                .contains(&snapshot.remaining_new_source_cap)
            && snapshot.discovery_tab_id.is_none()
        {
            // Initial consent has committed, but the browser has not yet
            // committed the exact task-owned tab. There is no page to read
            // and, critically, no tab identity a search proposal may name.
            return Ok(PreModelObservation::Waiting);
        }
        let sources = self.agent_observation_sources()?;
        let mut waiting = 0;
        let mut next_source = None;
        for source in sources {
            match self.source_observation_state(source, live_observations) {
                SourceObservationState::Ready => {}
                SourceObservationState::Waiting => waiting += 1,
                SourceObservationState::Refused => {
                    return Ok(PreModelObservation::Command(Command::FailTask {
                        reason: FailureReason::SourcesUnavailable,
                    }));
                }
                SourceObservationState::Needed => {
                    // Choose by source order, but inspect every source before
                    // proposing: a refusal later in the list must stop the
                    // batch before any further work leaves it.
                    next_source.get_or_insert(source);
                }
            }
        }
        if waiting < MAX_PARALLEL_SOURCE_READS {
            if let Some(source) = next_source {
                return self.propose_source_observation(source, digest);
            }
        }
        Ok(if waiting == 0 {
            PreModelObservation::Ready
        } else {
            PreModelObservation::Waiting
        })
    }

    fn propose_source_observation(
        &self,
        source: &ConsentedSource,
        digest: &dyn WorkflowDigest,
    ) -> Result<PreModelObservation, AgentError> {
        let revision = self.task().revision();
        let intent = ActionIntent::Browser(BrowserIntent::DomRead {
            tab: source.tab_id.clone(),
            target: None,
        });
        let material =
            observation_material(self.task().task_id().as_str(), source, revision, &intent)?;
        let digest_bytes = digest
            .sha256(&material)
            .map_err(|_| AgentError::DigestUnavailable)?;
        let encoded = hex_digest(&digest_bytes);
        let proposal = ActionProposal::new(
            intent,
            None,
            IdempotencyKey::new(format!("{OBSERVATION_KEY_PREFIX}{encoded}")),
            false,
            None,
            ContentDigest {
                algorithm: DigestAlgorithm::Sha256,
                value: encoded,
            },
        );
        // No second bound here. The repetition register used to be asked as
        // well, and it answered first: it abandons a call at
        // `MAX_IDENTICAL_REFUSALS`, which is three, while
        // `MAX_SOURCE_BOOTSTRAP_READS` is four, so the bound decision 0181
        // wrote for this exact case was never once reached. A phone showed
        // what that cost. An errand followed a link to the site it wanted,
        // read it 25 ms after the navigation committed, and the renderer
        // answered that the document had no body yet; the walk asked twice
        // more inside the next 210 ms, the register hit three, and the errand
        // ended saying it could not read enough to answer, on a page that
        // finished parsing a second later. Decision 0196 leaves one bound
        // here, and it is the one in [`source_observation_state`].
        Ok(PreModelObservation::Command(Command::ProposeAction(
            Box::new(proposal),
        )))
    }

    fn source_observation_state(
        &self,
        source: &ConsentedSource,
        live_observations: &[LiveSourceObservation],
    ) -> SourceObservationState {
        let mut pending = false;
        let mut left_the_source = false;
        let mut verified_live = false;
        let mut spent_reads = 0u32;
        for action in self.actions() {
            let proposal = action.proposal();
            // A reading the tab has since moved off is not a reading of the
            // document this bound is about, and it is skipped entirely: not
            // counted, and not able to say the tab left its source. The
            // reducer marks it at the move, because this loop walks a map
            // keyed by identifier and cannot tell which record came first.
            //
            // A reading that opened a paid turn is skipped on the same
            // grounds. This bound says a source cannot be read, and a reading
            // the walk used is the opposite of evidence for that. Counting
            // them made an errand that did four things on one page run out of
            // readings of it (decision 0197).
            let is_source_bootstrap = !action.superseded_by_a_move()
                && !action.reading_was_used()
                && is_agent_observation(proposal)
                && whole_document_read_of(proposal.intent(), source);
            if is_source_bootstrap {
                match action.state() {
                    ActionState::Proposed
                    | ActionState::WaitingApproval
                    | ActionState::Authorized
                    | ActionState::Dispatching
                    | ActionState::Verifying => pending = true,
                    // A refused read is a spent attempt and nothing more.
                    //
                    // It used to be a verdict: any terminal state the recovery
                    // ladder would not retry made the source `Refused`, which
                    // the walk answers with `FailTask(SourcesUnavailable)`. So
                    // one reading killed one errand — measured on a phone,
                    // where a followed link landed on the official site, the
                    // page had not finished becoming observable, the reading
                    // came back `kUnsupported` with no nodes, and a task that
                    // had just arrived exactly where it meant to reported that
                    // it could not read enough to answer (decision 0181).
                    //
                    // The verdict is the bound below, and it has always been
                    // the honest one: this many attempts at one source with no
                    // reading the walk can use. Between here and there every
                    // answer leaves the source `Needed`, so the walk asks
                    // again — which is free when the page was simply not ready
                    // yet, and costs four reads when it never will be.
                    ActionState::Failed
                    | ActionState::Rejected
                    | ActionState::Cancelled
                    | ActionState::OutcomeUnknown => {
                        spent_reads = spent_reads.saturating_add(1);
                        // One of those answers is not about the source at all.
                        // `EgressNotAuthorized` says this task holds no source
                        // for the document the tab is on *now* - it followed a
                        // link that redirected, or typed an address that did
                        // not resolve - and the repair is a move, which is the
                        // model's to choose and is the first thing that
                        // refusal's own sentence names. Calling it `Refused`
                        // ended the whole errand as `SourcesUnavailable` one
                        // proposal after it arrived somewhere new, which is
                        // both untrue and the opposite of recoverable
                        // (decision 0177).
                        if action.result() == Some(ActionResultCode::EgressNotAuthorized) {
                            left_the_source = true;
                        }
                    }
                    ActionState::Verified => {
                        spent_reads = spent_reads.saturating_add(1);
                    }
                }
            }
            // The tab is the match, and the live arena is what ties the bytes
            // to this source. This used to require the evidence's origin to
            // equal the source row's byte for byte as well — the same rule the
            // browser already answered with the registrable-domain table it is
            // the only holder of (decision 0160). With the reducer admitting
            // such an outcome and this clause still refusing it, a read of a
            // tab that had moved within its own site was recorded Verified and
            // then found un-live, so the walk proposed another one, and
            // another: a phone ran five hundred and eleven identical reads of
            // one page between two model turns.
            if action.state() == ActionState::Verified
                && whole_document_read_of(proposal.intent(), source)
                && action.observation().is_some_and(|observation| {
                    observation.tab_id == source.tab_id
                        && live_observations.iter().any(|live| {
                            live.source_id == source.source_id && live.evidence == *observation
                        })
                })
            {
                verified_live = true;
            }
        }

        // And a bound, because the loop above is the shape this prerequisite
        // fails in. Every gate here is about one reading being current, and
        // none of them counts; a reading that verifies and is still not the
        // one this source needs asks for another, forever, with no refusal to
        // record and no budget to spend. A source that has produced this many
        // verified whole-document reads and still has none the walk can use is
        // a source this task cannot read, which is what `Refused` says.
        if !verified_live && spent_reads >= MAX_SOURCE_BOOTSTRAP_READS {
            return SourceObservationState::Refused;
        }

        // A decoded graph arrives before its RecordActionOutcome command is
        // committed. Pending wins over an older verified read so that a
        // restored task cannot use new bytes under an old receipt.
        //
        // And a current reading wins over a later refusal. `Refused` here ends
        // the whole task as `SourcesUnavailable`, which is a claim about the
        // source and not about one attempt, so a source the walk already holds
        // a live verified reading of may not make it. An errand that typed an
        // address that did not resolve met exactly that: its tab committed
        // Chromium's error document, the next whole-document read of the
        // source was refused because a document with no site is not an
        // admitted source, and a task that had read four pages perfectly well
        // ended saying it could not read enough to answer (decision 0177).
        // The bound above is what still catches a source that verifies and is
        // never usable; it reads `!verified_live` for the same reason.
        if pending {
            SourceObservationState::Waiting
        } else if verified_live {
            SourceObservationState::Ready
        } else if left_the_source {
            // Nothing for the walk to propose and nothing wrong with the
            // source: the tab is somewhere this task has no source for, and
            // the model is handed that refusal to answer.
            SourceObservationState::Ready
        } else {
            SourceObservationState::Needed
        }
    }

    fn agent_observation_sources(&self) -> Result<&[ConsentedSource], AgentError> {
        let snapshot = self.task().snapshot();
        let sources = self.task().consented_sources();
        let count_is_valid = match snapshot.template_id {
            TaskTemplateId::BuildSourceTable => sources.len() == 1,
            TaskTemplateId::CompareProducts => sources.len() >= 2,
            TaskTemplateId::SummarizeEvidence => !sources.is_empty(),
            TaskTemplateId::WebErrand => sources.len() <= MAX_WEB_ERRAND_SOURCES,
        };
        if !count_is_valid {
            return Err(AgentError::InvalidObservationSource);
        }
        let exact_scope: Vec<_> = sources.iter().map(|source| source.source_id).collect();
        let has_duplicate_tab = sources.iter().enumerate().any(|(index, source)| {
            sources.get(..index).is_some_and(|previous_sources| {
                previous_sources
                    .iter()
                    .any(|previous| previous.tab_id == source.tab_id)
            })
        });
        let discovery_shape_is_valid = match snapshot.template_id {
            TaskTemplateId::WebErrand => {
                snapshot.source_discovery_enabled
                    && snapshot.remaining_new_source_cap <= crate::MAX_WEB_ERRAND_NEW_SOURCE_CAP
                    && (!sources.is_empty()
                        || (snapshot.remaining_new_source_cap > 0
                            && snapshot.discovery_tab_id.is_some()))
            }
            TaskTemplateId::BuildSourceTable
            | TaskTemplateId::CompareProducts
            | TaskTemplateId::SummarizeEvidence => {
                !snapshot.source_discovery_enabled
                    && snapshot.remaining_new_source_cap == 0
                    && snapshot.discovery_tab_id.is_none()
            }
        };
        if !discovery_shape_is_valid
            || self.task().scope().included() != exact_scope
            || has_duplicate_tab
            || snapshot
                .provider_route
                .as_ref()
                .is_none_or(|route| route.as_str() == REVIEWED_NO_MODEL_ROUTE_ID)
            || !snapshot
                .tool_allowlist
                .iter()
                .any(|name| name == REVIEWED_OBSERVATION_TOOL)
        {
            return Err(AgentError::InvalidObservationSource);
        }
        Ok(sources)
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum SourceObservationState {
    Ready,
    Waiting,
    Refused,
    Needed,
}

fn whole_document_read_of(intent: &ActionIntent, source: &ConsentedSource) -> bool {
    match intent {
        ActionIntent::Browser(
            BrowserIntent::DomRead { tab, target: None } | BrowserIntent::DomQuery { tab, .. },
        ) => tab == &source.tab_id,
        _ => false,
    }
}

/// Whether this move lands the tab it names on a different document.
///
/// The same set `discovered_source_matches_action` names, for the same reason:
/// these are the operations after which the tab is somewhere else, so every
/// reading before one of them was a reading of a page that is no longer there.
/// The tab is the proposal's own — `ActionProposal::tab_id` already answers
/// `LinkOpen` with the tab its target is in — so this says only which
/// operations move one.
///
/// [`MAX_SOURCE_BOOTSTRAP_READS`] is a bound on readings of *this* document,
/// and it was counting readings of this *tab*. An errand stays in one tab and
/// walks it from a search to a result to the site, so its fourth arrival found
/// the counter already at the bound and was refused before a single reading of
/// it was attempted — on a phone, one commit after `browser.link.open`
/// verified, as `FailTask(SourcesUnavailable)` with every move before it
/// verified too (decision 0181).
pub(crate) const fn lands_the_tab_somewhere_new(intent: &ActionIntent) -> bool {
    let ActionIntent::Browser(intent) = intent else {
        return false;
    };
    matches!(
        intent,
        BrowserIntent::Navigate { new_tab: false, .. }
            | BrowserIntent::Search { .. }
            | BrowserIntent::HistoryBack { .. }
            | BrowserIntent::HistoryForward { .. }
            | BrowserIntent::Reload { .. }
            | BrowserIntent::FormSubmit { .. }
            | BrowserIntent::LinkOpen { .. }
    )
}

fn observation_material(
    task_id: &str,
    source: &ConsentedSource,
    revision: u64,
    intent: &ActionIntent,
) -> Result<Vec<u8>, AgentError> {
    let mut material = OBSERVATION_DOMAIN.to_vec();
    let source_id = source.source_id.to_text();
    for value in [
        task_id,
        source_id.as_str(),
        source.tab_id.as_str(),
        source.normalized_origin.as_str(),
    ] {
        let length =
            u32::try_from(value.len()).map_err(|_| AgentError::ProposalEncodingOverflow)?;
        material.extend_from_slice(&length.to_le_bytes());
        material.extend_from_slice(value.as_bytes());
    }
    material.extend_from_slice(&revision.to_le_bytes());
    let intent = intent
        .encode_canonical()
        .map_err(|_| AgentError::ProposalEncodingOverflow)?;
    let length = u64::try_from(intent.len()).map_err(|_| AgentError::ProposalEncodingOverflow)?;
    material.extend_from_slice(&length.to_le_bytes());
    material.extend_from_slice(&intent);
    Ok(material)
}
