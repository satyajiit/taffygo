// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Fixed, deterministic workspace evidence time and identifier helpers.

use task_engine::Timestamp;

use crate::ports::WorkspaceExportError;

pub(super) fn decode_id(text: &str) -> Result<[u8; 16], WorkspaceExportError> {
    let mut output = [0u8; 16];
    let bytes = text.as_bytes();
    if bytes.len() != 32 {
        return Err(WorkspaceExportError::InvalidEvidence);
    }
    for (index, slot) in output.iter_mut().enumerate() {
        let offset = index.saturating_mul(2);
        let high = bytes.get(offset).and_then(|byte| hex(*byte));
        let low = bytes
            .get(offset.saturating_add(1))
            .and_then(|byte| hex(*byte));
        *slot = high
            .zip(low)
            .map(|(high, low)| (high << 4) | low)
            .ok_or(WorkspaceExportError::InvalidEvidence)?;
    }
    Ok(output)
}

const fn hex(value: u8) -> Option<u8> {
    match value {
        b'0'..=b'9' => Some(value - b'0'),
        b'a'..=b'f' => Some(value - b'a' + 10),
        _ => None,
    }
}

pub(super) fn timestamp_from_epoch_ms(value: u64) -> Result<Timestamp, WorkspaceExportError> {
    let seconds = value / 1_000;
    if seconds > 253_402_300_799 {
        return Err(WorkspaceExportError::InvalidEvidence);
    }
    let days =
        i64::try_from(seconds / 86_400).map_err(|_| WorkspaceExportError::InvalidEvidence)?;
    let daytime = seconds % 86_400;
    let (year, month, day) = civil_from_days(days);
    let hour = daytime / 3_600;
    let minute = daytime % 3_600 / 60;
    let second = daytime % 60;
    Timestamp::new(&format!(
        "{year:04}-{month:02}-{day:02}T{hour:02}:{minute:02}:{second:02}Z"
    ))
    .map_err(|_| WorkspaceExportError::InvalidEvidence)
}

const fn civil_from_days(days: i64) -> (i64, i64, i64) {
    let shifted = days + 719_468;
    let era = if shifted >= 0 {
        shifted
    } else {
        shifted - 146_096
    } / 146_097;
    let day_of_era = shifted - era * 146_097;
    let year_of_era =
        (day_of_era - day_of_era / 1_460 + day_of_era / 36_524 - day_of_era / 146_096) / 365;
    let mut year = year_of_era + era * 400;
    let day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    let month_prime = (5 * day_of_year + 2) / 153;
    let day = day_of_year - (153 * month_prime + 2) / 5 + 1;
    let month = month_prime + if month_prime < 10 { 3 } else { -9 };
    if month <= 2 {
        year += 1;
    }
    (year, month, day)
}
