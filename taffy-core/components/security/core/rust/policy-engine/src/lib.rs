// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The only component that may grant authority for a side effect.
//!
//! Granting and spending are different powers and this crate holds exactly the
//! first (decision 0033). The narrowed claim is the first line rather than the
//! ninth because the pre-0033 sentence — "the only authority for side effects"
//! — reads as if nothing downstream decides anything, and the browser-process
//! gate this crate's grants are spent through is precisely something
//! downstream that refuses.
//!
//! Authoritative specifications:
//! `docs/architecture/browser-intelligence-protocol.md` sections 9, 11, and 12;
//! `docs/architecture/domain-model.md` sections 12, 19, and 22;
//! `docs/security/threat-model.md`; `docs/security/data-and-privacy.md`
//! sections 7 and 14. Owning milestone: M2 (page intelligence).
//!
//! # The one rule
//!
//! The task engine proposes. This crate decides. It is the only component that
//! may **grant** authority; a gate downstream of the grant — the browser
//! process holds one, in `//taffy/browser/action_authority.*` —
//! spends and refuses what this crate minted and may never widen it (decision
//! 0033). Nothing in here can be persuaded by a page, a model, a skill, or a
//! tool label: every decision function takes typed values and consults records
//! this crate owns.
//!
//! # What lives here
//!
//! | Module | Decides |
//! |---|---|
//! | [`action_class`] | Which classes of effect a milestone has authorized |
//! | [`phase`] | Whether a request stages an effect or causes it, and what each half is worth |
//! | [`field_allowance`] | Which classes of field Taffy may ever write into |
//! | [`risk`] | How consequential one action turned out to be, once context is counted |
//! | [`site`] | Which destinations the assistant does not go to on its own |
//! | [`lease`] | Whether the assistant is the actor in a tab, and when it stops being |
//! | [`approval`] | What a person was asked, what they answered, and what makes the answer stop applying |
//! | [`capability`] | Whether one exact action is authorized, once, until an expiry |
//! | [`prepared`] | What was staged, what a person was shown, and what a commit may spend |
//! | [`origin`] | Whether two origins are the same one, and where a navigation may land |
//! | [`precondition`] | Whether the world at dispatch time is still the world that was authorized |
//! | [`step`] | The ten ordered steps of the stale-node algorithm |
//! | [`report`] | What the browser reports back at each effect step |
//! | [`sequence`] | The whole stale-node algorithm as one pure state machine |
//! | [`dispatch`] | The dispatch path as a type state, where dispatch after revoke does not compile |
//! | [`denial`] | Why a proposal was refused, and the release's action surface as a table |
//! | [`redaction`] | What a destination may be told |
//! | [`pipeline`] | That the four destinations really are strictly ordered |
//! | [`engine`] | The moments a task runtime has to ask about |
//!
//! # Properties this crate holds
//!
//! - **Deny by default.** The authorized set is an allowlist per milestone. A
//!   class this crate does not know about is refused, and so is a class it
//!   knows about but no milestone has enabled.
//! - **Lease plus capability.** Authority is never issued without a standing
//!   lease, never outlives that lease, and is spent once.
//! - **Take over is synchronous, and racing it does not compile.** When
//!   [`engine::PolicyEngine::user_took_over`] returns, undispatched authority is
//!   gone. Nothing waits for a page, a model, or a network. A
//!   [`dispatch::DispatchTicket`] holds the broker for exactly as long as
//!   another dispatch could still be started from it, so the window in which
//!   "dispatch after revoke" could be written is one the borrow checker
//!   rejects.
//! - **One answer, one action.** An approval is bound to the exact question it
//!   answered and spent once. A changed action, phase, target, document,
//!   origin, destination, data class, or risk invalidates it.
//! - **A consequential effect is prepared, shown, and only then committed.**
//!   Preparation mints nothing, and a commit spends exactly what was prepared
//!   and never more. What it is compared against is a digest over the effect
//!   *as the trusted surface rendered it*, because what a person approved is
//!   what they saw.
//! - **Sensitivity only rises.** Classification is a join over every signal.
//!   A page that declares a password field not sensitive raises it anyway.
//! - **Each destination is strictly narrower.** The local observation, the
//!   model projection, the audit record, and the telemetry record carry
//!   progressively fewer page-derived fields, and the last carries none.
//! - **Unknown values fail closed.** An unrecognized control type, an
//!   unrecognized `autocomplete` token, a precondition without its operand, and
//!   an origin that will not normalize all refuse rather than defaulting.
//! - **No panics, no clocks, no randomness.** Time and identifiers arrive
//!   through [`time::MonotonicClock`] and [`time::IdSource`]; every function is
//!   total.
#![doc(html_no_source)]
#![cfg_attr(
    test,
    allow(
        clippy::unwrap_used,
        clippy::expect_used,
        clippy::panic,
        clippy::indexing_slicing
    )
)]

pub mod action_class;
pub mod action_operation;
pub mod approval;
pub mod capability;
pub mod denial;
pub mod dispatch;
pub mod engine;
pub mod field_allowance;
pub mod grant;
pub mod lease;
pub mod origin;
pub mod phase;
pub mod pipeline;
pub mod precondition;
pub mod prepared;
pub mod redaction;
pub mod report;
pub mod risk;
pub mod sequence;
pub mod site;
pub mod step;
pub mod time;

pub use crate::action_class::{ActionClass, ClassAvailability, PolicyMilestone};
pub use crate::action_operation::ActionOperationKind;
pub use crate::approval::{
    Approval, ApprovalBinding, ApprovalDecision, ApprovalId, ApprovalInvalidation, ApprovalRequest,
    RepeatScope, UserGestureReceipt, MAX_APPROVALS_PER_SESSION,
};
pub use crate::capability::{
    Capability, CapabilityId, CapabilityRequest, CapabilityScope, CapabilityState, PolicyVersion,
    MAX_CAPABILITIES_PER_SESSION,
};
pub use crate::denial::{Denial, DenialReason};
pub use crate::dispatch::{
    DispatchDecision, DispatchTicket, Dispatched, DispatchedAction, Journalled, JournalledIntent,
    Observation, SequenceOutcome, SettledDispatch,
};
pub use crate::engine::{DispatchProposal, PolicyEngine, ProposalDecision, TakeOver};
pub use crate::field_allowance::{
    fill_verdict, per_use_confirmation_required, FieldClassAllowance, FillClearance, FillRefusal,
    FillVerdict,
};
pub use crate::grant::{
    ActorLeaseFact, ApprovalFact, AuthoritySubject, AuthoritySubjectIdError, DirectUserIntentId,
    GrantDecision, GrantIdempotencyKey, GrantIdempotencyKeyError, GrantPolicy, GrantRequest,
    MintedGrant, PolicyEvaluationContext, TaskDiscoveryAuthorityFact,
    MAX_AUTHORITY_SUBJECT_ID_BYTES, MAX_GRANT_IDEMPOTENCY_KEY_BYTES, MAX_TASK_DISCOVERY_SOURCE_CAP,
};
pub use crate::lease::{ActorLease, ActorLeaseId, ControlMode, LeaseRequest, RevocationReason};
pub use crate::origin::{normalize, AllowedRedirects, NormalizedOrigin};
pub use crate::phase::ActionPhase;
pub use crate::pipeline::{project_all, DestinationSet, NarrowingBreach};
pub use crate::precondition::{
    evaluate_dispatch, BrokerStanding, DispatchCheck, DispatchVerdict, ObservedState,
};
pub use crate::prepared::{
    plan_commit, CommitContext, CommitPlan, CommitReceipt, CommitRefusal, PrepareError,
    PrepareRequest, PreparedEffectId, PreparedEffectLedger, PreparedEffectStanding,
    PreparedEffectState, RenderedEffect, RenderedEffectDigest, MAX_PREPARED_EFFECTS_PER_SESSION,
};
pub use crate::redaction::{
    classify_zone, redact_for, FieldObservation, Projection, RedactionDestination,
    ZoneClassification,
};
pub use crate::report::{ConsumptionOutcome, DispatchAck, JournalOutcome, PostconditionReport};
pub use crate::risk::RiskClass;
pub use crate::sequence::{SequenceInput, SequenceProgress, SequenceState, StaleNodeSequence};
pub use crate::site::{assistant_navigation_verdict, classify_site, SiteClass, SiteTable};
pub use crate::step::StaleNodeStep;
pub use crate::time::{IdSource, MonotonicClock};
