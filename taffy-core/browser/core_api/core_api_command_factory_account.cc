// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_api/core_api_command_factory.h"

#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace taffy {
namespace {

namespace api = core_api::mojom;
namespace service = core_service::mojom;

constexpr uint64_t kAccountAuthorizationLifetimeMs = 300'000u;

uint64_t AccountAuthorizationDeadlineFrom(uint64_t now_monotonic_ms) {
  if (now_monotonic_ms > std::numeric_limits<uint64_t>::max() -
                             kAccountAuthorizationLifetimeMs) {
    return std::numeric_limits<uint64_t>::max();
  }
  return now_monotonic_ms + kAccountAuthorizationLifetimeMs;
}

bool IsIdentifier(const std::string &value) {
  return !value.empty() && value.size() <= api::kMaxIdentifierBytes;
}

bool IsOptionalIdentifier(const std::optional<std::string> &value) {
  return !value || IsIdentifier(*value);
}

std::optional<service::AccountAuthMethod>
ProjectProvider(api::AuthProvider provider) {
  switch (provider) {
  case api::AuthProvider::kGoogle:
    return service::AccountAuthMethod::kGoogle;
  case api::AuthProvider::kEmailLink:
    return service::AccountAuthMethod::kEmailLink;
  case api::AuthProvider::kGithub:
    return service::AccountAuthMethod::kGithub;
  case api::AuthProvider::kFacebook:
    return service::AccountAuthMethod::kFacebook;
  }
  return std::nullopt;
}

std::optional<service::AuthCredentialStatus>
ProjectCredentialStatus(api::AuthCredentialStatus status) {
  switch (status) {
  case api::AuthCredentialStatus::kSuccess:
    return service::AuthCredentialStatus::kSuccess;
  case api::AuthCredentialStatus::kCancelled:
    return service::AuthCredentialStatus::kCancelled;
  case api::AuthCredentialStatus::kNoCredential:
    return service::AuthCredentialStatus::kNoCredential;
  case api::AuthCredentialStatus::kUnavailable:
    return service::AuthCredentialStatus::kUnavailable;
  }
  return std::nullopt;
}

std::vector<service::AccountScope> ExternalScopes() {
  return {service::AccountScope::kOpenId, service::AccountScope::kEmail,
          service::AccountScope::kProfile};
}

} // namespace

std::optional<ProjectedCoreCommand>
CoreApiCommandFactory::BuildStartAuth(api::AuthProvider provider,
                                      uint64_t service_generation,
                                      uint64_t now_monotonic_ms) {
  const std::optional<service::AccountAuthMethod> method =
      ProjectProvider(provider);
  if (!method || provider == api::AuthProvider::kEmailLink) {
    return std::nullopt;
  }

  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->operation->deadline_monotonic_ms =
      AccountAuthorizationDeadlineFrom(now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kStartAuth;
  core_command->start_auth = api::StartAuthBody::New(provider);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }

  auto body = service::StartAuthCommand::New();
  body->flow_id = entropy_source_->NewOpaqueId("auth-flow");
  body->method = *method;
  body->redirect_binding_id = entropy_source_->NewOpaqueId("auth-binding");
  body->scopes = provider == api::AuthProvider::kGoogle
                     ? std::vector<service::AccountScope>()
                     : ExternalScopes();
  body->issued_at_monotonic_ms = now_monotonic_ms;
  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind = service::CoreServiceCommandKind::kStartAuth;
  service_command->start_auth = std::move(body);
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

std::optional<ProjectedCoreCommand>
CoreApiCommandFactory::BuildRequestEmailLink(std::string email,
                                             uint64_t service_generation,
                                             uint64_t now_monotonic_ms) {
  if (email.empty() || email.size() > api::kMaxAuthEmailBytes) {
    return std::nullopt;
  }
  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->operation->deadline_monotonic_ms =
      AccountAuthorizationDeadlineFrom(now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kRequestEmailLink;
  core_command->request_email_link = api::EmailLinkBody::New(email);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }

  auto body = service::RequestEmailLinkCommand::New();
  body->flow_id = entropy_source_->NewOpaqueId("auth-flow");
  body->email = std::move(email);
  body->redirect_binding_id = entropy_source_->NewOpaqueId("auth-binding");
  body->scopes = ExternalScopes();
  body->issued_at_monotonic_ms = now_monotonic_ms;
  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind = service::CoreServiceCommandKind::kRequestEmailLink;
  service_command->request_email_link = std::move(body);
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

std::optional<ProjectedCoreCommand>
CoreApiCommandFactory::BuildSignOut(std::optional<std::string> account_id,
                                    uint64_t service_generation,
                                    uint64_t now_monotonic_ms) {
  if (!IsOptionalIdentifier(account_id)) {
    return std::nullopt;
  }
  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kSignOut;
  core_command->sign_out = api::SignOutBody::New(account_id);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }

  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind = service::CoreServiceCommandKind::kSignOut;
  service_command->sign_out =
      service::SignOutCommand::New(std::move(account_id));
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

std::optional<ProjectedCoreCommand>
CoreApiCommandFactory::BuildAuthCredentialResult(
    std::string flow_id, api::AuthProvider provider,
    api::AuthCredentialStatus status,
    std::optional<std::string> credential_handle, uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  const std::optional<service::AuthCredentialStatus> service_status =
      ProjectCredentialStatus(status);
  const bool expects_handle = status == api::AuthCredentialStatus::kSuccess;
  if (!service_status || provider != api::AuthProvider::kGoogle ||
      !IsIdentifier(flow_id) ||
      credential_handle.has_value() != expects_handle ||
      !IsOptionalIdentifier(credential_handle)) {
    return std::nullopt;
  }

  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kAuthCredentialResult;
  core_command->auth_credential_result = api::AuthCredentialResultBody::New(
      provider, credential_handle, status, flow_id);
  if (!HasValidGeneratedBody(*core_command)) {
    return std::nullopt;
  }

  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind =
      service::CoreServiceCommandKind::kAuthCredentialResult;
  service_command->auth_credential_result =
      service::AuthCredentialResultCommand::New(
          std::move(flow_id), service::AccountAuthMethod::kGoogle,
          std::move(credential_handle), *service_status);
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

} // namespace taffy
