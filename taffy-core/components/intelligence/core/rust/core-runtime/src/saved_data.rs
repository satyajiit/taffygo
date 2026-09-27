// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Bounded projection of Chromium-owned saved sign-ins and address profiles.
//!
//! Chromium remains the persistence and mutation authority. This module owns
//! only the immutable state that a Core API surface may draw. In particular,
//! credential secret material is not a Rust type and cannot cross this seam.

use std::collections::BTreeSet;

use core_api_types::{
    SavedDataAvailability as ViewAvailability, SavedDetailView, SavedDetailsView, SavedSignInView,
    SavedSignInsView,
};
use core_service_types::{
    ReplaceSavedDataSnapshotCommand, SavedDataAvailability, SavedDetailRecord, SavedSignInMetadata,
};

const MAX_PLATFORM_EPOCH_MILLIS: u64 = 0x7fff_ffff_ffff_ffff;

/// Why a browser-authored snapshot was refused.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum SavedDataSnapshotError {
    /// Private profiles never expose or mutate persistent saved data.
    PrivateProfile,
    /// A collection exceeded its generated contract bound.
    TooManyRecords,
    /// A record was malformed, duplicated, or exceeded a generated bound.
    InvalidRecord,
    /// Availability, revision, and record presence disagreed.
    InvalidAvailability,
    /// A browser snapshot attempted to replace a newer projection.
    StaleRevision,
}

/// The complete resident projection for both Chromium-owned stores.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SavedDataState {
    sign_ins: SavedSignInsView,
    details: SavedDetailsView,
    sign_ins_revision_high_water: u64,
    details_revision_high_water: u64,
}

impl SavedDataState {
    /// Initial state before both normal-profile stores finish loading.
    pub const fn loading() -> Self {
        Self {
            sign_ins: SavedSignInsView {
                availability: ViewAvailability::Loading,
                revision: 0,
                records: Vec::new(),
            },
            details: SavedDetailsView {
                availability: ViewAvailability::Loading,
                revision: 0,
                people: Vec::new(),
            },
            sign_ins_revision_high_water: 0,
            details_revision_high_water: 0,
        }
    }

    /// Permanent private-profile state.
    pub const fn unavailable() -> Self {
        Self {
            sign_ins: SavedSignInsView {
                availability: ViewAvailability::Unavailable,
                revision: 0,
                records: Vec::new(),
            },
            details: SavedDetailsView {
                availability: ViewAvailability::Unavailable,
                revision: 0,
                people: Vec::new(),
            },
            sign_ins_revision_high_water: 0,
            details_revision_high_water: 0,
        }
    }

    /// Current sign-in metadata projection.
    pub fn sign_ins(&self) -> SavedSignInsView {
        self.sign_ins.clone()
    }

    /// Current saved-details projection.
    pub fn details(&self) -> SavedDetailsView {
        self.details.clone()
    }

    /// Validates and atomically replaces both store projections.
    pub fn replace(
        &mut self,
        command: ReplaceSavedDataSnapshotCommand,
        private_profile: bool,
    ) -> Result<(), SavedDataSnapshotError> {
        if private_profile {
            return Err(SavedDataSnapshotError::PrivateProfile);
        }
        let sign_ins = validate_sign_ins(
            command.sign_ins_availability,
            command.sign_ins_revision,
            command.sign_ins,
        )?;
        let details = validate_details(
            command.details_availability,
            command.details_revision,
            command.details,
        )?;
        reject_rollback(self.sign_ins_revision_high_water, &self.sign_ins, &sign_ins)?;
        reject_rollback(self.details_revision_high_water, &self.details, &details)?;
        if sign_ins.availability == ViewAvailability::Ready {
            self.sign_ins_revision_high_water = sign_ins.revision;
        }
        if details.availability == ViewAvailability::Ready {
            self.details_revision_high_water = details.revision;
        }
        self.sign_ins = sign_ins;
        self.details = details;
        Ok(())
    }
}

fn reject_rollback<T>(
    revision_high_water: u64,
    current: &T,
    replacement: &T,
) -> Result<(), SavedDataSnapshotError>
where
    T: Eq + Revisioned,
{
    if replacement.availability() == ViewAvailability::Ready
        && (replacement.revision() < revision_high_water
            || (replacement.revision() == revision_high_water
                && (current.availability() != ViewAvailability::Ready || replacement != current)))
    {
        return Err(SavedDataSnapshotError::StaleRevision);
    }
    Ok(())
}

trait Revisioned {
    fn revision(&self) -> u64;
    fn availability(&self) -> ViewAvailability;
}

impl Revisioned for SavedSignInsView {
    fn revision(&self) -> u64 {
        self.revision
    }

    fn availability(&self) -> ViewAvailability {
        self.availability
    }
}

impl Revisioned for SavedDetailsView {
    fn revision(&self) -> u64 {
        self.revision
    }

    fn availability(&self) -> ViewAvailability {
        self.availability
    }
}

fn validate_sign_ins(
    availability: SavedDataAvailability,
    revision: u64,
    records: Vec<SavedSignInMetadata>,
) -> Result<SavedSignInsView, SavedDataSnapshotError> {
    let availability = availability_pair(availability, revision, records.is_empty())?;
    if records.len() > core_service_types::MAX_SAVED_SIGN_INS {
        return Err(SavedDataSnapshotError::TooManyRecords);
    }
    let mut ids = BTreeSet::new();
    let mut projected = Vec::with_capacity(records.len());
    for record in records {
        if !valid_identifier(&record.id)
            || !ids.insert(record.id.clone())
            || !valid_host(&record.site)
            || record.last_used_epoch_ms > MAX_PLATFORM_EPOCH_MILLIS
            || !valid_text(
                &record.username,
                core_service_types::MAX_SAVED_SIGN_IN_USERNAME_BYTES,
                false,
            )
        {
            return Err(SavedDataSnapshotError::InvalidRecord);
        }
        projected.push(SavedSignInView {
            id: record.id,
            site: record.site,
            username: record.username,
            last_used_epoch_ms: record.last_used_epoch_ms,
        });
    }
    Ok(SavedSignInsView {
        availability,
        revision,
        records: projected,
    })
}

fn validate_details(
    availability: SavedDataAvailability,
    revision: u64,
    records: Vec<SavedDetailRecord>,
) -> Result<SavedDetailsView, SavedDataSnapshotError> {
    let availability = availability_pair(availability, revision, records.is_empty())?;
    if records.len() > core_service_types::MAX_SAVED_DETAILS {
        return Err(SavedDataSnapshotError::TooManyRecords);
    }
    let mut ids = BTreeSet::new();
    let mut people = Vec::with_capacity(records.len());
    for record in records {
        if !valid_identifier(&record.id) || !ids.insert(record.id.clone()) || !valid_detail(&record)
        {
            return Err(SavedDataSnapshotError::InvalidRecord);
        }
        people.push(SavedDetailView {
            id: record.id,
            given_name: record.given_name,
            family_name: record.family_name,
            email: record.email,
            phone: record.phone,
            address: record.address,
            postcode: record.postcode,
            country: record.country,
        });
    }
    Ok(SavedDetailsView {
        availability,
        revision,
        people,
    })
}

fn availability_pair(
    availability: SavedDataAvailability,
    revision: u64,
    empty: bool,
) -> Result<ViewAvailability, SavedDataSnapshotError> {
    match availability {
        SavedDataAvailability::Ready if revision > 0 => Ok(ViewAvailability::Ready),
        SavedDataAvailability::Loading if revision == 0 && empty => Ok(ViewAvailability::Loading),
        SavedDataAvailability::Unavailable if revision == 0 && empty => {
            Ok(ViewAvailability::Unavailable)
        }
        SavedDataAvailability::Ready
        | SavedDataAvailability::Loading
        | SavedDataAvailability::Unavailable => Err(SavedDataSnapshotError::InvalidAvailability),
    }
}

fn valid_identifier(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= core_service_types::MAX_IDENTIFIER_BYTES
        && value
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'-' | b'_'))
}

fn valid_host(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= core_service_types::MAX_SAVED_SIGN_IN_SITE_BYTES
        && value.is_ascii()
        && !value
            .bytes()
            .any(|byte| byte.is_ascii_whitespace() || matches!(byte, b'/' | b'?' | b'#' | b'@'))
        && value.bytes().all(|byte| {
            byte.is_ascii_alphanumeric() || matches!(byte, b'.' | b'-' | b'[' | b']' | b':')
        })
}

fn valid_detail(record: &SavedDetailRecord) -> bool {
    let any_value = !record.given_name.is_empty()
        || !record.family_name.is_empty()
        || !record.email.is_empty()
        || !record.phone.is_empty()
        || !record.address.is_empty()
        || !record.postcode.is_empty()
        || !record.country.is_empty();
    any_value
        && valid_text(
            &record.given_name,
            core_service_types::MAX_SAVED_DETAIL_NAME_BYTES,
            false,
        )
        && valid_text(
            &record.family_name,
            core_service_types::MAX_SAVED_DETAIL_NAME_BYTES,
            false,
        )
        && valid_text(
            &record.email,
            core_service_types::MAX_SAVED_DETAIL_EMAIL_BYTES,
            false,
        )
        && valid_text(
            &record.phone,
            core_service_types::MAX_SAVED_DETAIL_PHONE_BYTES,
            false,
        )
        && valid_text(
            &record.address,
            core_service_types::MAX_SAVED_DETAIL_ADDRESS_BYTES,
            true,
        )
        && valid_text(
            &record.postcode,
            core_service_types::MAX_SAVED_DETAIL_POSTCODE_BYTES,
            false,
        )
        && valid_text(
            &record.country,
            core_service_types::MAX_SAVED_DETAIL_COUNTRY_BYTES,
            false,
        )
}

fn valid_text(value: &str, max_bytes: usize, allow_newline: bool) -> bool {
    value.len() <= max_bytes
        && value.chars().all(|character| {
            !character.is_control() || (allow_newline && matches!(character, '\n' | '\r'))
        })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn command() -> ReplaceSavedDataSnapshotCommand {
        ReplaceSavedDataSnapshotCommand {
            sign_ins_availability: SavedDataAvailability::Ready,
            sign_ins_revision: 1,
            sign_ins: vec![SavedSignInMetadata {
                id: "sign-in-1".to_owned(),
                site: "accounts.example".to_owned(),
                username: "sensitive-user".to_owned(),
                last_used_epoch_ms: 7,
            }],
            details_availability: SavedDataAvailability::Ready,
            details_revision: 1,
            details: vec![SavedDetailRecord {
                id: "detail-1".to_owned(),
                given_name: "Priya".to_owned(),
                family_name: String::new(),
                email: String::new(),
                phone: String::new(),
                address: String::new(),
                postcode: String::new(),
                country: String::new(),
            }],
        }
    }

    #[test]
    fn accepts_one_bounded_snapshot_and_projects_no_secret_field() {
        let mut state = SavedDataState::loading();
        assert_eq!(state.replace(command(), false), Ok(()));
        assert_eq!(state.sign_ins().revision, 1);
        assert_eq!(state.details().people[0].given_name, "Priya");
    }

    #[test]
    fn private_profiles_refuse_even_an_unavailable_replacement() {
        let mut state = SavedDataState::unavailable();
        assert_eq!(
            state.replace(command(), true),
            Err(SavedDataSnapshotError::PrivateProfile)
        );
        assert_eq!(state, SavedDataState::unavailable());
    }

    #[test]
    fn duplicate_ids_malformed_hosts_and_revision_reuse_fail_closed() {
        let mut state = SavedDataState::loading();
        assert_eq!(state.replace(command(), false), Ok(()));

        let mut forged = command();
        forged.sign_ins[0].site = "https://accounts.example/path".to_owned();
        assert_eq!(
            state.replace(forged, false),
            Err(SavedDataSnapshotError::InvalidRecord)
        );

        let mut changed_same_revision = command();
        changed_same_revision.sign_ins[0].username = "other-sensitive-user".to_owned();
        assert_eq!(
            state.replace(changed_same_revision, false),
            Err(SavedDataSnapshotError::StaleRevision)
        );
    }

    #[test]
    fn refuses_a_timestamp_android_cannot_represent() {
        let mut malformed = command();
        malformed.sign_ins[0].last_used_epoch_ms = u64::MAX;
        assert_eq!(
            SavedDataState::loading().replace(malformed, false),
            Err(SavedDataSnapshotError::InvalidRecord)
        );
    }

    #[test]
    fn non_ready_snapshots_carry_neither_rows_nor_revision() {
        let mut invalid = command();
        invalid.sign_ins_availability = SavedDataAvailability::Unavailable;
        assert_eq!(
            SavedDataState::loading().replace(invalid, false),
            Err(SavedDataSnapshotError::InvalidAvailability)
        );
    }

    #[test]
    fn unavailable_transition_keeps_revision_high_water() {
        let mut state = SavedDataState::loading();
        let mut initial = command();
        initial.sign_ins_revision = 4;
        initial.details_revision = 4;
        assert_eq!(state.replace(initial, false), Ok(()));

        let unavailable = ReplaceSavedDataSnapshotCommand {
            sign_ins_availability: SavedDataAvailability::Unavailable,
            sign_ins_revision: 0,
            sign_ins: Vec::new(),
            details_availability: SavedDataAvailability::Unavailable,
            details_revision: 0,
            details: Vec::new(),
        };
        assert_eq!(state.replace(unavailable, false), Ok(()));

        assert_eq!(
            state.replace(command(), false),
            Err(SavedDataSnapshotError::StaleRevision)
        );
        let mut recovered = command();
        recovered.sign_ins_revision = 5;
        recovered.details_revision = 5;
        assert_eq!(state.replace(recovered, false), Ok(()));
    }
}
