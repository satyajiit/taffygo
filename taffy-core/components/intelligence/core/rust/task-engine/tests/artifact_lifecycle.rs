// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

mod common;

use task_engine::{
    ArtifactCustody, ArtifactId, ArtifactKind, Command, Effect, EventKind, Reducer, RefusalReason,
};

#[test]
fn only_a_successful_result_owned_render_can_be_accepted_and_exported() {
    let mut fixture = common::running();
    let accepted_id = fixture.artifact_id.clone();
    let unrelated_id = ArtifactId::new("artifact_1");

    let refusal = fixture
        .apply(Command::RequestArtifact {
            artifact_id: accepted_id.clone(),
            format: ArtifactKind::Markdown,
            workspace_revision: 0,
        })
        .err()
        .unwrap_or_else(|| unreachable!("a render without an evidence revision is refused"));
    assert_eq!(refusal.reason, RefusalReason::ArtifactNotReady);

    let ready = fixture
        .apply(Command::RequestArtifact {
            artifact_id: accepted_id.clone(),
            format: ArtifactKind::Markdown,
            workspace_revision: 3,
        })
        .unwrap_or_else(|refusal| unreachable!("the bounded render is recorded: {refusal:?}"));
    assert!(ready
        .events
        .iter()
        .any(|event| event.kind == EventKind::ArtifactReady));
    assert_eq!(
        ready.effects,
        vec![Effect::GenerateArtifact {
            artifact_id: accepted_id.clone(),
            kind: ArtifactKind::Markdown,
            workspace_revision: 3,
        }]
    );

    fixture.must_apply(Command::RequestArtifact {
        artifact_id: unrelated_id.clone(),
        format: ArtifactKind::Csv,
        workspace_revision: 3,
    });
    fixture.must_apply(Command::ResultCandidateReady);
    fixture.must_apply(Command::CompleteResultValidated(common::complete_result()));

    let not_in_result = fixture
        .apply(Command::AcceptArtifact {
            artifact_id: unrelated_id.clone(),
        })
        .err()
        .unwrap_or_else(|| unreachable!("an artifact outside the result cannot be accepted"));
    assert_eq!(not_in_result.reason, RefusalReason::ArtifactNotReady);

    fixture.must_apply(Command::AcceptArtifact {
        artifact_id: accepted_id.clone(),
    });
    let wrong_format = fixture
        .apply(Command::ExportArtifact {
            artifact_id: accepted_id.clone(),
            format: ArtifactKind::Csv,
        })
        .err()
        .unwrap_or_else(|| unreachable!("the caller cannot relabel deterministic bytes"));
    assert_eq!(wrong_format.reason, RefusalReason::ArtifactNotReady);

    let exported = fixture
        .apply(Command::ExportArtifact {
            artifact_id: accepted_id.clone(),
            format: ArtifactKind::Markdown,
        })
        .unwrap_or_else(|refusal| unreachable!("the accepted exact artifact exports: {refusal:?}"));
    assert_eq!(
        exported.effects,
        vec![Effect::ExportArtifact {
            artifact_id: accepted_id.clone(),
            kind: ArtifactKind::Markdown,
            workspace_revision: 3,
        }]
    );

    let journal = fixture.reducer.journal().clone();
    let (replayed, _) = Reducer::replay(
        common::seed(),
        common::defaults(),
        task_engine::ManualClock::at(1_000),
        task_engine::SequentialIds::new(),
        &journal,
    )
    .unwrap_or_else(|error| unreachable!("a journal written here replays: {error:?}"));
    let record = replayed
        .artifact(&accepted_id)
        .unwrap_or_else(|| unreachable!("replay reconstructs the artifact record"));
    assert_eq!(record.kind(), ArtifactKind::Markdown);
    assert_eq!(record.workspace_revision(), 3);
    assert_eq!(record.custody(), ArtifactCustody::Workspace);
    assert_eq!(replayed.journal().entries(), journal.entries());
}

#[test]
fn every_admitted_format_uses_the_same_acceptance_and_export_lifecycle() {
    for kind in ArtifactKind::ALL {
        let mut fixture = common::running();
        let artifact_id = fixture.artifact_id.clone();
        let ready = fixture
            .apply(Command::RequestArtifact {
                artifact_id: artifact_id.clone(),
                format: *kind,
                workspace_revision: 9,
            })
            .unwrap_or_else(|refusal| unreachable!("{} refused: {refusal:?}", kind.label()));
        assert_eq!(
            ready.effects,
            vec![Effect::GenerateArtifact {
                artifact_id: artifact_id.clone(),
                kind: *kind,
                workspace_revision: 9,
            }],
            "{}",
            kind.label()
        );

        fixture.must_apply(Command::ResultCandidateReady);
        fixture.must_apply(Command::CompleteResultValidated(common::complete_result()));
        fixture.must_apply(Command::AcceptArtifact {
            artifact_id: artifact_id.clone(),
        });
        let exported = fixture
            .apply(Command::ExportArtifact {
                artifact_id: artifact_id.clone(),
                format: *kind,
            })
            .unwrap_or_else(|refusal| unreachable!("{} refused: {refusal:?}", kind.label()));
        assert_eq!(
            exported.effects,
            vec![Effect::ExportArtifact {
                artifact_id,
                kind: *kind,
                workspace_revision: 9,
            }],
            "{}",
            kind.label()
        );
    }
}
