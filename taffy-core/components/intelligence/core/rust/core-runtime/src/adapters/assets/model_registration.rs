// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Installed catalog rows projected for the browser-owned model register.

use asset_plane::Kind;
use core_service_types as wire;

use crate::ports::AssetDeliveryError;

use super::{key, translate, ProductionAssetDelivery};

impl ProductionAssetDelivery {
    pub(super) fn build_model_artifact_registrations(
        &self,
    ) -> Result<Vec<wire::ModelArtifactRegistration>, AssetDeliveryError> {
        let mut registrations = Vec::new();
        for entry in self.catalog.entries() {
            let Some(model) = entry.model() else {
                continue;
            };
            if !entry.kind().is_model()
                || (entry.kind() == Kind::ModelTokenizer && model.is_adapter())
            {
                return Err(AssetDeliveryError::InvalidModelArtifact {
                    asset_id: entry.id().to_owned(),
                    asset_revision: entry.revision().to_owned(),
                });
            }
            let Some(variant) = entry.variant(self.policy.platform) else {
                continue;
            };
            let Some(state) = self.states.get(&key(entry.id(), entry.revision())) else {
                continue;
            };
            if !state.is_installed() {
                continue;
            }

            // An installed directory is a filesystem observation, not proof
            // that the file still has the catalog's shape. A truncated or
            // replaced artifact is excluded here and rejected again when the
            // browser hashes the opened descriptor. Publishing it as installed
            // would turn the path layout into an integrity verdict.
            let byte_length = variant.installed_bytes();
            let Some(digest) = variant.digest() else {
                return Err(AssetDeliveryError::InvalidModelArtifact {
                    asset_id: entry.id().to_owned(),
                    asset_revision: entry.revision().to_owned(),
                });
            };
            if state.progress.written_bytes != byte_length {
                continue;
            }
            let byte_length_is_supported = usize::try_from(byte_length)
                .is_ok_and(|length| length <= wire::MAX_TOOL_MODEL_ARTIFACT_BYTES);
            if !variant.is_fetchable() || byte_length == 0 || !byte_length_is_supported {
                return Err(AssetDeliveryError::InvalidModelArtifact {
                    asset_id: entry.id().to_owned(),
                    asset_revision: entry.revision().to_owned(),
                });
            }
            registrations.push(wire::ModelArtifactRegistration {
                asset_id: entry.id().to_owned(),
                asset_revision: entry.revision().to_owned(),
                asset_kind: translate::kind_to_wire(entry.kind()),
                format: translate::model_format_to_wire(model.format()),
                adapter: model.is_adapter(),
                byte_length,
                digest: *digest.as_bytes(),
            });
        }
        if registrations.len() > wire::MAX_REGISTERED_MODEL_ARTIFACTS {
            return Err(AssetDeliveryError::TooManyModelArtifacts {
                count: registrations.len(),
            });
        }
        Ok(registrations)
    }
}
