// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::collections::{BTreeMap, BTreeSet};

use crate::BrowserSessionId;

use super::{
    TaskDownloadActionResult, TaskDownloadResultError, TaskDownloadSnapshot, TaskDownloadState,
    TaskDownloadTranscriptEntry, TaskDownloadTranscriptOutcome, MAX_TASK_DOWNLOAD_RESULTS,
};

#[derive(Clone, Debug, PartialEq, Eq)]
struct TaskDownloadBinding {
    snapshot: TaskDownloadSnapshot,
    started_by_task: bool,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct TaskDownloadHandleTable {
    browser_session_id: Option<BrowserSessionId>,
    bindings: BTreeMap<u32, TaskDownloadBinding>,
    truncated: bool,
}

impl TaskDownloadHandleTable {
    pub fn resolve(&self, handle: u32) -> Option<&TaskDownloadSnapshot> {
        self.bindings.get(&handle).map(|binding| &binding.snapshot)
    }

    pub fn resolve_task_started(&self, handle: u32) -> Option<&TaskDownloadSnapshot> {
        self.bindings
            .get(&handle)
            .filter(|binding| binding.started_by_task)
            .map(|binding| &binding.snapshot)
    }

    pub const fn browser_session_id(&self) -> Option<&BrowserSessionId> {
        self.browser_session_id.as_ref()
    }

    pub fn is_empty(&self) -> bool {
        self.bindings.is_empty()
    }

    /// Downloads this task started whose latest verified browser snapshot is
    /// complete. A listed download with no task-start binding cannot witness
    /// this task's completion, even if it is an existing completed file.
    pub fn completed_task_downloads(&self) -> usize {
        self.bindings
            .values()
            .filter(|binding| {
                binding.started_by_task && binding.snapshot.state == TaskDownloadState::Complete
            })
            .count()
    }

    /// Only task-owned transfers still progressing may justify another poll.
    /// A missing session or truncated list cannot provide a complete witness.
    pub fn pending_task_downloads(&self) -> Option<usize> {
        (self.browser_session_id.is_some() && !self.truncated).then(|| {
            self.bindings
                .values()
                .filter(|binding| {
                    binding.started_by_task
                        && matches!(
                            binding.snapshot.state,
                            TaskDownloadState::Created | TaskDownloadState::InProgress
                        )
                })
                .count()
        })
    }

    pub fn apply_verified(
        &mut self,
        result: &TaskDownloadActionResult,
    ) -> Result<TaskDownloadTranscriptOutcome, TaskDownloadResultError> {
        if self
            .browser_session_id
            .as_ref()
            .is_some_and(|current| current != result.browser_session_id())
        {
            return Err(TaskDownloadResultError::WrongBrowserSession);
        }
        match result {
            TaskDownloadActionResult::Started {
                browser_session_id,
                download,
            } => {
                self.browser_session_id = Some(browser_session_id.clone());
                let handle = self.bind_task_started(download.clone())?;
                Ok(TaskDownloadTranscriptOutcome::Started {
                    handle,
                    state: download.state,
                })
            }
            TaskDownloadActionResult::Listed {
                browser_session_id,
                downloads,
                truncated,
            } => self.apply_list(browser_session_id, downloads, *truncated),
            TaskDownloadActionResult::Cancelled {
                browser_session_id,
                download,
            } => self.apply_cancelled(browser_session_id, download),
        }
    }

    fn apply_list(
        &mut self,
        browser_session_id: &BrowserSessionId,
        downloads: &[TaskDownloadSnapshot],
        truncated: bool,
    ) -> Result<TaskDownloadTranscriptOutcome, TaskDownloadResultError> {
        TaskDownloadActionResult::validate_list(downloads, truncated)?;
        self.browser_session_id = Some(browser_session_id.clone());
        self.truncated = truncated;
        let task_started: BTreeSet<_> = self
            .bindings
            .values()
            .filter(|binding| binding.started_by_task)
            .map(|binding| binding.snapshot.download_id.clone())
            .collect();
        self.bindings.clear();
        let mut transcript = Vec::with_capacity(downloads.len());
        for (index, download) in downloads.iter().cloned().enumerate() {
            let handle = u32::try_from(index)
                .ok()
                .and_then(|value| value.checked_add(1))
                .ok_or(TaskDownloadResultError::TooManyDownloads)?;
            transcript.push(TaskDownloadTranscriptEntry {
                handle,
                state: download.state,
            });
            let started_by_task = task_started.contains(&download.download_id);
            self.bindings.insert(
                handle,
                TaskDownloadBinding {
                    snapshot: download,
                    started_by_task,
                },
            );
        }
        Ok(TaskDownloadTranscriptOutcome::Listed {
            downloads: transcript,
            truncated,
        })
    }

    fn apply_cancelled(
        &mut self,
        browser_session_id: &BrowserSessionId,
        download: &TaskDownloadSnapshot,
    ) -> Result<TaskDownloadTranscriptOutcome, TaskDownloadResultError> {
        if download.state != TaskDownloadState::Cancelled {
            return Err(TaskDownloadResultError::InvalidCancellation);
        }
        self.browser_session_id = Some(browser_session_id.clone());
        let Some((_, binding)) = self.bindings.iter_mut().find(|(_, binding)| {
            binding.started_by_task && binding.snapshot.download_id == download.download_id
        }) else {
            return Err(TaskDownloadResultError::InvalidCancellation);
        };
        binding.snapshot = download.clone();
        Ok(TaskDownloadTranscriptOutcome::Cancelled)
    }

    fn bind_task_started(
        &mut self,
        download: TaskDownloadSnapshot,
    ) -> Result<u32, TaskDownloadResultError> {
        if let Some((&handle, binding)) = self
            .bindings
            .iter_mut()
            .find(|(_, binding)| binding.snapshot.download_id == download.download_id)
        {
            binding.snapshot = download;
            binding.started_by_task = true;
            return Ok(handle);
        }
        let handle = self.reusable_handle()?;
        self.bindings.insert(
            handle,
            TaskDownloadBinding {
                snapshot: download,
                started_by_task: true,
            },
        );
        Ok(handle)
    }

    fn reusable_handle(&self) -> Result<u32, TaskDownloadResultError> {
        (1..=u32::try_from(MAX_TASK_DOWNLOAD_RESULTS)
            .map_err(|_| TaskDownloadResultError::TooManyDownloads)?)
            .find(|candidate| !self.bindings.contains_key(candidate))
            .or_else(|| {
                self.bindings
                    .iter()
                    .find(|(_, binding)| !binding.started_by_task)
                    .map(|(&handle, _)| handle)
            })
            .or_else(|| {
                self.bindings
                    .iter()
                    .find(|(_, binding)| {
                        matches!(
                            binding.snapshot.state,
                            TaskDownloadState::Complete
                                | TaskDownloadState::Interrupted
                                | TaskDownloadState::Cancelled
                        )
                    })
                    .map(|(&handle, _)| handle)
            })
            .or_else(|| self.bindings.keys().next().copied())
            .ok_or(TaskDownloadResultError::TooManyDownloads)
    }
}
