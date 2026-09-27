// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Admission of account commands before their exact effect is tracked.

use core_service_types as wire;

use super::AccountServiceError;
use crate::account::{AccountEffect, AccountError};
use crate::codec::account_wire::{
    decode_auth_callback, decode_email_link, decode_native_credential, decode_start_auth,
    AccountStart,
};
use crate::ports::AccountPort;

pub(super) enum BeginCommand {
    Effect(AccountEffect),
    Terminal { state_changed: bool },
}

pub(super) fn begin(
    account: &mut dyn AccountPort,
    command: &wire::CoreServiceCommand,
    now_millis: u64,
) -> Result<BeginCommand, AccountServiceError> {
    let effect = match command.kind {
        wire::CoreServiceCommandKind::StartAuth => {
            let body = command
                .start_auth
                .as_ref()
                .ok_or(AccountServiceError::InvalidCommand)?;
            match decode_start_auth(body, &command.operation)? {
                AccountStart::Pkce(intent) => account.begin_authorization(intent, now_millis)?,
                AccountStart::Native {
                    flow_id,
                    auth_method,
                    deadline,
                } => account.begin_native_authorization(
                    flow_id,
                    auth_method,
                    deadline,
                    now_millis,
                )?,
            }
        }
        wire::CoreServiceCommandKind::RequestEmailLink => {
            let body = command
                .request_email_link
                .as_ref()
                .ok_or(AccountServiceError::InvalidCommand)?;
            account.begin_authorization(decode_email_link(body, &command.operation)?, now_millis)?
        }
        wire::CoreServiceCommandKind::AuthCallback => {
            let body = command
                .auth_callback
                .as_ref()
                .ok_or(AccountServiceError::InvalidCommand)?;
            return begin_auth_callback(account, body, now_millis);
        }
        wire::CoreServiceCommandKind::AuthCredentialResult => {
            let body = command
                .auth_credential_result
                .as_ref()
                .ok_or(AccountServiceError::InvalidCommand)?;
            let (flow_id, method, outcome) = decode_native_credential(body)?;
            account.accept_native_credential(&flow_id, method, outcome, now_millis)?
        }
        wire::CoreServiceCommandKind::SignOut => {
            let body = command
                .sign_out
                .as_ref()
                .ok_or(AccountServiceError::InvalidCommand)?;
            return begin_sign_out(account, body);
        }
        wire::CoreServiceCommandKind::StartTask
        | wire::CoreServiceCommandKind::CancelTask
        | wire::CoreServiceCommandKind::UserDecision
        | wire::CoreServiceCommandKind::PermissionResult
        | wire::CoreServiceCommandKind::CorrectWorkspaceFact
        | wire::CoreServiceCommandKind::ExcludeWorkspaceSource
        | wire::CoreServiceCommandKind::RequestWorkspaceExport
        | wire::CoreServiceCommandKind::SaveWorkspace
        | wire::CoreServiceCommandKind::RenameWorkspace
        | wire::CoreServiceCommandKind::DeleteWorkspace
        | wire::CoreServiceCommandKind::DiscardWorkspace
        | wire::CoreServiceCommandKind::SearchLibrary
        | wire::CoreServiceCommandKind::SaveLibraryFact
        | wire::CoreServiceCommandKind::RemoveLibraryEntry
        | wire::CoreServiceCommandKind::RequestLibraryExport
        | wire::CoreServiceCommandKind::SearchMemory
        | wire::CoreServiceCommandKind::UpsertMemory
        | wire::CoreServiceCommandKind::DeleteMemory
        | wire::CoreServiceCommandKind::AcceptTaskArtifact
        | wire::CoreServiceCommandKind::ExportTaskArtifact
        | wire::CoreServiceCommandKind::SetAssetDeliveryPolicy
        | wire::CoreServiceCommandKind::RequestAsset
        | wire::CoreServiceCommandKind::RemoveAsset
        | wire::CoreServiceCommandKind::SaveProviderCredential
        | wire::CoreServiceCommandKind::ForgetProviderCredential
        | wire::CoreServiceCommandKind::StartProviderAuth
        | wire::CoreServiceCommandKind::ProviderAuthCallback
        | wire::CoreServiceCommandKind::SaveCustomProvider
        | wire::CoreServiceCommandKind::RemoveCustomProvider
        | wire::CoreServiceCommandKind::SetProviderCredentialState
        | wire::CoreServiceCommandKind::SetProviderModelPreference
        | wire::CoreServiceCommandKind::ProbeProviderCredential
        | wire::CoreServiceCommandKind::ProbeCustomEndpoint
        | wire::CoreServiceCommandKind::RequestComposerCompletion
        | wire::CoreServiceCommandKind::CancelComposerCompletion
        | wire::CoreServiceCommandKind::PauseTask
        | wire::CoreServiceCommandKind::ResumeTask
        | wire::CoreServiceCommandKind::TakeOver
        | wire::CoreServiceCommandKind::SetAssistantConfiguration
        | wire::CoreServiceCommandKind::CompleteHandover
        | wire::CoreServiceCommandKind::ExpireHandover
        | wire::CoreServiceCommandKind::SupplyUserInput
        | wire::CoreServiceCommandKind::FollowUp
        | wire::CoreServiceCommandKind::SupplyFieldValues
        | wire::CoreServiceCommandKind::ReplaceSavedDataSnapshot
        | wire::CoreServiceCommandKind::MutateSkill
        | wire::CoreServiceCommandKind::CancelProviderAuth => {
            return Err(AccountServiceError::InvalidCommand);
        }
    };
    Ok(BeginCommand::Effect(effect))
}

fn begin_sign_out(
    account: &mut dyn AccountPort,
    body: &wire::SignOutCommand,
) -> Result<BeginCommand, AccountServiceError> {
    if let Some(expected_subject) = body.account_subject.as_deref() {
        if account
            .session()
            .is_none_or(|session| session.account_subject.as_str() != expected_subject)
        {
            return Err(AccountServiceError::InvalidCommand);
        }
    }
    Ok(match account.begin_sign_out()? {
        Some(effect) => BeginCommand::Effect(effect),
        None => BeginCommand::Terminal {
            state_changed: false,
        },
    })
}

fn begin_auth_callback(
    account: &mut dyn AccountPort,
    body: &wire::AuthCallbackCommand,
    now_millis: u64,
) -> Result<BeginCommand, AccountServiceError> {
    let receipt = decode_auth_callback(body)?;
    match account.accept_redirect(&receipt, now_millis) {
        Ok(effect) => Ok(BeginCommand::Effect(effect)),
        Err(
            AccountError::AccessDenied
            | AccountError::ProviderError
            | AccountError::DeadlineExceeded
            | AccountError::SurfaceUnavailable,
        ) => Ok(BeginCommand::Terminal {
            state_changed: true,
        }),
        Err(error) => Err(error.into()),
    }
}
