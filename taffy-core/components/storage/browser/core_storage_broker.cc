// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/core_storage_broker.h"

#include <stdint.h>

#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "taffy/components/storage/browser/core_storage_backend.h"

namespace taffy {

namespace mojom = core_service::mojom;

namespace {

void CompleteTaskJournalAppend(
    std::unique_ptr<TaskJournalAppendCallback> callback,
    bool committed) {
  CHECK(callback);
  callback->Run(committed);
}

// The revision this commit landed at, per operation shape.
//
// A workspace deletion carries its own record, `workspace_deletion`, with its
// own resulting revision, and this selector did not name it. `workspace` is
// null for a deletion, so the answer fell through to the envelope's top-level
// field — which the deletion path never sets, so it was zero. The core stages
// a deletion at `expected + 1` and compares, so `complete_deletion` answered
// WrongCompletion, no plane claimed the completion, and the deletion stayed
// pending for the life of the process: a person who confirmed "Discard
// temporary workspace" saw the dialog close, the button stay, a row reading
// "Working on it…" that never resolved, and nothing deleted.
//
// `kDeleteSource` deliberately still reads `workspace`: that operation carries
// a persist record beside the source it removes, and its revision is that
// record's.
uint64_t CommittedRevisionFor(const mojom::StorageCommitEffect& commit) {
  if (commit.operation_kind == mojom::StorageOperation::kDeleteWorkspace) {
    return commit.workspace_deletion ? commit.workspace_deletion->resulting_revision
                                     : 0u;
  }
  if (commit.operation_kind == mojom::StorageOperation::kAppendTaskCommit ||
      !commit.workspace) {
    return commit.resulting_revision;
  }
  return commit.workspace->resulting_revision;
}

}  // namespace

CoreStorageBroker::CoreStorageBroker(base::FilePath database_path,
                                     bool ephemeral)
    : backend_(base::ThreadPool::CreateSequencedTaskRunner(
                   {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
                    base::TaskShutdownBehavior::BLOCK_SHUTDOWN}),
               std::move(database_path),
               ephemeral) {}

CoreStorageBroker::~CoreStorageBroker() = default;

void CoreStorageBroker::LoadBootstrap(uint64_t generation,
                                      bool private_profile,
                                      BootstrapCallback callback) {
  backend_.AsyncCall(&Backend::LoadBootstrap)
      .WithArgs(generation, private_profile)
      .Then(std::move(callback));
}

void CoreStorageBroker::LoadAccountReconciliationState(
    AccountReconciliationCallback callback) {
  backend_.AsyncCall(&Backend::LoadAccountReconciliationState)
      .Then(std::move(callback));
}

void CoreStorageBroker::FinishAccountReconciliation(JournalCallback callback) {
  backend_.AsyncCall(&Backend::FinishAccountReconciliation)
      .Then(std::move(callback));
}

void CoreStorageBroker::CommitIntent(const mojom::EffectEnvelope& effect,
                                     JournalCallback callback) {
  backend_.AsyncCall(&Backend::CommitIntent)
      .WithArgs(effect.Clone())
      .Then(std::move(callback));
}

void CoreStorageBroker::CommitResult(const mojom::EffectResult& result,
                                     JournalCallback callback) {
  backend_.AsyncCall(&Backend::CommitResult)
      .WithArgs(result.Clone())
      .Then(std::move(callback));
}

void CoreStorageBroker::RecordDispatching(
    DispatchIntentRecord record,
    std::unique_ptr<TaskJournalAppendCallback> callback) {
  backend_.AsyncCall(&Backend::AppendTaskActionIntent)
      .WithArgs(std::move(record))
      .Then(base::BindOnce(&CompleteTaskJournalAppend, std::move(callback)));
}

void CoreStorageBroker::RecordTerminalResult(
    ActionResult result,
    std::unique_ptr<TaskJournalAppendCallback> callback) {
  backend_.AsyncCall(&Backend::AppendTaskActionResult)
      .WithArgs(std::move(result))
      .Then(base::BindOnce(&CompleteTaskJournalAppend, std::move(callback)));
}

void CoreStorageBroker::ReadTaskActionJournal(
    DispatchId dispatch_id,
    TaskId task_id,
    ActionId action_id,
    TaskActionLookupCallback callback) {
  backend_.AsyncCall(&Backend::ReadTaskActionJournal)
      .WithArgs(std::move(dispatch_id), std::move(task_id),
                std::move(action_id))
      .Then(std::move(callback));
}

void CoreStorageBroker::DispatchStorage(mojom::EffectEnvelopePtr effect,
                                        CompletionCallback callback) {
  mojom::EffectEnvelopePtr reply_source = effect.Clone();
  backend_.AsyncCall(&Backend::CommitStorage)
      .WithArgs(std::move(effect))
      .Then(base::BindOnce(&CoreStorageBroker::OnStorageCommitted,
                           weak_factory_.GetWeakPtr(), std::move(reply_source),
                           std::move(callback)));
}

void CoreStorageBroker::OnStorageCommitted(mojom::EffectEnvelopePtr effect,
                                           CompletionCallback callback,
                                           bool committed) {
  auto result = mojom::EffectResult::New();
  result->operation = effect->operation.Clone();
  result->effect_id = effect->effect_id;
  result->status = committed ? mojom::EffectStatus::kCompleted
                             : mojom::EffectStatus::kUnavailable;
  result->kind = mojom::EffectKind::kStorageCommit;
  result->storage = mojom::StorageEffectResult::New();
  result->storage->committed_revision =
      committed && effect->storage_commit
          ? CommittedRevisionFor(*effect->storage_commit)
          : 0u;
  std::move(callback).Run(std::move(result));
}

void CoreStorageBroker::MarkEffectsLost(uint64_t generation,
                                        std::vector<std::string> effect_ids) {
  backend_.AsyncCall(&Backend::MarkEffectsLost)
      .WithArgs(generation, std::move(effect_ids));
}

}  // namespace taffy
