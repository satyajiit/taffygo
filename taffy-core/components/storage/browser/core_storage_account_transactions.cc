// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/core_storage_account_transactions.h"

#include <limits>
#include <optional>
#include <string>

#include "base/time/time.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "taffy/components/storage/browser/account_session_expiry.h"

namespace taffy::storage_internal {

namespace mojom = core_service::mojom;

namespace {

const mojom::AccountSessionReceipt *
SessionReceiptForResult(const mojom::NetworkEffectResult &result) {
  switch (result.operation_kind) {
  case mojom::AccountNetworkOperation::kExchangeAuthorizationCode:
    return result.authorization_code_session.get();
  case mojom::AccountNetworkOperation::kExchangeNativeCredential:
    return result.native_credential_session.get();
  case mojom::AccountNetworkOperation::kRefreshSession:
    return result.refreshed_session.get();
  case mojom::AccountNetworkOperation::kRequestEmailLink:
  case mojom::AccountNetworkOperation::kRevokeSession:
  case mojom::AccountNetworkOperation::kFetchEntitlement:
    return nullptr;
  }
}

bool DeleteAccountSessionIfMatching(sql::Database *database,
                                    const std::string &session_handle) {
  sql::Statement remove(database->GetCachedStatement(
      SQL_FROM_HERE, "DELETE FROM core_account_session WHERE singleton=1 AND "
                     "session_handle=?"));
  remove.BindString(0, session_handle);
  if (!remove.Run()) {
    return false;
  }
  if (database->GetLastChangeCount() == 1) {
    return true;
  }
  // Reconciliation is idempotent when SQL is already empty, but a different
  // committed handle is an identity mismatch and must keep the barrier.
  sql::Statement existing(database->GetCachedStatement(
      SQL_FROM_HERE, "SELECT 1 FROM core_account_session WHERE singleton=1"));
  if (existing.Step()) {
    return false;
  }
  return existing.Succeeded();
}

} // namespace

bool IsAccountSessionMutation(const mojom::EffectEnvelope &effect) {
  if (effect.kind != mojom::EffectKind::kNetworkRequest ||
      !effect.network_request) {
    return false;
  }
  switch (effect.network_request->operation_kind) {
  case mojom::AccountNetworkOperation::kExchangeAuthorizationCode:
  case mojom::AccountNetworkOperation::kExchangeNativeCredential:
  case mojom::AccountNetworkOperation::kRefreshSession:
  case mojom::AccountNetworkOperation::kRevokeSession:
    return true;
  case mojom::AccountNetworkOperation::kRequestEmailLink:
  case mojom::AccountNetworkOperation::kFetchEntitlement:
    return false;
  }
}

bool IsAccountSessionMutation(const mojom::EffectResult &result) {
  if (result.kind != mojom::EffectKind::kNetworkRequest || !result.network) {
    return false;
  }
  switch (result.network->operation_kind) {
  case mojom::AccountNetworkOperation::kExchangeAuthorizationCode:
  case mojom::AccountNetworkOperation::kExchangeNativeCredential:
  case mojom::AccountNetworkOperation::kRefreshSession:
  case mojom::AccountNetworkOperation::kRevokeSession:
    return true;
  case mojom::AccountNetworkOperation::kRequestEmailLink:
  case mojom::AccountNetworkOperation::kFetchEntitlement:
    return false;
  }
}

std::optional<bool>
HasAccountSessionPendingMarker(sql::Database *database,
                               const std::string &effect_id) {
  sql::Statement pending(database->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT 1 FROM core_account_session_pending WHERE effect_id=?"));
  pending.BindString(0, effect_id);
  if (pending.Step()) {
    return true;
  }
  return pending.Succeeded() ? std::optional<bool>(false) : std::nullopt;
}

std::optional<bool> HasAnyAccountSessionPendingMarker(sql::Database *database) {
  sql::Statement pending(database->GetCachedStatement(
      SQL_FROM_HERE, "SELECT 1 FROM core_account_session_pending LIMIT 1"));
  if (pending.Step()) {
    return true;
  }
  return pending.Succeeded() ? std::optional<bool>(false) : std::nullopt;
}

bool PersistAccountResult(sql::Database *database,
                          const mojom::EffectResult &result,
                          bool has_pending_session_mutation) {
  if (result.kind != mojom::EffectKind::kNetworkRequest) {
    return true;
  }
  if (!result.network) {
    return !has_pending_session_mutation &&
           result.status != mojom::EffectStatus::kCompleted;
  }
  if (result.network->operation_kind ==
      mojom::AccountNetworkOperation::kRevokeSession) {
    if (!has_pending_session_mutation || !result.network->revoked_session ||
        result.network->revoked_session->session_handle.empty() ||
        result.network->revoked_session->session_handle.size() >
            mojom::kMaxIdentifierBytes) {
      return false;
    }
    // The account broker clears the canonical vault entry for every
    // deterministic revoke terminal, including a provider denial. Only an
    // ambiguous dispatch preserves both SQL and the recovery marker.
    if (result.status == mojom::EffectStatus::kOutcomeUnknown) {
      return !result.network->revoked_session->deleted;
    }
    if ((result.status == mojom::EffectStatus::kCompleted) !=
        result.network->revoked_session->deleted) {
      return false;
    }
    return DeleteAccountSessionIfMatching(
        database, result.network->revoked_session->session_handle);
  }
  if (result.status != mojom::EffectStatus::kCompleted) {
    return true;
  }
  if (result.network->operation_kind ==
      mojom::AccountNetworkOperation::kRequestEmailLink) {
    return result.network->email_link && result.network->email_link->accepted;
  }

  const mojom::AccountSessionReceipt *receipt =
      SessionReceiptForResult(*result.network);
  if (!has_pending_session_mutation || !receipt ||
      receipt->session_handle.empty() ||
      receipt->session_handle.size() > mojom::kMaxIdentifierBytes ||
      receipt->account_subject.empty() ||
      receipt->account_subject.size() > mojom::kMaxIdentifierBytes ||
      receipt->rotation >
          static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    return false;
  }
  const std::optional<base::Time> utc_expiry =
      PersistAccountSessionExpiry(receipt->expires_at_monotonic_ms,
                                  base::Time::Now(), base::TimeTicks::Now());
  if (!utc_expiry) {
    return false;
  }
  const int64_t expiry_ms = utc_expiry->InMillisecondsSinceUnixEpoch();
  if (expiry_ms <= 0) {
    return false;
  }
  if (result.network->operation_kind ==
      mojom::AccountNetworkOperation::kRefreshSession) {
    if (receipt->rotation == 0u) {
      return false;
    }
    // A refresh may advance only the already committed identity by exactly
    // one rotation. The vault and Rust validate the same invariant, but the
    // physical writer must not trust either caller to preserve it.
    // The identity is advanced only when the rotation carried one, by the same
    // rule the core applies in memory: a provider is not obliged to repeat the
    // user object on every rotation, and a refresh that did not mention a name
    // has not withdrawn it. COALESCE keeps the committed value in that case
    // and replaces it in the other, in one statement, so the row cannot end up
    // disagreeing with the session the core is holding.
    sql::Statement refresh(database->GetCachedStatement(
        SQL_FROM_HERE,
        "UPDATE core_account_session SET expires_at_utc_ms=?,rotation=?,"
        "email=COALESCE(?,email),display_name=COALESCE(?,display_name) "
        "WHERE singleton=1 AND session_handle=? AND account_subject=? AND "
        "rotation=? AND auth_method=?"));
    refresh.BindInt64(0, expiry_ms);
    refresh.BindInt64(1, static_cast<int64_t>(receipt->rotation));
    if (receipt->email && !receipt->email->empty()) {
      refresh.BindString(2, *receipt->email);
    } else {
      refresh.BindNull(2);
    }
    if (receipt->display_name && !receipt->display_name->empty()) {
      refresh.BindString(3, *receipt->display_name);
    } else {
      refresh.BindNull(3);
    }
    refresh.BindString(4, receipt->session_handle);
    refresh.BindString(5, receipt->account_subject);
    refresh.BindInt64(6, static_cast<int64_t>(receipt->rotation - 1u));
    refresh.BindInt(7, static_cast<int>(receipt->auth_method));
    return refresh.Run() && database->GetLastChangeCount() == 1;
  }

  if (receipt->rotation != 0u) {
    return false;
  }
  // Starting a new session never overwrites an existing identity. Switching
  // accounts is an explicit revoke followed by an exchange.
  sql::Statement insert(database->GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT INTO core_account_session(singleton,session_handle,"
      "account_subject,expires_at_utc_ms,rotation,auth_method,email,"
      "display_name) VALUES(1,?,?,?,?,?,?,?)"));
  insert.BindString(0, receipt->session_handle);
  insert.BindString(1, receipt->account_subject);
  insert.BindInt64(2, expiry_ms);
  insert.BindInt64(3, static_cast<int64_t>(receipt->rotation));
  insert.BindInt(4, static_cast<int>(receipt->auth_method));
  // NULL and empty are different answers and only one of them is written: the
  // provider confirmed no address, or supplied no name. An empty string here
  // would read back as a person called nothing, which the projection cannot
  // tell from a person the provider did name.
  if (receipt->email && !receipt->email->empty()) {
    insert.BindString(5, *receipt->email);
  } else {
    insert.BindNull(5);
  }
  if (receipt->display_name && !receipt->display_name->empty()) {
    insert.BindString(6, *receipt->display_name);
  } else {
    insert.BindNull(6);
  }
  return insert.Run() && database->GetLastChangeCount() == 1;
}

} // namespace taffy::storage_internal
