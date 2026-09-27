// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Restored procedure access and task-start narrowing.

use crate::procedure_catalogue::{
    PreparedSkillMutation, ProcedureCatalogue, SkillMutationError, SkillStartError,
};
use crate::{context::PageArena, decode_page_observation, ObservationEffectView};
use procedure_engine::{PageFacts, PageNode};

use super::ProfileServiceRuntime;
use bip_types::identity::TaskId;

impl ProfileServiceRuntime {
    /// Prepares an exact internally recorded task flow as a reviewable draft.
    pub fn prepare_recorded_procedure(
        &self,
        procedure: procedure_engine::Procedure,
        recorded_at_epoch_ms: u64,
    ) -> Result<PreparedSkillMutation, SkillMutationError> {
        self.procedures
            .prepare_recorded_procedure(procedure, recorded_at_epoch_ms)
    }

    /// Restored procedure catalogue for page matching and explicit selection.
    pub const fn procedure_catalogue(&self) -> &ProcedureCatalogue {
        &self.procedures
    }

    /// Validates one exact-version saved-skill change before durable I/O.
    pub fn prepare_skill_mutation(
        &self,
        command: &core_service_types::MutateSkillCommand,
    ) -> Result<PreparedSkillMutation, SkillMutationError> {
        self.procedures.prepare_mutation(command)
    }

    /// Installs only the mutation whose browser write completed.
    pub fn install_skill_mutation(
        &mut self,
        prepared: PreparedSkillMutation,
    ) -> Result<(), SkillMutationError> {
        self.procedures.install_mutation(prepared)
    }

    /// Revalidates an explicitly selected procedure and can only reduce the
    /// task's already-reviewed tool authority.
    pub fn prepare_skill_start(
        &self,
        start: &mut core_service_types::StartTaskCommand,
    ) -> Result<(), SkillStartError> {
        let installations = self.core.asset_installations();
        if self
            .builtin_skills
            .prepare_start(&self.assistant_configuration, &installations, start)
            .map_err(|_| SkillStartError::NarrowingRefused)?
        {
            return Ok(());
        }
        crate::assistant_configuration::prepare_configured_start(
            &self.assistant_configuration,
            start,
        )
        .map_err(|_| SkillStartError::NarrowingRefused)?;
        let Some(skill_version_id) = start.skill_version_id.as_deref() else {
            return Ok(());
        };
        if start.consent_preview.source_discovery_enabled
            && start.template_id != core_service_types::TaskTemplateId::WebErrand
        {
            return Err(SkillStartError::ScopeDiffers);
        }
        let [source] = start.consent_preview.sources.as_slice() else {
            return Err(SkillStartError::ScopeDiffers);
        };
        if start.template_id == core_service_types::TaskTemplateId::WebErrand
            && start.consent_preview.provider_route
                == core_service_types::TaskProviderRoute::NoModelRequired
            && self
                .procedures
                .resolve(skill_version_id)
                .is_none_or(|procedure| procedure.recorded_from_task_id.is_none())
        {
            return Err(SkillStartError::NotRunnable);
        }
        start.tool_allowlist = self.procedures.narrow_start(
            skill_version_id,
            &source.normalized_origin,
            decode_task_milestone(start.milestone),
            &start.tool_allowlist,
        )?;
        Ok(())
    }

    /// Revalidates one replayed built-in against immutable catalogue and task
    /// facts. Current profile configuration and installations are deliberately
    /// absent from this boundary.
    pub fn validate_restored_builtin_task(
        &self,
        task_id: &TaskId,
    ) -> Result<(), crate::builtin_skills::BuiltinSkillBindingError> {
        let task = self
            .core
            .task(task_id)
            .ok_or(crate::builtin_skills::BuiltinSkillBindingError::UnknownDefinition)?;
        let Some(facts) = task.builtin_skill_binding_facts() else {
            return Ok(());
        };
        self.builtin_skills.validate_restore(&facts)
    }

    /// Validates saved-flow access after canonical task journal replay.
    /// Historical terminal tasks retain their selected version without
    /// acquiring new execution authority from the current catalogue.
    pub fn validate_restored_procedure_task(
        &self,
        task_id: &TaskId,
    ) -> Result<(), SkillStartError> {
        let task = self
            .core
            .task(task_id)
            .ok_or(SkillStartError::NotRunnable)?;
        let Some(version) = task.skill_version_id() else {
            return Ok(());
        };
        if task
            .view_facts()
            .map_err(|_| SkillStartError::NotRunnable)?
            .state
            .is_terminal()
        {
            return Ok(());
        }
        let procedure = self
            .procedures
            .resolve(version)
            .ok_or(SkillStartError::UnknownVersion)?;
        if procedure.is_runnable() {
            Ok(())
        } else {
            Err(SkillStartError::NotRunnable)
        }
    }

    /// Matches one exact complete page against the resident saved-skill
    /// catalogue. Raw graph bytes and page labels die inside this call; the
    /// result carries only immutable skill identities and bounded metadata.
    pub fn match_site_skills(
        &self,
        command: core_service_types::SiteSkillMatchCommand,
    ) -> core_service_types::SiteSkillMatchResult {
        match_site_skills(&self.procedures, self.private_profile, command)
    }
}

fn match_site_skills(
    procedures: &ProcedureCatalogue,
    private_profile: bool,
    command: core_service_types::SiteSkillMatchCommand,
) -> core_service_types::SiteSkillMatchResult {
    use core_service_types::{BipObservationStatus, SiteSkillMatchOffer, SiteSkillMatchStatus};

    let core_service_types::SiteSkillMatchCommand {
        operation,
        expected_tab_id,
        expected_frame_id,
        expected_page_epoch,
        expected_graph_revision,
        expected_origin,
        observation,
    } = command;
    if private_profile || observation.private_profile {
        return match_failure(operation, SiteSkillMatchStatus::PrivateProfile);
    }
    if observation.status == BipObservationStatus::Incomplete
        || observation.truncated
        || observation.may_change_answer
    {
        return match_failure(operation, SiteSkillMatchStatus::Incomplete);
    }
    if observation.status != BipObservationStatus::Ok {
        return match_failure(operation, SiteSkillMatchStatus::Unavailable);
    }
    if !bounded_identity(&expected_tab_id)
        || !bounded_identity(&expected_frame_id)
        || !bounded_identity(&expected_page_epoch)
        || expected_graph_revision == 0
        || expected_origin.is_empty()
        || expected_origin.len() > core_service_types::MAX_NORMALIZED_ORIGIN_BYTES
    {
        return match_failure(operation, SiteSkillMatchStatus::Malformed);
    }
    let Ok(normalized_origin) = policy_engine::origin::normalize_serialization(&expected_origin)
    else {
        return match_failure(operation, SiteSkillMatchStatus::Malformed);
    };
    if normalized_origin.display() != expected_origin {
        return match_failure(operation, SiteSkillMatchStatus::Malformed);
    }
    if observation.tab_id != expected_tab_id
        || observation.frame_id != expected_frame_id
        || observation.page_epoch != expected_page_epoch
        || observation.graph_revision != expected_graph_revision
        || observation.origin != expected_origin
    {
        return match_failure(operation, SiteSkillMatchStatus::StalePage);
    }

    let mut arena = PageArena::new();
    if decode_page_observation(
        operation.service_generation,
        ObservationEffectView::from(&observation),
        Some(&mut arena),
    )
    .is_err()
    {
        return match_failure(operation, SiteSkillMatchStatus::Malformed);
    }
    let facts = PageFacts::new(
        normalized_origin,
        arena
            .nodes()
            .iter()
            .map(|node| {
                let mut fact = PageNode::new(node.role).asserting(node.states.clone());
                if let Some(label) = &node.name {
                    fact = fact.labelled(label.clone());
                }
                fact
            })
            .collect(),
    );
    let mut offers = Vec::new();
    for offer in procedures.matching(&facts) {
        let Ok(step_count) = u32::try_from(offer.procedure.steps.len()) else {
            return match_failure(operation, SiteSkillMatchStatus::Malformed);
        };
        offers.push(SiteSkillMatchOffer {
            skill_version_id: offer.skill_version_id.to_owned(),
            skill_id: offer.procedure.id.as_str().to_owned(),
            active_version: offer.procedure.version.0,
            step_count,
        });
    }
    core_service_types::SiteSkillMatchResult {
        operation,
        status: SiteSkillMatchStatus::Available,
        tab_id: expected_tab_id,
        frame_id: expected_frame_id,
        page_epoch: expected_page_epoch,
        graph_revision: expected_graph_revision,
        origin: expected_origin,
        offers,
    }
}

fn match_failure(
    operation: core_service_types::OperationEnvelope,
    status: core_service_types::SiteSkillMatchStatus,
) -> core_service_types::SiteSkillMatchResult {
    core_service_types::SiteSkillMatchResult {
        operation,
        status,
        tab_id: String::new(),
        frame_id: String::new(),
        page_epoch: String::new(),
        graph_revision: 0,
        origin: String::new(),
        offers: Vec::new(),
    }
}

fn bounded_identity(value: &str) -> bool {
    !value.is_empty() && value.len() <= core_service_types::MAX_IDENTIFIER_BYTES
}

const fn decode_task_milestone(value: core_service_types::TaskMilestone) -> task_engine::Milestone {
    match value {
        core_service_types::TaskMilestone::M0 => task_engine::Milestone::M0,
        core_service_types::TaskMilestone::M1 => task_engine::Milestone::M1,
        core_service_types::TaskMilestone::M2 => task_engine::Milestone::M2,
        core_service_types::TaskMilestone::M3 => task_engine::Milestone::M3,
        core_service_types::TaskMilestone::M4 => task_engine::Milestone::M4,
        core_service_types::TaskMilestone::M5 => task_engine::Milestone::M5,
        core_service_types::TaskMilestone::M6 => task_engine::Milestone::M6,
        core_service_types::TaskMilestone::M7 => task_engine::Milestone::M7,
        core_service_types::TaskMilestone::M8 => task_engine::Milestone::M8,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use bip_types::PROTOCOL_VERSION;
    use core_service_types as wire;
    use procedure_engine::{build_source_table, encode, ProcedureStatus};

    fn u16(out: &mut Vec<u8>, value: u16) {
        out.extend_from_slice(&value.to_le_bytes());
    }

    fn u32(out: &mut Vec<u8>, value: u32) {
        out.extend_from_slice(&value.to_le_bytes());
    }

    fn u64(out: &mut Vec<u8>, value: u64) {
        out.extend_from_slice(&value.to_le_bytes());
    }

    fn short(out: &mut Vec<u8>, value: &str) {
        u16(out, u16::try_from(value.len()).unwrap());
        out.extend_from_slice(value.as_bytes());
    }

    fn graph() -> Vec<u8> {
        let mut out = vec![4];
        short(&mut out, PROTOCOL_VERSION);
        u32(&mut out, 1);
        short(&mut out, "node-1");
        short(&mut out, "frame-1");
        u16(&mut out, 0); // DOCUMENT
        out.push(0); // NOT_SENSITIVE
        out.push(0); // no withheld flags
        out.push(9); // UNKNOWN value kind
        short(&mut out, "Public heading");
        u32(&mut out, 0); // no declared text runs
        u64(&mut out, 0); // no declared text bytes
        u32(&mut out, 0); // no actions
        u32(&mut out, 0); // no states
        out.push(0); // no destination
        out.push(2); // FIRST_PARTY_DOCUMENT
        u32(&mut out, 0); // no content signals
        u32(&mut out, 0); // no emitted text runs
        u32(&mut out, 0); // no edges
        out
    }

    fn catalogue() -> ProcedureCatalogue {
        let origin =
            policy_engine::origin::normalize_serialization("https://example.test").unwrap();
        let mut procedure = build_source_table(&origin).unwrap();
        procedure.status = ProcedureStatus::Active;
        let record = wire::SkillRecord {
            skill_id: procedure.id.as_str().to_owned(),
            origin: procedure.scope.origin().display(),
            provenance: wire::SkillProvenance::Authored,
            status: wire::SkillStatus::Active,
            active_version: procedure.version.0,
            definition: encode(&procedure).unwrap(),
            step_count: u32::try_from(procedure.steps.len()).unwrap(),
            installed_at_utc_ms: 1,
            updated_at_utc_ms: 2,
        };
        ProcedureCatalogue::restore(vec![record], Vec::new(), task_engine::Milestone::M5).unwrap()
    }

    fn command() -> wire::SiteSkillMatchCommand {
        let payload = graph();
        wire::SiteSkillMatchCommand {
            operation: wire::OperationEnvelope {
                operation_id: "match-1".to_owned(),
                service_generation: 9,
                task_revision: 0,
                deadline_monotonic_ms: 10_000,
                idempotency_key: "match-key-1".to_owned(),
            },
            expected_tab_id: "tab-1".to_owned(),
            expected_frame_id: "frame-1".to_owned(),
            expected_page_epoch: "epoch-1".to_owned(),
            expected_graph_revision: 7,
            expected_origin: "https://example.test".to_owned(),
            observation: wire::ObservationEffectResult {
                status: wire::BipObservationStatus::Ok,
                schema_version: PROTOCOL_VERSION.to_owned(),
                tab_id: "tab-1".to_owned(),
                frame_id: "frame-1".to_owned(),
                page_epoch: "epoch-1".to_owned(),
                graph_revision: 7,
                origin: "https://example.test".to_owned(),
                is_potentially_trustworthy: true,
                private_profile: false,
                node_count: 1,
                total_bytes: u32::try_from(payload.len()).unwrap(),
                truncated: false,
                may_change_answer: false,
                redacted_field_count: 0,
                suppressed_secret_value_count: 0,
                sensitive_zone_count: 0,
                policy_filtered_frame_count: 0,
                highest_sensitivity: wire::BipSensitivity::NotSensitive,
                graph_encoding: wire::BipGraphEncoding::BipContract,
                graph_payload: payload,
                media: None,
            },
        }
    }

    #[test]
    fn complete_exact_page_returns_only_the_matching_immutable_version() {
        let result = match_site_skills(&catalogue(), false, command());
        assert_eq!(result.status, wire::SiteSkillMatchStatus::Available);
        assert_eq!(result.tab_id, "tab-1");
        assert_eq!(result.graph_revision, 7);
        assert_eq!(result.offers.len(), 1);
        let offer = result.offers.first().unwrap();
        assert_eq!(offer.skill_id, "source-table");
        assert_eq!(offer.skill_version_id, "source-table@1");
        assert_eq!(offer.active_version, 1);
        assert!((1..=u32::try_from(wire::MAX_SKILL_STEPS).unwrap()).contains(&offer.step_count));
    }

    #[test]
    fn stale_incomplete_and_private_pages_never_return_offers() {
        let mut stale = command();
        stale.observation.page_epoch = "epoch-2".to_owned();
        let stale = match_site_skills(&catalogue(), false, stale);
        assert_eq!(stale.status, wire::SiteSkillMatchStatus::StalePage);
        assert!(stale.offers.is_empty());

        let mut incomplete = command();
        incomplete.observation.status = wire::BipObservationStatus::Incomplete;
        incomplete.observation.truncated = true;
        let incomplete = match_site_skills(&catalogue(), false, incomplete);
        assert_eq!(incomplete.status, wire::SiteSkillMatchStatus::Incomplete);
        assert!(incomplete.offers.is_empty());

        let private = match_site_skills(&catalogue(), true, command());
        assert_eq!(private.status, wire::SiteSkillMatchStatus::PrivateProfile);
        assert!(private.offers.is_empty());
    }
}
