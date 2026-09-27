// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Canonical identities for effects whose execution crosses a process seam.
//!
//! A durable identity names the logical work and deliberately excludes a
//! service generation: the same unresolved consequential work must make the
//! same durable claim after the utility process restarts. A live-attempt
//! identity includes both browser session and generation because the browser's
//! generation counter is only unique inside one browser process.

use crate::{wire, DigestError, Sha256Port};

const DIGEST_HEX_BYTES: usize = 64;
const NIBBLES: &[u8; 16] = b"0123456789abcdef";

const _: () = {
    assert!("task-effect-operation-".len() + DIGEST_HEX_BYTES <= wire::MAX_OPERATION_ID_BYTES);
    assert!("task-effect-".len() + DIGEST_HEX_BYTES <= wire::MAX_IDENTIFIER_BYTES);
    assert!("task-effect-idempotency-".len() + DIGEST_HEX_BYTES <= wire::MAX_IDEMPOTENCY_KEY_BYTES);
    assert!("task-settle-operation-".len() + DIGEST_HEX_BYTES <= wire::MAX_OPERATION_ID_BYTES);
    assert!("task-settle-idempotency-".len() + DIGEST_HEX_BYTES <= wire::MAX_IDEMPOTENCY_KEY_BYTES);
    assert!("task-settle-trace-".len() + DIGEST_HEX_BYTES <= wire::MAX_IDENTIFIER_BYTES);
    assert!("model-attempt-operation-".len() + DIGEST_HEX_BYTES <= wire::MAX_OPERATION_ID_BYTES);
    assert!("model-attempt-".len() + DIGEST_HEX_BYTES <= wire::MAX_IDENTIFIER_BYTES);
    assert!(
        "model-attempt-idempotency-".len() + DIGEST_HEX_BYTES <= wire::MAX_IDEMPOTENCY_KEY_BYTES
    );
    assert!("asset-startup-operation-".len() + DIGEST_HEX_BYTES <= wire::MAX_OPERATION_ID_BYTES);
    assert!(
        "asset-startup-idempotency-".len() + DIGEST_HEX_BYTES <= wire::MAX_IDEMPOTENCY_KEY_BYTES
    );
    assert!("asset-effect-".len() + DIGEST_HEX_BYTES <= wire::MAX_IDENTIFIER_BYTES);
};

/// Whether a task effect names durable logical work or one live attempt.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TaskEffectIdentityScope<'a> {
    /// The same unresolved consequential work keeps this identity forever.
    DurableWork,
    /// Retryable/live work gets a fresh attempt identity per incarnation.
    LiveAttempt {
        browser_session_id: &'a str,
        generation: u64,
    },
}

/// Names one paid sub-attempt of an already durable model effect.
///
/// The root effect id is itself stable across service generations. Deriving
/// only from it and the ordinal keeps a live failover distinct while making a
/// replay rediscover the same claim rather than authorizing paid work again.
pub fn model_attempt_identities(
    digest: &dyn Sha256Port,
    root_effect_id: &str,
    attempt_ordinal: u32,
) -> Result<(String, String, String), DigestError> {
    let hex = digest_fields(
        digest,
        "taffy.model-attempt.durable.v1",
        &[Field::Text(root_effect_id), Field::U32(attempt_ordinal)],
    )?;
    Ok((
        format!("model-attempt-operation-{hex}"),
        format!("model-attempt-{hex}"),
        format!("model-attempt-idempotency-{hex}"),
    ))
}

enum Field<'a> {
    Text(&'a str),
    U32(u32),
    U64(u64),
}

fn push_text(material: &mut Vec<u8>, value: &str) -> Result<(), DigestError> {
    material.push(b's');
    let length = u64::try_from(value.len()).map_err(|_| DigestError::Unavailable)?;
    material.extend_from_slice(&length.to_be_bytes());
    material.extend_from_slice(value.as_bytes());
    Ok(())
}

fn digest_fields(
    digest: &dyn Sha256Port,
    domain: &str,
    fields: &[Field<'_>],
) -> Result<String, DigestError> {
    let mut material = Vec::new();
    push_text(&mut material, domain)?;
    for field in fields {
        match field {
            Field::Text(value) => push_text(&mut material, value)?,
            Field::U32(value) => {
                material.push(b'i');
                material.extend_from_slice(&value.to_be_bytes());
            }
            Field::U64(value) => {
                material.push(b'l');
                material.extend_from_slice(&value.to_be_bytes());
            }
        }
    }
    let bytes = digest.sha256(&material)?;
    let mut hex = String::with_capacity(DIGEST_HEX_BYTES);
    for byte in bytes {
        let upper = NIBBLES
            .get(usize::from(byte >> 4))
            .copied()
            .ok_or(DigestError::Unavailable)?;
        let lower = NIBBLES
            .get(usize::from(byte & 0x0f))
            .copied()
            .ok_or(DigestError::Unavailable)?;
        hex.push(char::from(upper));
        hex.push(char::from(lower));
    }
    Ok(hex)
}

/// Names one reducer effect and its operation metadata.
pub fn task_effect_identities(
    digest: &dyn Sha256Port,
    scope: TaskEffectIdentityScope<'_>,
    task_id: &str,
    revision: u64,
    parent_operation_id: &str,
    batch_ordinal: u32,
    kind: u32,
) -> Result<(String, String, String), DigestError> {
    let stable = [
        Field::Text(task_id),
        Field::U64(revision),
        Field::Text(parent_operation_id),
        Field::U32(batch_ordinal),
        Field::U32(kind),
    ];
    let hex = match scope {
        TaskEffectIdentityScope::DurableWork => {
            digest_fields(digest, "taffy.task-effect.durable.v2", &stable)?
        }
        TaskEffectIdentityScope::LiveAttempt {
            browser_session_id,
            generation,
        } => {
            let live = [
                Field::Text(browser_session_id),
                Field::U64(generation),
                Field::Text(task_id),
                Field::U64(revision),
                Field::Text(parent_operation_id),
                Field::U32(batch_ordinal),
                Field::U32(kind),
            ];
            digest_fields(digest, "taffy.task-effect.live.v2", &live)?
        }
    };
    Ok((
        format!("task-effect-operation-{hex}"),
        format!("task-effect-{hex}"),
        format!("task-effect-idempotency-{hex}"),
    ))
}

/// Names one durable pause/cancel settlement commit.
pub fn task_settlement_identities(
    digest: &dyn Sha256Port,
    task_id: &str,
    revision: u64,
    kind: u32,
) -> Result<(String, String, String), DigestError> {
    let hex = digest_fields(
        digest,
        "taffy.task-settlement.v2",
        &[Field::Text(task_id), Field::U64(revision), Field::U32(kind)],
    )?;
    Ok((
        format!("task-settle-operation-{hex}"),
        format!("task-settle-idempotency-{hex}"),
        format!("task-settle-trace-{hex}"),
    ))
}

/// Names the synthetic causation for delivery planning at service startup.
pub fn startup_asset_identities(
    digest: &dyn Sha256Port,
    browser_session_id: &str,
    generation: u64,
) -> Result<(String, String), DigestError> {
    let hex = digest_fields(
        digest,
        "taffy.asset-startup.v2",
        &[Field::Text(browser_session_id), Field::U64(generation)],
    )?;
    Ok((
        format!("asset-startup-operation-{hex}"),
        format!("asset-startup-idempotency-{hex}"),
    ))
}

/// Names one exact delivery attempt by content rather than catalog position.
pub fn asset_effect_identity(
    digest: &dyn Sha256Port,
    operation_id: &str,
    asset_id: &str,
    asset_revision: &str,
    operation_kind: u32,
    attempt: u32,
) -> Result<String, DigestError> {
    let hex = digest_fields(
        digest,
        "taffy.asset-effect.v2",
        &[
            Field::Text(operation_id),
            Field::Text(asset_id),
            Field::Text(asset_revision),
            Field::U32(operation_kind),
            Field::U32(attempt),
        ],
    )?;
    Ok(format!("asset-effect-{hex}"))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::account::crypto::ReferenceSha256;

    #[test]
    fn durable_task_work_is_stable_and_not_delimiter_ambiguous() {
        let first = task_effect_identities(
            &ReferenceSha256,
            TaskEffectIdentityScope::DurableWork,
            "task:a",
            7,
            "parent",
            1,
            4,
        )
        .unwrap();
        let same = task_effect_identities(
            &ReferenceSha256,
            TaskEffectIdentityScope::DurableWork,
            "task:a",
            7,
            "parent",
            1,
            4,
        )
        .unwrap();
        let spliced = task_effect_identities(
            &ReferenceSha256,
            TaskEffectIdentityScope::DurableWork,
            "task",
            7,
            "a:parent",
            1,
            4,
        )
        .unwrap();
        assert_eq!(first, same);
        assert_ne!(first, spliced);

        let effect = |task, revision, parent, ordinal, kind| {
            task_effect_identities(
                &ReferenceSha256,
                TaskEffectIdentityScope::DurableWork,
                task,
                revision,
                parent,
                ordinal,
                kind,
            )
            .unwrap()
            .1
        };
        let base = effect("task-1", 7, "parent-1", 1, 4);
        for changed in [
            effect("task-2", 7, "parent-1", 1, 4),
            effect("task-1", 8, "parent-1", 1, 4),
            effect("task-1", 7, "parent-2", 1, 4),
            effect("task-1", 7, "parent-1", 2, 4),
            effect("task-1", 7, "parent-1", 1, 5),
        ] {
            assert_ne!(base, changed);
        }
    }

    #[test]
    fn live_task_attempts_are_unique_across_six_incarnations() {
        let mut ids = std::collections::BTreeSet::new();
        for session in ["browser-a", "browser-b"] {
            for generation in 1..=3 {
                let (_, effect, _) = task_effect_identities(
                    &ReferenceSha256,
                    TaskEffectIdentityScope::LiveAttempt {
                        browser_session_id: session,
                        generation,
                    },
                    "task-1",
                    7,
                    "parent-1",
                    0,
                    4,
                )
                .unwrap();
                assert!(ids.insert(effect));
            }
        }
        assert_eq!(ids.len(), 6);
    }

    #[test]
    fn model_sub_attempts_are_durable_distinct_and_delimiter_safe() {
        let first = model_attempt_identities(&ReferenceSha256, "root:a", 1).unwrap();
        let same = model_attempt_identities(&ReferenceSha256, "root:a", 1).unwrap();
        let next = model_attempt_identities(&ReferenceSha256, "root:a", 2).unwrap();
        let spliced = model_attempt_identities(&ReferenceSha256, "root", 1).unwrap();
        assert_eq!(first, same);
        assert_ne!(first, next);
        assert_ne!(first, spliced);
    }

    #[test]
    fn startup_and_asset_attempt_facts_are_all_identity_material() {
        let startup_a = startup_asset_identities(&ReferenceSha256, "browser-a", 1).unwrap();
        let startup_b = startup_asset_identities(&ReferenceSha256, "browser-b", 1).unwrap();
        assert_ne!(startup_a, startup_b);

        let first = asset_effect_identity(
            &ReferenceSha256,
            &startup_a.0,
            "country-flags",
            "2026.1",
            0,
            1,
        )
        .unwrap();
        assert_eq!(
            first,
            asset_effect_identity(
                &ReferenceSha256,
                &startup_a.0,
                "country-flags",
                "2026.1",
                0,
                1,
            )
            .unwrap()
        );
        assert_ne!(
            first,
            asset_effect_identity(
                &ReferenceSha256,
                &startup_a.0,
                "country-flags",
                "2026.1",
                0,
                2,
            )
            .unwrap()
        );
    }

    #[test]
    fn settlement_identity_excludes_process_incarnation() {
        let first = task_settlement_identities(&ReferenceSha256, "task-1", 8, 1).unwrap();
        let same = task_settlement_identities(&ReferenceSha256, "task-1", 8, 1).unwrap();
        let other_task = task_settlement_identities(&ReferenceSha256, "task-2", 8, 1).unwrap();
        let other_revision = task_settlement_identities(&ReferenceSha256, "task-1", 9, 1).unwrap();
        let other_kind = task_settlement_identities(&ReferenceSha256, "task-1", 8, 2).unwrap();
        assert_eq!(first, same);
        assert_ne!(first, other_task);
        assert_ne!(first, other_revision);
        assert_ne!(first, other_kind);
    }
}
