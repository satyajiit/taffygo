// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Canonical reducer construction for fresh tasks and committed replay.

use std::rc::Rc;
use task_engine::{BudgetDefaults, Recovery, Reducer, ReplayError, TaskJournal, TaskSeed};

use super::ids::DerivedTaskIds;
use super::time::ServiceClock;
use crate::account::Sha256Port;
use crate::ports::{
    apply_initial_consent, OpenedTaskEngine, PortError, TaskEngineFactory, TaskEngineLoad,
    TaskIdEntropy,
};

type ReplayedTask = (Reducer<ServiceClock, DerivedTaskIds>, Recovery);

/// Production task factory using browser entropy and no ambient state.
#[derive(Clone)]
pub struct ProductionTaskFactory {
    clock: ServiceClock,
    missing_budget_defaults: BudgetDefaults,
    digest: Rc<dyn Sha256Port>,
}

impl ProductionTaskFactory {
    /// Missing budget entries fail closed at zero; product commands supply all
    /// reviewed limits explicitly in their frozen task seed.
    pub fn new(clock: ServiceClock, digest: Rc<dyn Sha256Port>) -> Self {
        Self {
            clock,
            missing_budget_defaults: BudgetDefaults::uniform(0),
            digest,
        }
    }
}

impl ProductionTaskFactory {
    /// Why a committed journal does not replay, in compiled-in words, or
    /// `None` when it does.
    ///
    /// For a refusal already taken and nothing else. [`TaskEngineFactory`]
    /// answers a replay it refused with a bare [`PortError::Rejected`], so the
    /// log line beside a refused restore named no reason at all; replay is
    /// deterministic, so running it again over the same load gives the reason
    /// the first run refused for (decision 0235).
    pub fn replay_refusal(&self, load: TaskEngineLoad) -> Option<&'static str> {
        match load {
            TaskEngineLoad::Replay {
                seed,
                journal,
                id_entropy,
            } => self.replay(*seed, &journal, &id_entropy).err(),
            TaskEngineLoad::Fresh { .. } => None,
        }
        .map(|error| error.label())
    }

    fn replay(
        &self,
        seed: TaskSeed,
        journal: &TaskJournal,
        id_entropy: &TaskIdEntropy,
    ) -> Result<ReplayedTask, ReplayError> {
        let ids = DerivedTaskIds::new(&seed.task_id, id_entropy, self.digest.clone());
        Reducer::replay(
            seed,
            self.missing_budget_defaults.clone(),
            self.clock.clone(),
            ids,
            journal,
        )
    }
}

impl core::fmt::Debug for ProductionTaskFactory {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        formatter
            .debug_struct("ProductionTaskFactory")
            .field("clock", &self.clock)
            .finish_non_exhaustive()
    }
}

impl TaskEngineFactory for ProductionTaskFactory {
    fn open(&mut self, load: TaskEngineLoad) -> Result<OpenedTaskEngine, PortError> {
        match load {
            TaskEngineLoad::Fresh {
                seed,
                creation_key,
                trace_id,
                id_entropy,
                initial_consent,
            } => {
                let ids = DerivedTaskIds::new(&seed.task_id, &id_entropy, self.digest.clone());
                let mut reducer = Reducer::create(
                    *seed,
                    self.missing_budget_defaults.clone(),
                    self.clock.clone(),
                    ids,
                    creation_key,
                    trace_id,
                );
                let initial_effects = apply_initial_consent(&mut reducer, *initial_consent)?;
                Ok(OpenedTaskEngine::new(
                    Box::new(reducer),
                    None,
                    initial_effects,
                ))
            }
            TaskEngineLoad::Replay {
                seed,
                journal,
                id_entropy,
            } => {
                let (task, recovery) = self
                    .replay(*seed, &journal, &id_entropy)
                    .map_err(|_| PortError::Rejected)?;
                Ok(OpenedTaskEngine::new(
                    Box::new(task),
                    Some(recovery),
                    Vec::new(),
                ))
            }
        }
    }
}
