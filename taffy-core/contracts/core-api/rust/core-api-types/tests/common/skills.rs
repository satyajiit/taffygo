// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use core_api_types::{
    SiteSkillArgumentKind as Kind, SiteSkillObservedArgument as Argument,
    SiteSkillObservedStep as Step, SiteSkillProvenanceView as Provenance, SiteSkillSemanticTarget,
    SiteSkillStatusView as Status, SiteSkillView,
};

pub(super) fn skills_fixture() -> Vec<SiteSkillView> {
    vec![
        SiteSkillView {
            skill_id: "compare-products".to_owned(),
            origin: "https://shop.example".to_owned(),
            provenance: Provenance::Authored,
            status: Status::Active,
            active_version: 2,
            step_count: 3,
            installed_at_epoch_ms: 1_700_000_001_000,
            updated_at_epoch_ms: 1_700_000_002_000,
            recorded_from_task_id: None,
            reviewed_steps: Vec::new(),
        },
        SiteSkillView {
            skill_id: "learned-document".to_owned(),
            origin: "https://example.test".to_owned(),
            provenance: Provenance::RecordedFromTask,
            status: Status::Draft,
            active_version: 1,
            step_count: 3,
            installed_at_epoch_ms: 1_700_000_003_000,
            updated_at_epoch_ms: 1_700_000_003_000,
            recorded_from_task_id: Some("task-completed-1".to_owned()),
            reviewed_steps: vec![
                step(
                    "browser.navigate",
                    1,
                    Argument {
                        kind: Kind::PublicAddress,
                        public_address: Some("https://example.test/entry".to_owned()),
                        ..argument()
                    },
                ),
                step(
                    "user.handover",
                    0,
                    Argument {
                        kind: Kind::Choice,
                        value: 1,
                        ..argument()
                    },
                ),
                step(
                    "browser.download.from_link",
                    7,
                    Argument {
                        kind: Kind::SemanticTarget,
                        semantic_target: Some(SiteSkillSemanticTarget {
                            role: 9,
                            phrase: 10,
                        }),
                        ..argument()
                    },
                ),
            ],
        },
    ]
}

fn argument() -> Argument {
    Argument {
        parameter: 0,
        kind: Kind::Count,
        value: 0,
        purpose: 0,
        public_address: None,
        semantic_target: None,
    }
}

fn step(verb: &str, postcondition: u32, argument: Argument) -> Step {
    Step {
        verb: verb.to_owned(),
        postcondition,
        arguments: vec![argument],
        has_fill: false,
        fill_purpose: 0,
    }
}
