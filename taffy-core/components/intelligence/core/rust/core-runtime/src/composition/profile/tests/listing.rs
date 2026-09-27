// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A provider that serves its own model list, read into the same picker.

use core_service_types as wire;

use super::{built_runtime, save_provider_credential_command};

#[test]
fn a_listing_is_planned_only_for_a_connected_provider_that_serves_one() {
    let mut runtime = built_runtime();
    assert!(
        runtime.plan_provider_listing_refresh(1_000).is_none(),
        "no credential, no fetch"
    );
    assert!(runtime
        .submit_provider_command(&save_provider_credential_command("openrouter", "handle-1"))
        .is_ok());
    let effect = runtime
        .plan_provider_listing_refresh(1_000)
        .unwrap_or_else(|| unreachable!("a connected aggregator is due"));
    assert_eq!(effect.kind, wire::EffectKind::FetchProviderListing);
    let fetch = effect
        .provider_listing_fetch
        .unwrap_or_else(|| unreachable!());
    assert_eq!(fetch.provider_id, "openrouter");
    assert_eq!(fetch.credential_handle.as_deref(), Some("handle-1"));
    assert!(
        runtime.plan_provider_listing_refresh(1_000).is_none(),
        "one fetch in flight, never two"
    );
}

#[test]
fn a_fetched_listing_reaches_the_picker_like_any_other_model() {
    use crate::provider_listing::ProviderListingVerdict;

    let mut runtime = built_runtime();
    assert!(runtime
        .submit_provider_command(&save_provider_credential_command("openrouter", "handle-1"))
        .is_ok());
    assert!(runtime.plan_provider_listing_refresh(1_000).is_some());
    let body = concat!(
        r#"{"data":[{"id":"vendor/alpha","context_length":131072,"#,
        r#""max_completion_tokens":8192,"supported_parameters":["tools"],"#,
        r#""pricing":{"prompt":"0.0000012","completion":"0.000006"}}]}"#
    );
    let verdict = runtime.deliver_provider_listing_result(
        &wire::ProviderListingFetchResult {
            provider_id: "openrouter".to_owned(),
            disposition: wire::CatalogFetchDisposition::Success,
            body: body.as_bytes().to_vec(),
        },
        1_000,
    );
    assert_eq!(
        verdict,
        ProviderListingVerdict::Accepted {
            provider_id: "openrouter".to_owned(),
            offered: 1,
            kept: 1,
        }
    );
    let status = runtime
        .project_core_status(core_api_types::CoreAvailability::Ready)
        .unwrap_or_else(|_| unreachable!());
    assert!(
        status
            .provider_models
            .iter()
            .any(|entry| entry.provider_id == "openrouter" && entry.model_id == "vendor/alpha"),
        "a fetched model reaches every surface through the roster like any other"
    );
}

#[test]
fn a_listing_goes_when_the_credential_it_was_fetched_with_is_forgotten() {
    let mut runtime = built_runtime();
    assert!(runtime
        .submit_provider_command(&save_provider_credential_command("openrouter", "handle-1"))
        .is_ok());
    assert!(runtime.plan_provider_listing_refresh(1_000).is_some());
    let body = concat!(
        r#"{"data":[{"id":"vendor/alpha","supported_parameters":["tools"],"#,
        r#""pricing":{"prompt":"0.000001","completion":"0.000001"}}]}"#
    );
    assert!(matches!(
        runtime.deliver_provider_listing_result(
            &wire::ProviderListingFetchResult {
                provider_id: "openrouter".to_owned(),
                disposition: wire::CatalogFetchDisposition::Success,
                body: body.as_bytes().to_vec(),
            },
            1_000,
        ),
        crate::provider_listing::ProviderListingVerdict::Accepted { .. }
    ));
    let mut forget = save_provider_credential_command("openrouter", "handle-1");
    forget.kind = wire::CoreServiceCommandKind::ForgetProviderCredential;
    forget.save_provider_credential = None;
    forget.forget_provider_credential = Some(wire::ForgetProviderCredentialCommand {
        provider_id: "openrouter".to_owned(),
    });
    assert!(runtime.submit_provider_command(&forget).is_ok());
    let status = runtime
        .project_core_status(core_api_types::CoreAvailability::Ready)
        .unwrap_or_else(|_| unreachable!());
    assert!(
        !status
            .provider_models
            .iter()
            .any(|entry| entry.provider_id == "openrouter"),
        "a per-account list stops being about anybody when the account's key goes"
    );
}

#[test]
fn a_listing_at_its_bound_leaves_every_published_provider_in_the_picker() {
    // The Core API status carries one flat model list and the projection
    // cuts it in identity order when it overflows. A listing allowed to
    // fill that budget would push every provider sorting after it out of
    // the picker, on every device and without a word — which is why the
    // listing's own bound sits far inside the surface's.
    let mut runtime = built_runtime();
    assert!(runtime
        .submit_provider_command(&save_provider_credential_command("openrouter", "handle-1"))
        .is_ok());
    assert!(runtime.plan_provider_listing_refresh(1_000).is_some());
    let rows: Vec<String> = (0..crate::provider_listing::MAX_LISTED_MODELS)
        .map(|index| {
            format!(
                concat!(
                    r#"{{"id":"vendor/m{index:04}","supported_parameters":["tools"],"#,
                    r#""pricing":{{"prompt":"0.000001","completion":"0.000001"}}}}"#
                ),
                index = index
            )
        })
        .collect();
    let body = format!(r#"{{"data":[{}]}}"#, rows.join(","));
    assert!(matches!(
        runtime.deliver_provider_listing_result(
            &wire::ProviderListingFetchResult {
                provider_id: "openrouter".to_owned(),
                disposition: wire::CatalogFetchDisposition::Success,
                body: body.into_bytes(),
            },
            1_000,
        ),
        crate::provider_listing::ProviderListingVerdict::Accepted { .. }
    ));
    let status = runtime
        .project_core_status(core_api_types::CoreAvailability::Ready)
        .unwrap_or_else(|_| unreachable!());
    assert!(
        status.provider_models.len() <= core_api_types::MAX_PROVIDER_MODEL_ENTRIES,
        "the projection must fit the payload the codec will encode"
    );
    assert!(
        status
            .provider_models
            .iter()
            .any(|entry| entry.provider_id == "xai"),
        "a vendor sorting after the aggregator must still be offered"
    );
    // And the whole snapshot still encodes, which is what a surface reads.
    assert!(runtime.encode_ready_core_status().is_ok());
}
