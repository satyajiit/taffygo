// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use bip_types::identity::{
    ApprovalReceiptReference, ContentDigest, DigestAlgorithm, MonotonicMillis,
};
use bip_types::sensitivity::{Sensitivity, SensitivitySet};
use core_service_types as wire;
use policy_engine::{ActionClass, ApprovalFact, RiskClass};

use super::super::PolicyWireError;

pub(super) fn approval(input: &wire::PolicyApprovalFact) -> Result<ApprovalFact, PolicyWireError> {
    Ok(ApprovalFact {
        receipt: ApprovalReceiptReference::new(
            identifier(&input.receipt_reference).map_err(|_| PolicyWireError::InvalidApproval)?,
        ),
        proposal_digest: digest(&input.proposal_digest)
            .map_err(|_| PolicyWireError::InvalidApproval)?,
        service_generation: input.service_generation,
        expires_at: MonotonicMillis(input.expires_at_monotonic_ms),
        expires_at_utc_ms: input.expires_at_utc_ms,
        browser_session_id: identifier(&input.browser_session_id)
            .map_err(|_| PolicyWireError::InvalidApproval)?,
    })
}

pub(super) fn digest(value: &str) -> Result<ContentDigest, PolicyWireError> {
    if value.len() != 64
        || !value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
    {
        return Err(PolicyWireError::InvalidDigest);
    }
    Ok(ContentDigest {
        algorithm: DigestAlgorithm::Sha256,
        value: value.to_owned(),
    })
}

pub(super) fn sensitivities(
    values: &[wire::BipSensitivity],
) -> Result<SensitivitySet, PolicyWireError> {
    if values.len() > Sensitivity::ALL.len() {
        return Err(PolicyWireError::InvalidDataClasses);
    }
    let mut decoded = Vec::with_capacity(values.len());
    for value in values {
        let sensitivity = sensitivity(*value);
        if decoded.contains(&sensitivity)
            || (sensitivity == Sensitivity::NotSensitive && values.len() != 1)
        {
            return Err(PolicyWireError::InvalidDataClasses);
        }
        decoded.push(sensitivity);
    }
    Ok(decoded.into_iter().collect())
}

pub(super) fn identifier(value: &str) -> Result<String, PolicyWireError> {
    if value.is_empty()
        || value.len() > wire::MAX_IDENTIFIER_BYTES
        || value.chars().any(char::is_control)
    {
        return Err(PolicyWireError::InvalidIdentity);
    }
    Ok(value.to_owned())
}

pub(super) const fn action_class(value: wire::PolicyActionClass) -> ActionClass {
    match value {
        wire::PolicyActionClass::ObservePage => ActionClass::ObservePage,
        wire::PolicyActionClass::ScrollIntoView => ActionClass::ScrollIntoView,
        wire::PolicyActionClass::OpenLink => ActionClass::OpenLink,
        wire::PolicyActionClass::CreateTaskTab => ActionClass::CreateTaskTab,
        wire::PolicyActionClass::SyntheticClick => ActionClass::SyntheticClick,
        wire::PolicyActionClass::MoveFocus => ActionClass::MoveFocus,
        wire::PolicyActionClass::FillField => ActionClass::FillField,
        wire::PolicyActionClass::SelectOption => ActionClass::SelectOption,
        wire::PolicyActionClass::ToggleControl => ActionClass::ToggleControl,
        wire::PolicyActionClass::SubmitForm => ActionClass::SubmitForm,
        wire::PolicyActionClass::StartDownload => ActionClass::StartDownload,
        wire::PolicyActionClass::UploadFile => ActionClass::UploadFile,
        wire::PolicyActionClass::SendMessage => ActionClass::SendMessage,
        wire::PolicyActionClass::Purchase => ActionClass::Purchase,
        wire::PolicyActionClass::ExtractCredential => ActionClass::ExtractCredential,
        wire::PolicyActionClass::BypassAccessControl => ActionClass::BypassAccessControl,
        wire::PolicyActionClass::ExecuteToolJob => ActionClass::ExecuteToolJob,
        wire::PolicyActionClass::LibraryRead => ActionClass::LibraryRead,
        wire::PolicyActionClass::LibraryWrite => ActionClass::LibraryWrite,
        wire::PolicyActionClass::MemoryRead => ActionClass::MemoryRead,
        wire::PolicyActionClass::MemoryWrite => ActionClass::MemoryWrite,
        wire::PolicyActionClass::ControlTab => ActionClass::ControlTab,
        wire::PolicyActionClass::ProfileStoreRead => ActionClass::ProfileStoreRead,
    }
}

pub(super) const fn risk(value: wire::PolicyRiskClass) -> RiskClass {
    match value {
        wire::PolicyRiskClass::LocalRead => RiskClass::LocalRead,
        wire::PolicyRiskClass::ReversibleDisclosure => RiskClass::ReversibleDisclosure,
        wire::PolicyRiskClass::SensitiveDisclosure => RiskClass::SensitiveDisclosure,
        wire::PolicyRiskClass::ExcludedCommitment => RiskClass::ExcludedCommitment,
        wire::PolicyRiskClass::ProhibitedAbuse => RiskClass::ProhibitedAbuse,
    }
}

const fn sensitivity(value: wire::BipSensitivity) -> Sensitivity {
    match value {
        wire::BipSensitivity::NotSensitive => Sensitivity::NotSensitive,
        wire::BipSensitivity::Personal => Sensitivity::Personal,
        wire::BipSensitivity::Account => Sensitivity::Account,
        wire::BipSensitivity::Payment => Sensitivity::Payment,
        wire::BipSensitivity::Identity => Sensitivity::Identity,
        wire::BipSensitivity::Health => Sensitivity::Health,
        wire::BipSensitivity::Financial => Sensitivity::Financial,
        wire::BipSensitivity::Legal => Sensitivity::Legal,
        wire::BipSensitivity::PrivateCommunication => Sensitivity::PrivateCommunication,
        wire::BipSensitivity::Administration => Sensitivity::Administration,
        wire::BipSensitivity::Credential => Sensitivity::Credential,
        wire::BipSensitivity::UnknownSensitive => Sensitivity::UnknownSensitive,
        wire::BipSensitivity::OneTimeCode => Sensitivity::OneTimeCode,
        wire::BipSensitivity::ChallengeResponse => Sensitivity::ChallengeResponse,
    }
}
