// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Generated from schema/contract.json. Do not edit.
// Contract core_api 3.49.

#ifndef TAFFY_CONTRACTS_CORE_API_GENERATED_CPP_CORE_API_ENUMS_H_
#define TAFFY_CONTRACTS_CORE_API_GENERATED_CPP_CORE_API_ENUMS_H_

#include <stdint.h>

#include <optional>

#include "taffy/contracts/core-api/generated/mojom/core_api.mojom-shared.h"

// Closed-enumeration decoders for this contract. Every enumeration here is
// closed: a wire integer that names no member is not a member, and these return
// std::nullopt for it instead of forming an out-of-range enumerator, which is
// undefined behaviour and, at a trust seam, a fail-open. Any caller holding an
// integer must come through here.

namespace taffy::core_api::wire {

// The version this contract's state payload is written at. C++ never encodes or
// decodes that payload — it carries the bytes — but a browser test asserts the
// version a published state arrives with, and a literal there goes stale
// silently on every layout change. Emitted so it cannot.
constexpr uint32_t kStatePayloadSchemaVersion = 33;

// A user or platform intent accepted by the browser facade; none is an
// authorization fact.
constexpr std::optional<mojom::CoreCommandKind>
CoreCommandKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::CoreCommandKind::kStartTask;
    case 1:
      return mojom::CoreCommandKind::kCancelTask;
    case 2:
      return mojom::CoreCommandKind::kApproveAction;
    case 3:
      return mojom::CoreCommandKind::kRetryCore;
    case 4:
      return mojom::CoreCommandKind::kPermissionResult;
    case 5:
      return mojom::CoreCommandKind::kStartAuth;
    case 6:
      return mojom::CoreCommandKind::kRequestEmailLink;
    case 7:
      return mojom::CoreCommandKind::kSignOut;
    case 8:
      return mojom::CoreCommandKind::kAuthCredentialResult;
    case 9:
      return mojom::CoreCommandKind::kCorrectWorkspaceFact;
    case 10:
      return mojom::CoreCommandKind::kExcludeWorkspaceSource;
    case 11:
      return mojom::CoreCommandKind::kRequestWorkspaceExport;
    case 12:
      return mojom::CoreCommandKind::kRequestAsset;
    case 13:
      return mojom::CoreCommandKind::kRemoveAsset;
    case 14:
      return mojom::CoreCommandKind::kSetAssetPolicy;
    case 15:
      return mojom::CoreCommandKind::kSaveProviderCredential;
    case 16:
      return mojom::CoreCommandKind::kForgetProviderCredential;
    case 17:
      return mojom::CoreCommandKind::kStartProviderAuth;
    case 18:
      return mojom::CoreCommandKind::kSaveCustomProvider;
    case 19:
      return mojom::CoreCommandKind::kRemoveCustomProvider;
    case 20:
      return mojom::CoreCommandKind::kCompleteHandover;
    case 21:
      return mojom::CoreCommandKind::kSupplyUserInput;
    case 22:
      return mojom::CoreCommandKind::kSetProviderCredentialState;
    case 23:
      return mojom::CoreCommandKind::kProbeProviderKey;
    case 24:
      return mojom::CoreCommandKind::kSetProviderModelPreference;
    case 25:
      return mojom::CoreCommandKind::kProbeCustomEndpoint;
    case 26:
      return mojom::CoreCommandKind::kRequestComposerCompletion;
    case 27:
      return mojom::CoreCommandKind::kCancelComposerCompletion;
    case 28:
      return mojom::CoreCommandKind::kPauseTask;
    case 29:
      return mojom::CoreCommandKind::kResumeTask;
    case 30:
      return mojom::CoreCommandKind::kTakeOver;
    case 31:
      return mojom::CoreCommandKind::kSetAssistantConfiguration;
    case 32:
      return mojom::CoreCommandKind::kSaveWorkspace;
    case 33:
      return mojom::CoreCommandKind::kRenameWorkspace;
    case 34:
      return mojom::CoreCommandKind::kDeleteWorkspace;
    case 35:
      return mojom::CoreCommandKind::kDiscardWorkspace;
    case 36:
      return mojom::CoreCommandKind::kSearchLibrary;
    case 37:
      return mojom::CoreCommandKind::kSaveLibraryFact;
    case 38:
      return mojom::CoreCommandKind::kRemoveLibraryEntry;
    case 39:
      return mojom::CoreCommandKind::kRequestLibraryExport;
    case 40:
      return mojom::CoreCommandKind::kSearchMemory;
    case 41:
      return mojom::CoreCommandKind::kUpsertMemory;
    case 42:
      return mojom::CoreCommandKind::kDeleteMemory;
    case 43:
      return mojom::CoreCommandKind::kAcceptTaskArtifact;
    case 44:
      return mojom::CoreCommandKind::kRequestTaskArtifactExport;
    case 45:
      return mojom::CoreCommandKind::kMutateSiteSkill;
    case 46:
      return mojom::CoreCommandKind::kCancelProviderAuth;
    case 47:
      return mojom::CoreCommandKind::kStartLibraryRefresh;
    case 48:
      return mojom::CoreCommandKind::kFollowUp;
    default:
      return std::nullopt;
  }
}

// The sixteen compiled-in ability groups of the one assistant. These configure
// existing tools and templates; they never name another actor.
constexpr std::optional<mojom::AssistantAbilityView>
AssistantAbilityViewFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::AssistantAbilityView::kPagesLookup;
    case 1:
      return mojom::AssistantAbilityView::kPagesCompare;
    case 2:
      return mojom::AssistantAbilityView::kPagesSummarize;
    case 3:
      return mojom::AssistantAbilityView::kPagesTable;
    case 4:
      return mojom::AssistantAbilityView::kProducts;
    case 5:
      return mojom::AssistantAbilityView::kOffers;
    case 6:
      return mojom::AssistantAbilityView::kForm;
    case 7:
      return mojom::AssistantAbilityView::kDownloads;
    case 8:
      return mojom::AssistantAbilityView::kPdf;
    case 9:
      return mojom::AssistantAbilityView::kSheet;
    case 10:
      return mojom::AssistantAbilityView::kDocument;
    case 11:
      return mojom::AssistantAbilityView::kDepth;
    case 12:
      return mojom::AssistantAbilityView::kTrip;
    case 13:
      return mojom::AssistantAbilityView::kPictures;
    case 14:
      return mojom::AssistantAbilityView::kVideo;
    case 15:
      return mojom::AssistantAbilityView::kKeep;
    default:
      return std::nullopt;
  }
}

// The fixed built-in ways Taffy can help. The order is part of the status
// contract and never comes from a server or a model.
constexpr std::optional<mojom::BuiltinSkillIdView>
BuiltinSkillIdViewFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::BuiltinSkillIdView::kGeneralWebResearch;
    case 1:
      return mojom::BuiltinSkillIdView::kDeepResearch;
    case 2:
      return mojom::BuiltinSkillIdView::kProductComparison;
    case 3:
      return mojom::BuiltinSkillIdView::kMultiTabComparison;
    case 4:
      return mojom::BuiltinSkillIdView::kWebsiteSummarizer;
    case 5:
      return mojom::BuiltinSkillIdView::kPdfAnalysis;
    case 6:
      return mojom::BuiltinSkillIdView::kDataExtraction;
    case 7:
      return mojom::BuiltinSkillIdView::kFormAssistant;
    case 8:
      return mojom::BuiltinSkillIdView::kShopping;
    case 9:
      return mojom::BuiltinSkillIdView::kDownloadOrganizer;
    case 10:
      return mojom::BuiltinSkillIdView::kTravelResearch;
    case 11:
      return mojom::BuiltinSkillIdView::kVideoTranscriptAnalyzer;
    case 12:
      return mojom::BuiltinSkillIdView::kImageUnderstanding;
    case 13:
      return mojom::BuiltinSkillIdView::kLibraryBuilder;
    case 14:
      return mojom::BuiltinSkillIdView::kSpreadsheetBuilder;
    case 15:
      return mojom::BuiltinSkillIdView::kDocumentGenerator;
    default:
      return std::nullopt;
  }
}

// Whether one built-in skill has every compiled tool, product part, profile
// fact, and policy path its definition requires. This is independent of whether
// the person switched it on.
constexpr std::optional<mojom::BuiltinSkillAvailabilityView>
BuiltinSkillAvailabilityViewFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::BuiltinSkillAvailabilityView::kAvailable;
    case 1:
      return mojom::BuiltinSkillAvailabilityView::kRequiredToolUnavailable;
    case 2:
      return mojom::BuiltinSkillAvailabilityView::kRequiredPartMissing;
    case 3:
      return mojom::BuiltinSkillAvailabilityView::kProfileUnavailable;
    case 4:
      return mojom::BuiltinSkillAvailabilityView::kPolicyUnavailable;
    default:
      return std::nullopt;
  }
}

// Whether this payload contains every status family or a bounded recovery view
// that names every omitted family.
constexpr std::optional<mojom::CoreStatusProjectionMode>
CoreStatusProjectionModeFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::CoreStatusProjectionMode::kComplete;
    case 1:
      return mojom::CoreStatusProjectionMode::kRecoveryRequired;
    default:
      return std::nullopt;
  }
}

// The fixed bulk status families a recovery projection may omit. The order is
// the required omission-descriptor order.
constexpr std::optional<mojom::CoreStatusProjectionFamily>
CoreStatusProjectionFamilyFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::CoreStatusProjectionFamily::kActiveTasks;
    case 1:
      return mojom::CoreStatusProjectionFamily::kWorkspaces;
    case 2:
      return mojom::CoreStatusProjectionFamily::kWorkspaceExport;
    case 3:
      return mojom::CoreStatusProjectionFamily::kAssetDelivery;
    case 4:
      return mojom::CoreStatusProjectionFamily::kProviderRoster;
    case 5:
      return mojom::CoreStatusProjectionFamily::kProviderProbes;
    case 6:
      return mojom::CoreStatusProjectionFamily::kProviderModels;
    case 7:
      return mojom::CoreStatusProjectionFamily::kLibrary;
    case 8:
      return mojom::CoreStatusProjectionFamily::kLibraryExport;
    case 9:
      return mojom::CoreStatusProjectionFamily::kMemory;
    case 10:
      return mojom::CoreStatusProjectionFamily::kSavedSignIns;
    case 11:
      return mojom::CoreStatusProjectionFamily::kSavedDetails;
    case 12:
      return mojom::CoreStatusProjectionFamily::kSiteSkills;
    default:
      return std::nullopt;
  }
}

// The four person-visible changes supported by the saved-skill aggregate.
constexpr std::optional<mojom::SiteSkillMutationKind>
SiteSkillMutationKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::SiteSkillMutationKind::kTeach;
    case 1:
      return mojom::SiteSkillMutationKind::kUpdate;
    case 2:
      return mojom::SiteSkillMutationKind::kSetEnabled;
    case 3:
      return mojom::SiteSkillMutationKind::kRemove;
    default:
      return std::nullopt;
  }
}

// One closed shape of page fact captured by the browser observation.
constexpr std::optional<mojom::SiteSkillClauseKind>
SiteSkillClauseKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::SiteSkillClauseKind::kRolePresent;
    case 1:
      return mojom::SiteSkillClauseKind::kPhraseAt;
    case 2:
      return mojom::SiteSkillClauseKind::kStateAt;
    default:
      return std::nullopt;
  }
}

// Closed arguments for reviewed saved steps. Public starting addresses are
// explicit; semantic targets retain compiled vocabulary only.
constexpr std::optional<mojom::SiteSkillArgumentKind>
SiteSkillArgumentKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::SiteSkillArgumentKind::kFromEarlierStep;
    case 1:
      return mojom::SiteSkillArgumentKind::kFromPerson;
    case 2:
      return mojom::SiteSkillArgumentKind::kChoice;
    case 3:
      return mojom::SiteSkillArgumentKind::kCount;
    case 4:
      return mojom::SiteSkillArgumentKind::kFlag;
    case 5:
      return mojom::SiteSkillArgumentKind::kPublicAddress;
    case 6:
      return mojom::SiteSkillArgumentKind::kSemanticTarget;
    default:
      return std::nullopt;
  }
}

// Where the saved steps came from; display fact only and never authority.
constexpr std::optional<mojom::SiteSkillProvenanceView>
SiteSkillProvenanceViewFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::SiteSkillProvenanceView::kAuthored;
    case 1:
      return mojom::SiteSkillProvenanceView::kRecordedFromTask;
    default:
      return std::nullopt;
  }
}

// The visible lifecycle of one saved skill version.
constexpr std::optional<mojom::SiteSkillStatusView>
SiteSkillStatusViewFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::SiteSkillStatusView::kDraft;
    case 1:
      return mojom::SiteSkillStatusView::kActive;
    case 2:
      return mojom::SiteSkillStatusView::kSuperseded;
    case 3:
      return mojom::SiteSkillStatusView::kRetired;
    case 4:
      return mojom::SiteSkillStatusView::kDisabled;
    default:
      return std::nullopt;
  }
}

// A named starting point for response style and planning cadence only.
constexpr std::optional<mojom::PersonalityPresetView>
PersonalityPresetViewFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::PersonalityPresetView::kCarefulResearcher;
    case 1:
      return mojom::PersonalityPresetView::kQuickShopper;
    case 2:
      return mojom::PersonalityPresetView::kTripPlanner;
    default:
      return std::nullopt;
  }
}

// Closed reviewed task templates shared by ingress, replay, and every UI
// projection.
constexpr std::optional<mojom::TaskTemplateId>
TaskTemplateIdFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::TaskTemplateId::kCompareProducts;
    case 1:
      return mojom::TaskTemplateId::kSummarizeEvidence;
    case 2:
      return mojom::TaskTemplateId::kBuildSourceTable;
    case 3:
      return mojom::TaskTemplateId::kWebErrand;
    default:
      return std::nullopt;
  }
}

// Closed route disclosure shown on the task consent surface.
constexpr std::optional<mojom::TaskProviderRoute>
TaskProviderRouteFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::TaskProviderRoute::kNotConfigured;
    case 1:
      return mojom::TaskProviderRoute::kDirectUserKey;
    case 2:
      return mojom::TaskProviderRoute::kManagedService;
    case 3:
      return mojom::TaskProviderRoute::kNoModelRequired;
    default:
      return std::nullopt;
  }
}

// One of the person's own stores attached whole to a task; Taffy reaches it
// through bounded tools, never as prompt text.
constexpr std::optional<mojom::TaskAttachedStore>
TaskAttachedStoreFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::TaskAttachedStore::kHistory;
    case 1:
      return mojom::TaskAttachedStore::kBookmarks;
    case 2:
      return mojom::TaskAttachedStore::kOpenTabs;
    default:
      return std::nullopt;
  }
}

// The user-visible availability of the profile core service.
constexpr std::optional<mojom::CoreAvailability>
CoreAvailabilityFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::CoreAvailability::kStarting;
    case 1:
      return mojom::CoreAvailability::kReady;
    case 2:
      return mojom::CoreAvailability::kUnavailable;
    case 3:
      return mojom::CoreAvailability::kCircuitOpen;
    default:
      return std::nullopt;
  }
}

// Whether Chromium's profile-owned saved-data stores have produced one complete
// bounded snapshot. Private profiles are always unavailable.
constexpr std::optional<mojom::SavedDataAvailability>
SavedDataAvailabilityFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::SavedDataAvailability::kLoading;
    case 1:
      return mojom::SavedDataAvailability::kReady;
    case 2:
      return mojom::SavedDataAvailability::kUnavailable;
    default:
      return std::nullopt;
  }
}

// The immutable phase projected to all platform UIs.
constexpr std::optional<mojom::TaskPhase> TaskPhaseFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::TaskPhase::kIdle;
    case 1:
      return mojom::TaskPhase::kPlanning;
    case 2:
      return mojom::TaskPhase::kWaitingForUser;
    case 3:
      return mojom::TaskPhase::kRunning;
    case 4:
      return mojom::TaskPhase::kCompleted;
    case 5:
      return mojom::TaskPhase::kFailed;
    case 6:
      return mojom::TaskPhase::kCancelled;
    case 7:
      return mojom::TaskPhase::kOutcomeUnknown;
    case 8:
      return mojom::TaskPhase::kPaused;
    case 9:
      return mojom::TaskPhase::kPartial;
    default:
      return std::nullopt;
  }
}

// A closed control the portable reducer admits for one exact task revision.
constexpr std::optional<mojom::TaskControlKind>
TaskControlKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::TaskControlKind::kPause;
    case 1:
      return mojom::TaskControlKind::kResume;
    case 2:
      return mojom::TaskControlKind::kTakeOver;
    case 3:
      return mojom::TaskControlKind::kStop;
    default:
      return std::nullopt;
  }
}

// Closed, content-free failures exposed to a platform UI.
constexpr std::optional<mojom::CoreFailureCode>
CoreFailureCodeFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::CoreFailureCode::kCancelled;
    case 1:
      return mojom::CoreFailureCode::kDeadlineExceeded;
    case 2:
      return mojom::CoreFailureCode::kCoreUnavailable;
    case 3:
      return mojom::CoreFailureCode::kPolicyDenied;
    case 4:
      return mojom::CoreFailureCode::kInvalidRequest;
    case 5:
      return mojom::CoreFailureCode::kBackpressure;
    case 6:
      return mojom::CoreFailureCode::kOutcomeUnknown;
    case 7:
      return mojom::CoreFailureCode::kInternal;
    case 8:
      return mojom::CoreFailureCode::kBudgetExceeded;
    case 9:
      return mojom::CoreFailureCode::kProviderUnavailable;
    case 10:
      return mojom::CoreFailureCode::kSourcesUnavailable;
    case 11:
      return mojom::CoreFailureCode::kJournalUnusable;
    case 12:
      return mojom::CoreFailureCode::kUnverifiableAction;
    case 13:
      return mojom::CoreFailureCode::kProviderRefused;
    case 14:
      return mojom::CoreFailureCode::kProviderLimit;
    case 15:
      return mojom::CoreFailureCode::kOffline;
    case 16:
      return mojom::CoreFailureCode::kPolicyRefused;
    default:
      return std::nullopt;
  }
}

// A portable permission name implemented by each platform adapter.
constexpr std::optional<mojom::PlatformPermission>
PlatformPermissionFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::PlatformPermission::kNotifications;
    case 1:
      return mojom::PlatformPermission::kCamera;
    case 2:
      return mojom::PlatformPermission::kMicrophone;
    case 3:
      return mojom::PlatformPermission::kLocation;
    case 4:
      return mojom::PlatformPermission::kReadUserFile;
    case 5:
      return mojom::PlatformPermission::kWriteUserFile;
    default:
      return std::nullopt;
  }
}

// The platform result for a permission request.
constexpr std::optional<mojom::PermissionDecision>
PermissionDecisionFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::PermissionDecision::kGranted;
    case 1:
      return mojom::PermissionDecision::kDenied;
    case 2:
      return mojom::PermissionDecision::kUnavailable;
    default:
      return std::nullopt;
  }
}

// A closed account method understood by portable auth logic and every platform
// UI.
constexpr std::optional<mojom::AuthProvider>
AuthProviderFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::AuthProvider::kGoogle;
    case 1:
      return mojom::AuthProvider::kEmailLink;
    case 2:
      return mojom::AuthProvider::kGithub;
    case 3:
      return mojom::AuthProvider::kFacebook;
    default:
      return std::nullopt;
  }
}

// Why one closed account method is or is not offered by this signed product.
constexpr std::optional<mojom::AuthMethodAvailability>
AuthMethodAvailabilityFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::AuthMethodAvailability::kAvailable;
    case 1:
      return mojom::AuthMethodAvailability::kNotConfigured;
    case 2:
      return mojom::AuthMethodAvailability::kPlatformUnavailable;
    default:
      return std::nullopt;
  }
}

// The UI-safe state of the profile account session.
constexpr std::optional<mojom::AuthPhase> AuthPhaseFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::AuthPhase::kInitializing;
    case 1:
      return mojom::AuthPhase::kSignedOut;
    case 2:
      return mojom::AuthPhase::kInFlight;
    case 3:
      return mojom::AuthPhase::kLinkSent;
    case 4:
      return mojom::AuthPhase::kSignedIn;
    case 5:
      return mojom::AuthPhase::kFailed;
    default:
      return std::nullopt;
  }
}

// A content-free account failure safe to show on every platform.
constexpr std::optional<mojom::AuthFailureCode>
AuthFailureCodeFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::AuthFailureCode::kNotConfigured;
    case 1:
      return mojom::AuthFailureCode::kCancelled;
    case 2:
      return mojom::AuthFailureCode::kNoCredential;
    case 3:
      return mojom::AuthFailureCode::kNetwork;
    case 4:
      return mojom::AuthFailureCode::kRejected;
    case 5:
      return mojom::AuthFailureCode::kInvalidRedirect;
    case 6:
      return mojom::AuthFailureCode::kCoreUnavailable;
    case 7:
      return mojom::AuthFailureCode::kUnknown;
    default:
      return std::nullopt;
  }
}

// Exactly one terminal result from a native credential surface.
constexpr std::optional<mojom::AuthCredentialStatus>
AuthCredentialStatusFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::AuthCredentialStatus::kSuccess;
    case 1:
      return mojom::AuthCredentialStatus::kCancelled;
    case 2:
      return mojom::AuthCredentialStatus::kNoCredential;
    case 3:
      return mojom::AuthCredentialStatus::kUnavailable;
    default:
      return std::nullopt;
  }
}

// The honest terminal or active state retained with one workspace.
constexpr std::optional<mojom::WorkspacePhase>
WorkspacePhaseFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::WorkspacePhase::kRunning;
    case 1:
      return mojom::WorkspacePhase::kWaitingForUser;
    case 2:
      return mojom::WorkspacePhase::kPaused;
    case 3:
      return mojom::WorkspacePhase::kDone;
    case 4:
      return mojom::WorkspacePhase::kPartlyDone;
    case 5:
      return mojom::WorkspacePhase::kStopped;
    case 6:
      return mojom::WorkspacePhase::kFailed;
    default:
      return std::nullopt;
  }
}

// How a workspace fact came to be.
constexpr std::optional<mojom::WorkspaceFactKind>
WorkspaceFactKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::WorkspaceFactKind::kFromPage;
    case 1:
      return mojom::WorkspaceFactKind::kSummarized;
    case 2:
      return mojom::WorkspaceFactKind::kTaffyInference;
    case 3:
      return mojom::WorkspaceFactKind::kUserEntered;
    default:
      return std::nullopt;
  }
}

// Closed deterministic export formats implemented by portable Rust.
constexpr std::optional<mojom::WorkspaceExportFormat>
WorkspaceExportFormatFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::WorkspaceExportFormat::kMarkdown;
    case 1:
      return mojom::WorkspaceExportFormat::kCsv;
    default:
      return std::nullopt;
  }
}

// One closed thing a task did (decision 0148). The sentence a step renders is
// composed on the surface from a compiled-in template, so no text ever crosses
// this contract.
constexpr std::optional<mojom::TaskActivityKind>
TaskActivityKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::TaskActivityKind::kOpenedPage;
    case 1:
      return mojom::TaskActivityKind::kReadPage;
    case 2:
      return mojom::TaskActivityKind::kPageUnavailable;
    case 3:
      return mojom::TaskActivityKind::kMoveRefused;
    case 4:
      return mojom::TaskActivityKind::kAskedYou;
    case 5:
      return mojom::TaskActivityKind::kYouAnswered;
    case 6:
      return mojom::TaskActivityKind::kHandedBack;
    case 7:
      return mojom::TaskActivityKind::kYouTookOver;
    case 8:
      return mojom::TaskActivityKind::kBuiltOutput;
    default:
      return std::nullopt;
  }
}

// Closed task artifact formats exposed by the portable UI contract.
constexpr std::optional<mojom::TaskArtifactKind>
TaskArtifactKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::TaskArtifactKind::kMarkdown;
    case 1:
      return mojom::TaskArtifactKind::kCsv;
    case 2:
      return mojom::TaskArtifactKind::kXlsx;
    case 3:
      return mojom::TaskArtifactKind::kPdf;
    case 4:
      return mojom::TaskArtifactKind::kDocx;
    case 5:
      return mojom::TaskArtifactKind::kPptx;
    case 6:
      return mojom::TaskArtifactKind::kWaveAudio;
    case 7:
      return mojom::TaskArtifactKind::kFrameArchive;
    default:
      return std::nullopt;
  }
}

// Whether durable Library behavior is available for this profile.
constexpr std::optional<mojom::LibraryAvailability>
LibraryAvailabilityFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::LibraryAvailability::kAvailable;
    case 1:
      return mojom::LibraryAvailability::kPrivateProfile;
    case 2:
      return mojom::LibraryAvailability::kUnavailable;
    default:
      return std::nullopt;
  }
}

// Exact comparison of one revisited source with the preserved original.
constexpr std::optional<mojom::LibraryRefreshDisposition>
LibraryRefreshDispositionFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::LibraryRefreshDisposition::kUnchanged;
    case 1:
      return mojom::LibraryRefreshDisposition::kChanged;
    case 2:
      return mojom::LibraryRefreshDisposition::kMissing;
    default:
      return std::nullopt;
  }
}

// Whether durable Memory behavior is available for this profile.
constexpr std::optional<mojom::MemoryAvailability>
MemoryAvailabilityFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::MemoryAvailability::kAvailable;
    case 1:
      return mojom::MemoryAvailability::kPrivateProfile;
    case 2:
      return mojom::MemoryAvailability::kUnavailable;
    default:
      return std::nullopt;
  }
}

// The immutable visible reason a Memory record exists.
constexpr std::optional<mojom::MemorySourceKind>
MemorySourceKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::MemorySourceKind::kYouWrote;
    case 1:
      return mojom::MemorySourceKind::kTaffySuggested;
    default:
      return std::nullopt;
  }
}

// The visible future-task scope in which a Memory record may be used.
constexpr std::optional<mojom::MemoryScopeKind>
MemoryScopeKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::MemoryScopeKind::kAllTasks;
    case 1:
      return mojom::MemoryScopeKind::kWorkspace;
    default:
      return std::nullopt;
  }
}

// Visible handling class for an explicitly retained preference.
constexpr std::optional<mojom::MemorySensitivity>
MemorySensitivityFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::MemorySensitivity::kStandard;
    case 1:
      return mojom::MemorySensitivity::kSensitive;
    default:
      return std::nullopt;
  }
}

// A closed, content-free result for a selected-page inspector request.
constexpr std::optional<mojom::PageInspectorAvailability>
PageInspectorAvailabilityFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::PageInspectorAvailability::kAvailable;
    case 1:
      return mojom::PageInspectorAvailability::kNoSelectedPage;
    case 2:
      return mojom::PageInspectorAvailability::kDocumentUnavailable;
    case 3:
      return mojom::PageInspectorAvailability::kCoreUnavailable;
    case 4:
      return mojom::PageInspectorAvailability::kPolicyDenied;
    case 5:
      return mojom::PageInspectorAvailability::kBackpressure;
    case 6:
      return mojom::PageInspectorAvailability::kStaleDocument;
    case 7:
      return mojom::PageInspectorAvailability::kInvalidResponse;
    default:
      return std::nullopt;
  }
}

// Whether the exact inspected page produced a trustworthy saved-skill offer
// set.
constexpr std::optional<mojom::SiteSkillOfferAvailability>
SiteSkillOfferAvailabilityFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::SiteSkillOfferAvailability::kAvailable;
    case 1:
      return mojom::SiteSkillOfferAvailability::kCoreUnavailable;
    case 2:
      return mojom::SiteSkillOfferAvailability::kStaleDocument;
    case 3:
      return mojom::SiteSkillOfferAvailability::kPrivateProfile;
    case 4:
      return mojom::SiteSkillOfferAvailability::kIncomplete;
    case 5:
      return mojom::SiteSkillOfferAvailability::kInvalidResponse;
    default:
      return std::nullopt;
  }
}

// Closed deterministic page snapshot formats rendered by sandboxed Rust.
constexpr std::optional<mojom::PageSnapshotExportFormat>
PageSnapshotExportFormatFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::PageSnapshotExportFormat::kMarkdown;
    case 1:
      return mojom::PageSnapshotExportFormat::kCanonicalJson;
    default:
      return std::nullopt;
  }
}

// Closed content-free terminal result for one page snapshot export request.
constexpr std::optional<mojom::PageSnapshotExportAvailability>
PageSnapshotExportAvailabilityFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::PageSnapshotExportAvailability::kAvailable;
    case 1:
      return mojom::PageSnapshotExportAvailability::kNoSelectedPage;
    case 2:
      return mojom::PageSnapshotExportAvailability::kDocumentUnavailable;
    case 3:
      return mojom::PageSnapshotExportAvailability::kCoreUnavailable;
    case 4:
      return mojom::PageSnapshotExportAvailability::kPolicyDenied;
    case 5:
      return mojom::PageSnapshotExportAvailability::kBackpressure;
    case 6:
      return mojom::PageSnapshotExportAvailability::kStaleDocument;
    case 7:
      return mojom::PageSnapshotExportAvailability::kPrivateProfile;
    case 8:
      return mojom::PageSnapshotExportAvailability::kIncomplete;
    case 9:
      return mojom::PageSnapshotExportAvailability::kOversize;
    case 10:
      return mojom::PageSnapshotExportAvailability::kInvalidResponse;
    case 11:
      return mojom::PageSnapshotExportAvailability::kCancelled;
    case 12:
      return mojom::PageSnapshotExportAvailability::kReplayConflict;
    default:
      return std::nullopt;
  }
}

// The UI-safe document projections the inspector can render.
constexpr std::optional<mojom::PageInspectorDocumentKind>
PageInspectorDocumentKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::PageInspectorDocumentKind::kSemanticPage;
    default:
      return std::nullopt;
  }
}

// Closed content-free claims a document projection can make about itself.
constexpr std::optional<mojom::PageInspectorClaim>
PageInspectorClaimFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::PageInspectorClaim::kBrowserValidated;
    case 1:
      return mojom::PageInspectorClaim::kPolicyAdmitted;
    case 2:
      return mojom::PageInspectorClaim::kRedacted;
    case 3:
      return mojom::PageInspectorClaim::kBounded;
    default:
      return std::nullopt;
  }
}

// The browser-corroborated lifecycle safe for a UI to display.
constexpr std::optional<mojom::PageInspectorDocumentState>
PageInspectorDocumentStateFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::PageInspectorDocumentState::kActive;
    case 1:
      return mojom::PageInspectorDocumentState::kFrozen;
    case 2:
      return mojom::PageInspectorDocumentState::kUnavailable;
    default:
      return std::nullopt;
  }
}

// A deliberately coarse UI role taxonomy independent of renderer protocol
// ordinals.
constexpr std::optional<mojom::PageInspectorNodeRole>
PageInspectorNodeRoleFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::PageInspectorNodeRole::kDocument;
    case 1:
      return mojom::PageInspectorNodeRole::kSection;
    case 2:
      return mojom::PageInspectorNodeRole::kText;
    case 3:
      return mojom::PageInspectorNodeRole::kList;
    case 4:
      return mojom::PageInspectorNodeRole::kTable;
    case 5:
      return mojom::PageInspectorNodeRole::kLink;
    case 6:
      return mojom::PageInspectorNodeRole::kControl;
    case 7:
      return mojom::PageInspectorNodeRole::kMedia;
    case 8:
      return mojom::PageInspectorNodeRole::kCommerce;
    case 9:
      return mojom::PageInspectorNodeRole::kReference;
    case 10:
      return mojom::PageInspectorNodeRole::kUnknown;
    default:
      return std::nullopt;
  }
}

// A UI-safe disclosure result after browser redaction.
constexpr std::optional<mojom::PageInspectorSensitivity>
PageInspectorSensitivityFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::PageInspectorSensitivity::kPublic;
    case 1:
      return mojom::PageInspectorSensitivity::kWithheld;
    case 2:
      return mojom::PageInspectorSensitivity::kUnknown;
    default:
      return std::nullopt;
  }
}

// Coarse relationships sufficient for the inspector without exposing renderer
// enum values.
constexpr std::optional<mojom::PageInspectorRelationship>
PageInspectorRelationshipFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::PageInspectorRelationship::kHierarchy;
    case 1:
      return mojom::PageInspectorRelationship::kLabel;
    case 2:
      return mojom::PageInspectorRelationship::kDescription;
    case 3:
      return mojom::PageInspectorRelationship::kControl;
    case 4:
      return mojom::PageInspectorRelationship::kTableHeader;
    case 5:
      return mojom::PageInspectorRelationship::kEntity;
    case 6:
      return mojom::PageInspectorRelationship::kSource;
    case 7:
      return mojom::PageInspectorRelationship::kOther;
    default:
      return std::nullopt;
  }
}

// Coarse extraction sources safe to name in a product UI.
constexpr std::optional<mojom::PageInspectorAdapterKind>
PageInspectorAdapterKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::PageInspectorAdapterKind::kStructure;
    case 1:
      return mojom::PageInspectorAdapterKind::kAccessibility;
    case 2:
      return mojom::PageInspectorAdapterKind::kForms;
    case 3:
      return mojom::PageInspectorAdapterKind::kMetadata;
    case 4:
      return mojom::PageInspectorAdapterKind::kBrowser;
    default:
      return std::nullopt;
  }
}

// Content-free status of one UI-safe extraction source.
constexpr std::optional<mojom::PageInspectorAdapterStatus>
PageInspectorAdapterStatusFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::PageInspectorAdapterStatus::kComplete;
    case 1:
      return mojom::PageInspectorAdapterStatus::kPartial;
    case 2:
      return mojom::PageInspectorAdapterStatus::kConflict;
    case 3:
      return mojom::PageInspectorAdapterStatus::kUnsupported;
    case 4:
      return mojom::PageInspectorAdapterStatus::kFailed;
    default:
      return std::nullopt;
  }
}

// The bounded resource dimensions an inspector projection can report.
constexpr std::optional<mojom::PageInspectorBudgetKind>
PageInspectorBudgetKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::PageInspectorBudgetKind::kItems;
    case 1:
      return mojom::PageInspectorBudgetKind::kText;
    case 2:
      return mojom::PageInspectorBudgetKind::kTotalBytes;
    case 3:
      return mojom::PageInspectorBudgetKind::kDepth;
    case 4:
      return mojom::PageInspectorBudgetKind::kFrames;
    case 5:
      return mojom::PageInspectorBudgetKind::kMessage;
    case 6:
      return mojom::PageInspectorBudgetKind::kDeadline;
    default:
      return std::nullopt;
  }
}

// Closed content-free warnings safe to show for an observed page.
constexpr std::optional<mojom::PageInspectorWarningCode>
PageInspectorWarningCodeFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::PageInspectorWarningCode::kSourceUnavailable;
    case 1:
      return mojom::PageInspectorWarningCode::kSourceFailed;
    case 2:
      return mojom::PageInspectorWarningCode::kConflict;
    case 3:
      return mojom::PageInspectorWarningCode::kFrameOmitted;
    case 4:
      return mojom::PageInspectorWarningCode::kContentPartial;
    case 5:
      return mojom::PageInspectorWarningCode::kSemanticsMissing;
    case 6:
      return mojom::PageInspectorWarningCode::kContentWithheld;
    case 7:
      return mojom::PageInspectorWarningCode::kLocationMinimized;
    case 8:
      return mojom::PageInspectorWarningCode::kDeadline;
    case 9:
      return mojom::PageInspectorWarningCode::kResourcePressure;
    case 10:
      return mojom::PageInspectorWarningCode::kSuspiciousContent;
    default:
      return std::nullopt;
  }
}

// One terminal browser admission verdict for one UI intent; never task
// completion.
constexpr std::optional<mojom::CoreApiSubmissionStatus>
CoreApiSubmissionStatusFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::CoreApiSubmissionStatus::kAccepted;
    case 1:
      return mojom::CoreApiSubmissionStatus::kInvalidRequest;
    case 2:
      return mojom::CoreApiSubmissionStatus::kStaleGeneration;
    case 3:
      return mojom::CoreApiSubmissionStatus::kStaleRevision;
    case 4:
      return mojom::CoreApiSubmissionStatus::kDeadlineExceeded;
    case 5:
      return mojom::CoreApiSubmissionStatus::kBackpressure;
    case 6:
      return mojom::CoreApiSubmissionStatus::kCoreUnavailable;
    case 7:
      return mojom::CoreApiSubmissionStatus::kDuplicate;
    case 8:
      return mojom::CoreApiSubmissionStatus::kSourceNotOpen;
    case 9:
      return mojom::CoreApiSubmissionStatus::kSourceAmbiguous;
    case 10:
      return mojom::CoreApiSubmissionStatus::kWindowUnavailable;
    default:
      return std::nullopt;
  }
}

// What one of the product's own artifacts is for. Closed, and the same set the
// delivery catalog holds; a surface renders a name from it rather than from a
// string an origin sent.
constexpr std::optional<mojom::AssetKindView>
AssetKindViewFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::AssetKindView::kPythonStdlib;
    case 1:
      return mojom::AssetKindView::kPythonPackages;
    case 2:
      return mojom::AssetKindView::kModelWeights;
    case 3:
      return mojom::AssetKindView::kModelTokenizer;
    case 4:
      return mojom::AssetKindView::kFilterList;
    case 5:
      return mojom::AssetKindView::kCountryFlags;
    case 6:
      return mojom::AssetKindView::kStartScenes;
    default:
      return std::nullopt;
  }
}

// How much of one artifact is on the device.
constexpr std::optional<mojom::AssetPresenceView>
AssetPresenceViewFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::AssetPresenceView::kAbsent;
    case 1:
      return mojom::AssetPresenceView::kPartial;
    case 2:
      return mojom::AssetPresenceView::kComplete;
    case 3:
      return mojom::AssetPresenceView::kInstalled;
    default:
      return std::nullopt;
  }
}

// What the device's connection costs right now.
constexpr std::optional<mojom::AssetNetworkCostView>
AssetNetworkCostViewFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::AssetNetworkCostView::kOffline;
    case 1:
      return mojom::AssetNetworkCostView::kMetered;
    case 2:
      return mojom::AssetNetworkCostView::kUnmetered;
    default:
      return std::nullopt;
  }
}

// Why the last attempt at one artifact did not end in an install. Member for
// member the delivery plane's own AssetRefusalReason, so the projection can
// only pass a verdict through and never invent one; cross_contracts.py holds
// the two sets equal.
constexpr std::optional<mojom::AssetRefusalView>
AssetRefusalViewFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::AssetRefusalView::kUnknownAsset;
    case 1:
      return mojom::AssetRefusalView::kNoVariantForPlatform;
    case 2:
      return mojom::AssetRefusalView::kNotPublishedYet;
    case 3:
      return mojom::AssetRefusalView::kCatalogRowIncomplete;
    case 4:
      return mojom::AssetRefusalView::kVariantTooLarge;
    case 5:
      return mojom::AssetRefusalView::kAttemptsExhausted;
    case 6:
      return mojom::AssetRefusalView::kIntegrityFailed;
    case 7:
      return mojom::AssetRefusalView::kNetworkNotPermitted;
    case 8:
      return mojom::AssetRefusalView::kDeclinedByPerson;
    default:
      return std::nullopt;
  }
}

// How a read of one member out of an installed part ended. Closed, and
// deliberately separate from CoreApiSubmissionStatus: nothing here was
// submitted anywhere. The browser opened a file it already owns and answered,
// and the isolated core was not asked.
constexpr std::optional<mojom::PartMemberStatus>
PartMemberStatusFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::PartMemberStatus::kOk;
    case 1:
      return mojom::PartMemberStatus::kNotInstalled;
    case 2:
      return mojom::PartMemberStatus::kNotFound;
    case 3:
      return mojom::PartMemberStatus::kTooLarge;
    case 4:
      return mojom::PartMemberStatus::kUnreadable;
    case 5:
      return mojom::PartMemberStatus::kInvalidRequest;
    default:
      return std::nullopt;
  }
}

// How a person authenticates to one model provider. Held equal to
// core_service::ProviderAuthMethod member for member and wire value for wire
// value by cross_contracts.py.
constexpr std::optional<mojom::ProviderAuthMethodView>
ProviderAuthMethodViewFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::ProviderAuthMethodView::kApiKey;
    case 1:
      return mojom::ProviderAuthMethodView::kOauth;
    default:
      return std::nullopt;
  }
}

// The closed request family a model effect names. Held equal to
// core_service::ProviderWireApi member for member and wire value for wire
// value.
constexpr std::optional<mojom::ProviderWireApiView>
ProviderWireApiViewFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::ProviderWireApiView::kAnthropicMessages;
    case 1:
      return mojom::ProviderWireApiView::kOpenAiResponses;
    case 2:
      return mojom::ProviderWireApiView::kOpenAiCompletions;
    case 3:
      return mojom::ProviderWireApiView::kGoogleGenerativeLanguage;
    case 4:
      return mojom::ProviderWireApiView::kManaged;
    case 5:
      return mojom::ProviderWireApiView::kOpenAiCodexResponses;
    case 6:
      return mojom::ProviderWireApiView::kGoogleCloudCodeAssist;
    default:
      return std::nullopt;
  }
}

// What one bounded probe call proved (decision 0083). USABLE and the definitive
// failures subtract; the indefinite members mean the provider was not
// definitively heard, so a surface offers to save anyway rather than claiming
// the key is wrong.
constexpr std::optional<mojom::ProviderProbeVerdictView>
ProviderProbeVerdictViewFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::ProviderProbeVerdictView::kUsable;
    case 1:
      return mojom::ProviderProbeVerdictView::kAuth;
    case 2:
      return mojom::ProviderProbeVerdictView::kBilling;
    case 3:
      return mojom::ProviderProbeVerdictView::kRateLimit;
    case 4:
      return mojom::ProviderProbeVerdictView::kOverloaded;
    case 5:
      return mojom::ProviderProbeVerdictView::kTimeout;
    case 6:
      return mojom::ProviderProbeVerdictView::kNetwork;
    case 7:
      return mojom::ProviderProbeVerdictView::kModelNotFound;
    case 8:
      return mojom::ProviderProbeVerdictView::kUnknown;
    case 9:
      return mojom::ProviderProbeVerdictView::kEndpointReached;
    case 10:
      return mojom::ProviderProbeVerdictView::kNoModelListed;
    default:
      return std::nullopt;
  }
}

// The registry state of one stored credential. Held equal to
// core_service::ProviderCredentialState member for member and wire value for
// wire value by cross_contracts.py. There is no ABSENT member: a provider
// holding no credential carries no state at all, which is why the roster field
// is optional.
constexpr std::optional<mojom::ProviderCredentialStateView>
ProviderCredentialStateViewFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::ProviderCredentialStateView::kUsable;
    case 1:
      return mojom::ProviderCredentialStateView::kNeedsSignIn;
    case 2:
      return mojom::ProviderCredentialStateView::kRefreshFailed;
    default:
      return std::nullopt;
  }
}

// The last thing a vendor refused a request with, when the credential itself is
// fine. Closed at the three a surface can say something useful about: a
// credential state cannot express 'this key works and the vendor is refusing to
// spend it', because USABLE, NEEDS_SIGN_IN and REFRESH_FAILED are all
// statements about the key.
constexpr std::optional<mojom::ProviderRefusalView>
ProviderRefusalViewFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::ProviderRefusalView::kRateLimit;
    case 1:
      return mojom::ProviderRefusalView::kBilling;
    case 2:
      return mojom::ProviderRefusalView::kOverloaded;
    default:
      return std::nullopt;
  }
}

// Whether a roster row came from the compiled catalog or is a person's own
// provider.
constexpr std::optional<mojom::ProviderOriginView>
ProviderOriginViewFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::ProviderOriginView::kCatalog;
    case 1:
      return mojom::ProviderOriginView::kCustom;
    default:
      return std::nullopt;
  }
}

// Which catalog layer supplied a roster row, for a surface that explains where
// a provider came from: compiled into this release, served and cached on the
// device, or the person’s own.
constexpr std::optional<mojom::CatalogLayerView>
CatalogLayerViewFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::CatalogLayerView::kEmbeddedBaseline;
    case 1:
      return mojom::CatalogLayerView::kRemoteOverlay;
    case 2:
      return mojom::CatalogLayerView::kUserOverride;
    default:
      return std::nullopt;
  }
}

// One rung of the thinking ladder, ordered from least to most. Held equal to
// core_service::ThinkingLevel member for member and wire value for wire value.
// There is no member for 'let Taffy decide': that is the absence of a
// preference, which is why the roster field is optional rather than carrying a
// rung that means no rung.
constexpr std::optional<mojom::ThinkingLevelView>
ThinkingLevelViewFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::ThinkingLevelView::kOff;
    case 1:
      return mojom::ThinkingLevelView::kMinimal;
    case 2:
      return mojom::ThinkingLevelView::kLow;
    case 3:
      return mojom::ThinkingLevelView::kMedium;
    case 4:
      return mojom::ThinkingLevelView::kHigh;
    case 5:
      return mojom::ThinkingLevelView::kXhigh;
    case 6:
      return mojom::ThinkingLevelView::kMax;
    default:
      return std::nullopt;
  }
}

// What a model is cataloged to be used for. A surface groups a picker by these
// rather than inventing its own idea of what a model is good at.
constexpr std::optional<mojom::ModelRoleView>
ModelRoleViewFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::ModelRoleView::kPrimaryReasoning;
    case 1:
      return mojom::ModelRoleView::kFastBrowsing;
    case 2:
      return mojom::ModelRoleView::kVision;
    case 3:
      return mojom::ModelRoleView::kEmbedding;
    default:
      return std::nullopt;
  }
}

// What a model accepts as input.
constexpr std::optional<mojom::InputModalityView>
InputModalityViewFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::InputModalityView::kText;
    case 1:
      return mojom::InputModalityView::kImage;
    default:
      return std::nullopt;
  }
}

// What a person's own endpoint turned out to be, as the probe read it from the
// server's own answer. Named so a surface can say what it found rather than
// asking the person to confirm what they already typed; never inferred from a
// port alone.
constexpr std::optional<mojom::ServerKindView>
ServerKindViewFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::ServerKindView::kOpenaiCompatible;
    case 1:
      return mojom::ServerKindView::kOllama;
    case 2:
      return mojom::ServerKindView::kLmStudio;
    case 3:
      return mojom::ServerKindView::kVllm;
    case 4:
      return mojom::ServerKindView::kLlamaCpp;
    default:
      return std::nullopt;
  }
}

// Closed availability of a transient saved-flow review.
constexpr std::optional<mojom::SavedFlowQueryAvailability>
SavedFlowQueryAvailabilityFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::SavedFlowQueryAvailability::kAvailable;
    case 1:
      return mojom::SavedFlowQueryAvailability::kPrivateProfile;
    case 2:
      return mojom::SavedFlowQueryAvailability::kUnavailable;
    case 3:
      return mojom::SavedFlowQueryAvailability::kInvalidRequest;
    case 4:
      return mojom::SavedFlowQueryAvailability::kStaleRequest;
    default:
      return std::nullopt;
  }
}

}  // namespace taffy::core_api::wire

#endif  // TAFFY_CONTRACTS_CORE_API_GENERATED_CPP_CORE_API_ENUMS_H_
