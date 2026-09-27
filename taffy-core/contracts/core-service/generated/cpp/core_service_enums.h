// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Generated from schema/contract.json. Do not edit.
// Contract core_service 2.113.

#ifndef TAFFY_CONTRACTS_CORE_SERVICE_GENERATED_CPP_CORE_SERVICE_ENUMS_H_
#define TAFFY_CONTRACTS_CORE_SERVICE_GENERATED_CPP_CORE_SERVICE_ENUMS_H_

#include <stdint.h>

#include <optional>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom-shared.h"

// Closed-enumeration decoders for this contract. Every enumeration here is
// closed: a wire integer that names no member is not a member, and these return
// std::nullopt for it instead of forming an out-of-range enumerator, which is
// undefined behaviour and, at a trust seam, a fail-open. Any caller holding an
// integer must come through here.

namespace taffy::core_service::wire {

// Commands accepted by the ordered Rust core sequence.
constexpr std::optional<mojom::CoreServiceCommandKind>
CoreServiceCommandKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::CoreServiceCommandKind::kStartTask;
    case 1:
      return mojom::CoreServiceCommandKind::kCancelTask;
    case 2:
      return mojom::CoreServiceCommandKind::kUserDecision;
    case 3:
      return mojom::CoreServiceCommandKind::kAuthCallback;
    case 4:
      return mojom::CoreServiceCommandKind::kPermissionResult;
    case 5:
      return mojom::CoreServiceCommandKind::kStartAuth;
    case 6:
      return mojom::CoreServiceCommandKind::kRequestEmailLink;
    case 7:
      return mojom::CoreServiceCommandKind::kSignOut;
    case 8:
      return mojom::CoreServiceCommandKind::kAuthCredentialResult;
    case 9:
      return mojom::CoreServiceCommandKind::kCorrectWorkspaceFact;
    case 10:
      return mojom::CoreServiceCommandKind::kExcludeWorkspaceSource;
    case 11:
      return mojom::CoreServiceCommandKind::kRequestWorkspaceExport;
    case 12:
      return mojom::CoreServiceCommandKind::kSetAssetDeliveryPolicy;
    case 13:
      return mojom::CoreServiceCommandKind::kRequestAsset;
    case 14:
      return mojom::CoreServiceCommandKind::kRemoveAsset;
    case 15:
      return mojom::CoreServiceCommandKind::kSaveProviderCredential;
    case 16:
      return mojom::CoreServiceCommandKind::kForgetProviderCredential;
    case 17:
      return mojom::CoreServiceCommandKind::kStartProviderAuth;
    case 18:
      return mojom::CoreServiceCommandKind::kProviderAuthCallback;
    case 19:
      return mojom::CoreServiceCommandKind::kSaveCustomProvider;
    case 20:
      return mojom::CoreServiceCommandKind::kRemoveCustomProvider;
    case 21:
      return mojom::CoreServiceCommandKind::kCompleteHandover;
    case 22:
      return mojom::CoreServiceCommandKind::kExpireHandover;
    case 23:
      return mojom::CoreServiceCommandKind::kSupplyUserInput;
    case 24:
      return mojom::CoreServiceCommandKind::kSetProviderCredentialState;
    case 25:
      return mojom::CoreServiceCommandKind::kProbeProviderCredential;
    case 26:
      return mojom::CoreServiceCommandKind::kSupplyFieldValues;
    case 27:
      return mojom::CoreServiceCommandKind::kSetProviderModelPreference;
    case 28:
      return mojom::CoreServiceCommandKind::kProbeCustomEndpoint;
    case 29:
      return mojom::CoreServiceCommandKind::kRequestComposerCompletion;
    case 30:
      return mojom::CoreServiceCommandKind::kCancelComposerCompletion;
    case 31:
      return mojom::CoreServiceCommandKind::kPauseTask;
    case 32:
      return mojom::CoreServiceCommandKind::kResumeTask;
    case 33:
      return mojom::CoreServiceCommandKind::kTakeOver;
    case 34:
      return mojom::CoreServiceCommandKind::kSetAssistantConfiguration;
    case 35:
      return mojom::CoreServiceCommandKind::kSaveWorkspace;
    case 36:
      return mojom::CoreServiceCommandKind::kRenameWorkspace;
    case 37:
      return mojom::CoreServiceCommandKind::kDeleteWorkspace;
    case 38:
      return mojom::CoreServiceCommandKind::kDiscardWorkspace;
    case 39:
      return mojom::CoreServiceCommandKind::kSearchLibrary;
    case 40:
      return mojom::CoreServiceCommandKind::kSaveLibraryFact;
    case 41:
      return mojom::CoreServiceCommandKind::kRemoveLibraryEntry;
    case 42:
      return mojom::CoreServiceCommandKind::kRequestLibraryExport;
    case 43:
      return mojom::CoreServiceCommandKind::kSearchMemory;
    case 44:
      return mojom::CoreServiceCommandKind::kUpsertMemory;
    case 45:
      return mojom::CoreServiceCommandKind::kDeleteMemory;
    case 46:
      return mojom::CoreServiceCommandKind::kAcceptTaskArtifact;
    case 47:
      return mojom::CoreServiceCommandKind::kExportTaskArtifact;
    case 48:
      return mojom::CoreServiceCommandKind::kReplaceSavedDataSnapshot;
    case 49:
      return mojom::CoreServiceCommandKind::kMutateSkill;
    case 50:
      return mojom::CoreServiceCommandKind::kCancelProviderAuth;
    case 51:
      return mojom::CoreServiceCommandKind::kFollowUp;
    default:
      return std::nullopt;
  }
}

// Whether one Chromium-owned saved-data store supplied a complete bounded
// snapshot. Private profiles are always unavailable.
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

// Asynchronous work emitted by the deterministic reducer.
constexpr std::optional<mojom::EffectKind> EffectKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::EffectKind::kStorageCommit;
    case 1:
      return mojom::EffectKind::kPageObservation;
    case 2:
      return mojom::EffectKind::kModelRequest;
    case 3:
      return mojom::EffectKind::kNetworkRequest;
    case 4:
      return mojom::EffectKind::kBrowserAction;
    case 5:
      return mojom::EffectKind::kToolJob;
    case 6:
      return mojom::EffectKind::kSecureStore;
    case 7:
      return mojom::EffectKind::kOpenAuthSurface;
    case 8:
      return mojom::EffectKind::kRequestPermission;
    case 9:
      return mojom::EffectKind::kDeliverAsset;
    case 10:
      return mojom::EffectKind::kFetchCatalog;
    case 11:
      return mojom::EffectKind::kFetchProviderListing;
    case 12:
      return mojom::EffectKind::kDeliverComposerCompletion;
    case 13:
      return mojom::EffectKind::kProbeCustomEndpoint;
    default:
      return std::nullopt;
  }
}

// The one catalog operation the browser performs. A closed set of one, so the
// effect can never be re-pointed at an operation nobody reviewed.
constexpr std::optional<mojom::CatalogNetworkOperation>
CatalogNetworkOperationFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::CatalogNetworkOperation::kFetchPublishedCatalog;
    default:
      return std::nullopt;
  }
}

// What the browser's one catalog fetch did. Closed: an unknown disposition is
// refused, never coerced.
constexpr std::optional<mojom::CatalogFetchDisposition>
CatalogFetchDispositionFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::CatalogFetchDisposition::kSuccess;
    case 1:
      return mojom::CatalogFetchDisposition::kNotModified;
    case 2:
      return mojom::CatalogFetchDisposition::kUnavailable;
    case 3:
      return mojom::CatalogFetchDisposition::kOversized;
    case 4:
      return mojom::CatalogFetchDisposition::kMalformedTransport;
    default:
      return std::nullopt;
  }
}

// Whether recovery may repeat an interrupted effect.
constexpr std::optional<mojom::RetryClass> RetryClassFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::RetryClass::kIdempotent;
    case 1:
      return mojom::RetryClass::kConsequential;
    case 2:
      return mojom::RetryClass::kNever;
    default:
      return std::nullopt;
  }
}

// Closed terminal result returned by a browser broker.
constexpr std::optional<mojom::EffectStatus>
EffectStatusFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::EffectStatus::kCompleted;
    case 1:
      return mojom::EffectStatus::kDenied;
    case 2:
      return mojom::EffectStatus::kCancelled;
    case 3:
      return mojom::EffectStatus::kDeadlineExceeded;
    case 4:
      return mojom::EffectStatus::kResourceLimit;
    case 5:
      return mojom::EffectStatus::kUnavailable;
    case 6:
      return mojom::EffectStatus::kOutcomeUnknown;
    case 7:
      return mojom::EffectStatus::kInvalidResult;
    default:
      return std::nullopt;
  }
}

// The fixed disclosure class checked again by the network broker.
constexpr std::optional<mojom::DisclosureClass>
DisclosureClassFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::DisclosureClass::kContentFree;
    case 1:
      return mojom::DisclosureClass::kAccountMetadata;
    case 2:
      return mojom::DisclosureClass::kUserSelectedContent;
    case 3:
      return mojom::DisclosureClass::kPageContent;
    default:
      return std::nullopt;
  }
}

// Closed result of restoring one profile core generation.
constexpr std::optional<mojom::InitializationStatus>
InitializationStatusFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::InitializationStatus::kReady;
    case 1:
      return mojom::InitializationStatus::kInvalidBootstrap;
    case 2:
      return mojom::InitializationStatus::kIncompatibleVersion;
    case 3:
      return mojom::InitializationStatus::kResourceLimit;
    default:
      return std::nullopt;
  }
}

// Immediate result of admitting work to the ordered core queue.
constexpr std::optional<mojom::AdmissionStatus>
AdmissionStatusFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::AdmissionStatus::kAccepted;
    case 1:
      return mojom::AdmissionStatus::kStaleGeneration;
    case 2:
      return mojom::AdmissionStatus::kStaleRevision;
    case 3:
      return mojom::AdmissionStatus::kDeadlineExceeded;
    case 4:
      return mojom::AdmissionStatus::kBackpressure;
    case 5:
      return mojom::AdmissionStatus::kInvalidCommand;
    case 6:
      return mojom::AdmissionStatus::kCoreUnavailable;
    case 7:
      return mojom::AdmissionStatus::kDuplicate;
    default:
      return std::nullopt;
  }
}

// The closed task families supported by the reducer.
constexpr std::optional<mojom::TaskKind> TaskKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::TaskKind::kResearch;
    case 1:
      return mojom::TaskKind::kErrand;
    default:
      return std::nullopt;
  }
}

// The closed reviewed product workflow frozen into one task.
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

// Stable identity of one compiled built-in skill in product catalogue order.
constexpr std::optional<mojom::BuiltinSkillId>
BuiltinSkillIdFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::BuiltinSkillId::kGeneralWebResearch;
    case 1:
      return mojom::BuiltinSkillId::kDeepResearch;
    case 2:
      return mojom::BuiltinSkillId::kProductComparison;
    case 3:
      return mojom::BuiltinSkillId::kMultiTabComparison;
    case 4:
      return mojom::BuiltinSkillId::kWebsiteSummarizer;
    case 5:
      return mojom::BuiltinSkillId::kPdfAnalysis;
    case 6:
      return mojom::BuiltinSkillId::kDataExtraction;
    case 7:
      return mojom::BuiltinSkillId::kFormAssistant;
    case 8:
      return mojom::BuiltinSkillId::kShopping;
    case 9:
      return mojom::BuiltinSkillId::kDownloadOrganizer;
    case 10:
      return mojom::BuiltinSkillId::kTravelResearch;
    case 11:
      return mojom::BuiltinSkillId::kVideoTranscriptAnalyzer;
    case 12:
      return mojom::BuiltinSkillId::kImageUnderstanding;
    case 13:
      return mojom::BuiltinSkillId::kLibraryBuilder;
    case 14:
      return mojom::BuiltinSkillId::kSpreadsheetBuilder;
    case 15:
      return mojom::BuiltinSkillId::kDocumentGenerator;
    default:
      return std::nullopt;
  }
}

// Closed provider route disclosed in the initial task consent preview.
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

// The actor relationship selected by the user without granting authority.
constexpr std::optional<mojom::TaskControlMode>
TaskControlModeFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::TaskControlMode::kUser;
    case 1:
      return mojom::TaskControlMode::kShared;
    case 2:
      return mojom::TaskControlMode::kAssistant;
    default:
      return std::nullopt;
  }
}

// The closed build milestone whose reviewed tool surface is frozen into the
// task seed.
constexpr std::optional<mojom::TaskMilestone>
TaskMilestoneFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::TaskMilestone::kM0;
    case 1:
      return mojom::TaskMilestone::kM1;
    case 2:
      return mojom::TaskMilestone::kM2;
    case 3:
      return mojom::TaskMilestone::kM3;
    case 4:
      return mojom::TaskMilestone::kM4;
    case 5:
      return mojom::TaskMilestone::kM5;
    case 6:
      return mojom::TaskMilestone::kM6;
    case 7:
      return mojom::TaskMilestone::kM7;
    case 8:
      return mojom::TaskMilestone::kM8;
    default:
      return std::nullopt;
  }
}

// The bounded resources a task may consume.
constexpr std::optional<mojom::TaskBudgetKind>
TaskBudgetKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::TaskBudgetKind::kMaxSources;
    case 1:
      return mojom::TaskBudgetKind::kMaxWorkingTabs;
    case 2:
      return mojom::TaskBudgetKind::kMaxWallTimeMs;
    case 3:
      return mojom::TaskBudgetKind::kMaxModelRequests;
    case 4:
      return mojom::TaskBudgetKind::kMaxInputUnits;
    case 5:
      return mojom::TaskBudgetKind::kMaxOutputUnits;
    case 6:
      return mojom::TaskBudgetKind::kMaxCostUnits;
    case 7:
      return mojom::TaskBudgetKind::kMaxNavigationDepth;
    case 8:
      return mojom::TaskBudgetKind::kMaxRetriesPerStep;
    case 9:
      return mojom::TaskBudgetKind::kMaxArtifactBytes;
    default:
      return std::nullopt;
  }
}

// A visible user intent delivered for policy evaluation.
constexpr std::optional<mojom::UserDecisionKind>
UserDecisionKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::UserDecisionKind::kAccept;
    case 1:
      return mojom::UserDecisionKind::kDeny;
    case 2:
      return mojom::UserDecisionKind::kDismiss;
    default:
      return std::nullopt;
  }
}

// Why the browser requested task cancellation.
constexpr std::optional<mojom::CancelReason>
CancelReasonFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::CancelReason::kUser;
    case 1:
      return mojom::CancelReason::kProfileShutdown;
    case 2:
      return mojom::CancelReason::kDeadline;
    default:
      return std::nullopt;
  }
}

// Browser/service-private durable task settlement requested by one published
// reducer state.
constexpr std::optional<mojom::TaskSettlementKind>
TaskSettlementKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::TaskSettlementKind::kPause;
    case 1:
      return mojom::TaskSettlementKind::kCancel;
    default:
      return std::nullopt;
  }
}

// Closed terminal task states used only for browser authority cleanup.
constexpr std::optional<mojom::TerminalTaskKind>
TerminalTaskKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::TerminalTaskKind::kCompleted;
    case 1:
      return mojom::TerminalTaskKind::kPartial;
    case 2:
      return mojom::TerminalTaskKind::kFailed;
    case 3:
      return mojom::TerminalTaskKind::kCancelled;
    default:
      return std::nullopt;
  }
}

// Closed durable effect alphabet emitted by task-engine after commit.
constexpr std::optional<mojom::TaskReducerEffectKind>
TaskReducerEffectKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::TaskReducerEffectKind::kRevokeAuthority;
    case 1:
      return mojom::TaskReducerEffectKind::kAskPolicy;
    case 2:
      return mojom::TaskReducerEffectKind::kRequestApproval;
    case 3:
      return mojom::TaskReducerEffectKind::kRequestPermission;
    case 4:
      return mojom::TaskReducerEffectKind::kDispatchAction;
    case 5:
      return mojom::TaskReducerEffectKind::kAwaitInFlightWork;
    case 6:
      return mojom::TaskReducerEffectKind::kReconcileAction;
    case 7:
      return mojom::TaskReducerEffectKind::kReleaseTaskTabs;
    case 8:
      return mojom::TaskReducerEffectKind::kGenerateArtifact;
    case 9:
      return mojom::TaskReducerEffectKind::kExportArtifact;
    case 10:
      return mojom::TaskReducerEffectKind::kCallModel;
    case 11:
      return mojom::TaskReducerEffectKind::kAwaitHandover;
    case 12:
      return mojom::TaskReducerEffectKind::kRunToolJob;
    case 13:
      return mojom::TaskReducerEffectKind::kRequestFieldValues;
    case 14:
      return mojom::TaskReducerEffectKind::kPrepareDiscoveryTab;
    case 15:
      return mojom::TaskReducerEffectKind::kRunLibraryTool;
    case 16:
      return mojom::TaskReducerEffectKind::kRunMemoryTool;
    default:
      return std::nullopt;
  }
}

// Closed reason recorded by the reducer before browser revocation.
constexpr std::optional<mojom::TaskRevocationReason>
TaskRevocationReasonFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::TaskRevocationReason::kUserTookOver;
    case 1:
      return mojom::TaskRevocationReason::kDirectUserInput;
    case 2:
      return mojom::TaskRevocationReason::kTaskCancelled;
    case 3:
      return mojom::TaskRevocationReason::kPolicyRevoked;
    case 4:
      return mojom::TaskRevocationReason::kTabClosed;
    default:
      return std::nullopt;
  }
}

// Closed reconciliation rule frozen by the reviewed tool registry.
constexpr std::optional<mojom::TaskRecoveryRule>
TaskRecoveryRuleFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::TaskRecoveryRule::kRetryWithinEpochAndBudget;
    case 1:
      return mojom::TaskRecoveryRule::kRetryAfterStateCheck;
    case 2:
      return mojom::TaskRecoveryRule::kReconcileFirst;
    case 3:
      return mojom::TaskRecoveryRule::kNeverAutomatically;
    default:
      return std::nullopt;
  }
}

// Closed deterministic task artifact formats.
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

// Closed browser checks that must all hold immediately before dispatch.
constexpr std::optional<mojom::TaskActionPrecondition>
TaskActionPreconditionFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::TaskActionPrecondition::kDocumentUnchanged;
    case 1:
      return mojom::TaskActionPrecondition::kGraphRevisionAtLeast;
    case 2:
      return mojom::TaskActionPrecondition::kNodePresent;
    case 3:
      return mojom::TaskActionPrecondition::kDestinationUnchanged;
    default:
      return std::nullopt;
  }
}

// Closed evidence required before an action is reported successful.
constexpr std::optional<mojom::TaskActionPostcondition>
TaskActionPostconditionFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::TaskActionPostcondition::kObservationCaptured;
    case 1:
      return mojom::TaskActionPostcondition::kDocumentNavigated;
    case 2:
      return mojom::TaskActionPostcondition::kNodeStateChanged;
    case 3:
      return mojom::TaskActionPostcondition::kDownloadStarted;
    case 4:
      return mojom::TaskActionPostcondition::kPlatformAcknowledged;
    case 5:
      return mojom::TaskActionPostcondition::kTaskTabsListed;
    case 6:
      return mojom::TaskActionPostcondition::kTaskTabActive;
    case 7:
      return mojom::TaskActionPostcondition::kTaskTabAbsent;
    case 8:
      return mojom::TaskActionPostcondition::kDownloadCancelled;
    case 9:
      return mojom::TaskActionPostcondition::kPageReloaded;
    case 10:
      return mojom::TaskActionPostcondition::kLoadingStopped;
    case 11:
      return mojom::TaskActionPostcondition::kStoreRowsListed;
    default:
      return std::nullopt;
  }
}

// The browser-proven task-tab state carried by a successful terminal.
constexpr std::optional<mojom::TaskTabPostcondition>
TaskTabPostconditionFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::TaskTabPostcondition::kListed;
    case 1:
      return mojom::TaskTabPostcondition::kActive;
    case 2:
      return mojom::TaskTabPostcondition::kAbsent;
    default:
      return std::nullopt;
  }
}

// The browser-proven task download state carried by a successful terminal.
constexpr std::optional<mojom::TaskDownloadPostcondition>
TaskDownloadPostconditionFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::TaskDownloadPostcondition::kStarted;
    case 1:
      return mojom::TaskDownloadPostcondition::kListed;
    case 2:
      return mojom::TaskDownloadPostcondition::kCancelled;
    default:
      return std::nullopt;
  }
}

// Content-free lifecycle state read from Chromium's download manager.
constexpr std::optional<mojom::TaskDownloadState>
TaskDownloadStateFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::TaskDownloadState::kCreated;
    case 1:
      return mojom::TaskDownloadState::kInProgress;
    case 2:
      return mojom::TaskDownloadState::kPaused;
    case 3:
      return mojom::TaskDownloadState::kComplete;
    case 4:
      return mojom::TaskDownloadState::kInterrupted;
    case 5:
      return mojom::TaskDownloadState::kCancelled;
    default:
      return std::nullopt;
  }
}

// Only the registered top-level media class; no server-authored subtype or text
// crosses the seam.
constexpr std::optional<mojom::TaskDownloadMediaType>
TaskDownloadMediaTypeFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::TaskDownloadMediaType::kUnknown;
    case 1:
      return mojom::TaskDownloadMediaType::kApplication;
    case 2:
      return mojom::TaskDownloadMediaType::kAudio;
    case 3:
      return mojom::TaskDownloadMediaType::kFont;
    case 4:
      return mojom::TaskDownloadMediaType::kImage;
    case 5:
      return mojom::TaskDownloadMediaType::kMessage;
    case 6:
      return mojom::TaskDownloadMediaType::kModel;
    case 7:
      return mojom::TaskDownloadMediaType::kMultipart;
    case 8:
      return mojom::TaskDownloadMediaType::kText;
    case 9:
      return mojom::TaskDownloadMediaType::kVideo;
    default:
      return std::nullopt;
  }
}

// A browser-owned directory class rather than a file-system path.
constexpr std::optional<mojom::TaskDownloadDirectoryClass>
TaskDownloadDirectoryClassFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::TaskDownloadDirectoryClass::kUndecided;
    case 1:
      return mojom::TaskDownloadDirectoryClass::kPersonChosen;
    case 2:
      return mojom::TaskDownloadDirectoryClass::kDefaultDownloads;
    case 3:
      return mojom::TaskDownloadDirectoryClass::kApplicationPrivate;
    default:
      return std::nullopt;
  }
}

// Closed terminal result for one externalized reducer effect.
constexpr std::optional<mojom::TaskEffectCompletionStatus>
TaskEffectCompletionStatusFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::TaskEffectCompletionStatus::kSucceeded;
    case 1:
      return mojom::TaskEffectCompletionStatus::kRefused;
    case 2:
      return mojom::TaskEffectCompletionStatus::kUnavailable;
    case 3:
      return mojom::TaskEffectCompletionStatus::kCancelled;
    case 4:
      return mojom::TaskEffectCompletionStatus::kOutcomeUnknown;
    case 5:
      return mojom::TaskEffectCompletionStatus::kValueReferenceUnknown;
    default:
      return std::nullopt;
  }
}

// The structural outcome delivered by a platform auth callback.
constexpr std::optional<mojom::AuthCallbackStatus>
AuthCallbackStatusFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::AuthCallbackStatus::kAuthorizationCode;
    case 1:
      return mojom::AuthCallbackStatus::kDenied;
    case 2:
      return mojom::AuthCallbackStatus::kProviderError;
    case 3:
      return mojom::AuthCallbackStatus::kDeadlineExceeded;
    case 4:
      return mojom::AuthCallbackStatus::kPlatformUnavailable;
    default:
      return std::nullopt;
  }
}

// Closed account methods shared by Core API, portable Rust, and the pinned
// account broker.
constexpr std::optional<mojom::AccountAuthMethod>
AccountAuthMethodFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::AccountAuthMethod::kGoogle;
    case 1:
      return mojom::AccountAuthMethod::kEmailLink;
    case 2:
      return mojom::AccountAuthMethod::kGithub;
    case 3:
      return mojom::AccountAuthMethod::kFacebook;
    default:
      return std::nullopt;
  }
}

// Terminal disposition of one transient sandboxed token-response validation.
constexpr std::optional<mojom::AccountTokenValidationStatus>
AccountTokenValidationStatusFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::AccountTokenValidationStatus::kValidated;
    case 1:
      return mojom::AccountTokenValidationStatus::kInvalidResponse;
    case 2:
      return mojom::AccountTokenValidationStatus::kStaleGeneration;
    case 3:
      return mojom::AccountTokenValidationStatus::kDeadlineExceeded;
    case 4:
      return mojom::AccountTokenValidationStatus::kResourceLimit;
    default:
      return std::nullopt;
  }
}

// Closed account claims accepted by the portable authorization protocol.
constexpr std::optional<mojom::AccountScope>
AccountScopeFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::AccountScope::kOpenId;
    case 1:
      return mojom::AccountScope::kEmail;
    case 2:
      return mojom::AccountScope::kProfile;
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

// The closed native permissions the core may request.
constexpr std::optional<mojom::PlatformPermission>
PlatformPermissionFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::PlatformPermission::kNotifications;
    case 1:
      return mojom::PlatformPermission::kMicrophone;
    case 2:
      return mojom::PlatformPermission::kCamera;
    case 3:
      return mojom::PlatformPermission::kLocation;
    default:
      return std::nullopt;
  }
}

// The closed result of a visible native permission request.
constexpr std::optional<mojom::PermissionDecision>
PermissionDecisionFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::PermissionDecision::kGranted;
    case 1:
      return mojom::PermissionDecision::kDenied;
    case 2:
      return mojom::PermissionDecision::kDismissed;
    case 3:
      return mojom::PermissionDecision::kUnavailable;
    default:
      return std::nullopt;
  }
}

// The closed storage operations implemented by the browser single writer.
constexpr std::optional<mojom::StorageOperation>
StorageOperationFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::StorageOperation::kAppendTaskCommit;
    case 1:
      return mojom::StorageOperation::kQueryWorkspace;
    case 2:
      return mojom::StorageOperation::kDeleteSource;
    case 3:
      return mojom::StorageOperation::kUpsertWorkspace;
    case 4:
      return mojom::StorageOperation::kInstallSkill;
    case 5:
      return mojom::StorageOperation::kSetSkillStatus;
    case 6:
      return mojom::StorageOperation::kRecordSkillRun;
    case 7:
      return mojom::StorageOperation::kForgetSkill;
    case 8:
      return mojom::StorageOperation::kSetAssistantConfiguration;
    case 9:
      return mojom::StorageOperation::kDeleteWorkspace;
    case 10:
      return mojom::StorageOperation::kUpsertLibraryEntry;
    case 11:
      return mojom::StorageOperation::kRemoveLibraryEntry;
    case 12:
      return mojom::StorageOperation::kUpsertMemory;
    case 13:
      return mojom::StorageOperation::kDeleteMemory;
    default:
      return std::nullopt;
  }
}

// The provenance class copied from the exact kept workspace fact.
constexpr std::optional<mojom::LibraryFactKind>
LibraryFactKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::LibraryFactKind::kFromPage;
    case 1:
      return mojom::LibraryFactKind::kSummarized;
    case 2:
      return mojom::LibraryFactKind::kTaffyInference;
    case 3:
      return mojom::LibraryFactKind::kUserEntered;
    default:
      return std::nullopt;
  }
}

// The immutable, visible reason one Memory record exists.
constexpr std::optional<mojom::MemorySourceKind>
MemorySourceKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::MemorySourceKind::kUserEntered;
    case 1:
      return mojom::MemorySourceKind::kAcceptedTaskSuggestion;
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

// Visible handling class for one explicitly retained preference.
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

// One compiled-in ability group. Disabling a group removes its tools and task
// templates from the effective set; it never changes authority or safety rules.
constexpr std::optional<mojom::AssistantAbility>
AssistantAbilityFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::AssistantAbility::kPagesLookup;
    case 1:
      return mojom::AssistantAbility::kPagesCompare;
    case 2:
      return mojom::AssistantAbility::kPagesSummarize;
    case 3:
      return mojom::AssistantAbility::kPagesTable;
    case 4:
      return mojom::AssistantAbility::kProducts;
    case 5:
      return mojom::AssistantAbility::kOffers;
    case 6:
      return mojom::AssistantAbility::kForm;
    case 7:
      return mojom::AssistantAbility::kDownloads;
    case 8:
      return mojom::AssistantAbility::kPdf;
    case 9:
      return mojom::AssistantAbility::kSheet;
    case 10:
      return mojom::AssistantAbility::kDocument;
    case 11:
      return mojom::AssistantAbility::kDepth;
    case 12:
      return mojom::AssistantAbility::kTrip;
    case 13:
      return mojom::AssistantAbility::kPictures;
    case 14:
      return mojom::AssistantAbility::kVideo;
    case 15:
      return mojom::AssistantAbility::kKeep;
    default:
      return std::nullopt;
  }
}

// A response-style starting point. It changes presentation and planning cadence
// only, never permissions, authority, safety, or tool availability.
constexpr std::optional<mojom::PersonalityPreset>
PersonalityPresetFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::PersonalityPreset::kCarefulResearcher;
    case 1:
      return mojom::PersonalityPreset::kQuickShopper;
    case 2:
      return mojom::PersonalityPreset::kTripPlanner;
    default:
      return std::nullopt;
  }
}

// Where a stored sequence of steps came from. A field a person is shown and an
// audit record names, and deliberately not an input to any authority decision:
// provenance is a fact about the past page, and every security question here is
// about the page in front of the assistant now (decision 0055 section 2).
constexpr std::optional<mojom::SkillProvenance>
SkillProvenanceFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::SkillProvenance::kAuthored;
    case 1:
      return mojom::SkillProvenance::kRecordedFromTask;
    case 2:
      return mojom::SkillProvenance::kInstalledFromPack;
    default:
      return std::nullopt;
  }
}

// The four person-visible changes supported by the portable saved-skill
// aggregate.
constexpr std::optional<mojom::SkillMutationKind>
SkillMutationKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::SkillMutationKind::kTeach;
    case 1:
      return mojom::SkillMutationKind::kUpdate;
    case 2:
      return mojom::SkillMutationKind::kSetEnabled;
    case 3:
      return mojom::SkillMutationKind::kRemove;
    default:
      return std::nullopt;
  }
}

// One closed shape of page fact captured by a browser-owned semantic
// observation.
constexpr std::optional<mojom::SkillClauseKind>
SkillClauseKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::SkillClauseKind::kRolePresent;
    case 1:
      return mojom::SkillClauseKind::kPhraseAt;
    case 2:
      return mojom::SkillClauseKind::kStateAt;
    default:
      return std::nullopt;
  }
}

// Closed arguments for reviewed saved steps. Public starting addresses are
// explicit; semantic targets retain compiled vocabulary only.
constexpr std::optional<mojom::SkillArgumentKind>
SkillArgumentKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::SkillArgumentKind::kFromEarlierStep;
    case 1:
      return mojom::SkillArgumentKind::kFromPerson;
    case 2:
      return mojom::SkillArgumentKind::kChoice;
    case 3:
      return mojom::SkillArgumentKind::kCount;
    case 4:
      return mojom::SkillArgumentKind::kFlag;
    case 5:
      return mojom::SkillArgumentKind::kPublicAddress;
    case 6:
      return mojom::SkillArgumentKind::kSemanticTarget;
    default:
      return std::nullopt;
  }
}

// The one lifecycle a skill has, whatever its provenance. DISABLED is reachable
// from any of the others and reachable only by a person.
constexpr std::optional<mojom::SkillStatus>
SkillStatusFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::SkillStatus::kDraft;
    case 1:
      return mojom::SkillStatus::kActive;
    case 2:
      return mojom::SkillStatus::kSuperseded;
    case 3:
      return mojom::SkillStatus::kRetired;
    case 4:
      return mojom::SkillStatus::kDisabled;
    default:
      return std::nullopt;
  }
}

// How one replay of a skill ended. Content-free by construction: five closed
// members and no page text, because the journal's shape is what keeps page
// content out of durable storage.
constexpr std::optional<mojom::SkillRunOutcome>
SkillRunOutcomeFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::SkillRunOutcome::kCompleted;
    case 1:
      return mojom::SkillRunOutcome::kRefused;
    case 2:
      return mojom::SkillRunOutcome::kAbandoned;
    case 3:
      return mojom::SkillRunOutcome::kUnavailable;
    default:
      return std::nullopt;
  }
}

// Closed deterministic workspace export formats.
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

// Closed deterministic page snapshot export formats rendered only by sandboxed
// Rust.
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

// Closed terminal status for one isolated page snapshot export operation.
constexpr std::optional<mojom::PageSnapshotExportStatus>
PageSnapshotExportStatusFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::PageSnapshotExportStatus::kExported;
    case 1:
      return mojom::PageSnapshotExportStatus::kStalePage;
    case 2:
      return mojom::PageSnapshotExportStatus::kPrivateProfile;
    case 3:
      return mojom::PageSnapshotExportStatus::kIncomplete;
    case 4:
      return mojom::PageSnapshotExportStatus::kOversize;
    case 5:
      return mojom::PageSnapshotExportStatus::kMalformed;
    case 6:
      return mojom::PageSnapshotExportStatus::kCancelled;
    case 7:
      return mojom::PageSnapshotExportStatus::kReplayConflict;
    case 8:
      return mojom::PageSnapshotExportStatus::kUnavailable;
    default:
      return std::nullopt;
  }
}

// Closed terminal status for matching one exact live page against the restored
// saved-skill catalogue.
constexpr std::optional<mojom::SiteSkillMatchStatus>
SiteSkillMatchStatusFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::SiteSkillMatchStatus::kAvailable;
    case 1:
      return mojom::SiteSkillMatchStatus::kStalePage;
    case 2:
      return mojom::SiteSkillMatchStatus::kPrivateProfile;
    case 3:
      return mojom::SiteSkillMatchStatus::kIncomplete;
    case 4:
      return mojom::SiteSkillMatchStatus::kMalformed;
    case 5:
      return mojom::SiteSkillMatchStatus::kUnavailable;
    default:
      return std::nullopt;
  }
}

// The bounded page-intelligence scope requested from the browser.
constexpr std::optional<mojom::ObservationScope>
ObservationScopeFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::ObservationScope::kCurrentDocument;
    case 1:
      return mojom::ObservationScope::kSelectedSources;
    default:
      return std::nullopt;
  }
}

// Closed browser-validated BIP observation outcome.
constexpr std::optional<mojom::BipObservationStatus>
BipObservationStatusFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::BipObservationStatus::kOk;
    case 1:
      return mojom::BipObservationStatus::kUnsupported;
    case 2:
      return mojom::BipObservationStatus::kIncomplete;
    case 3:
      return mojom::BipObservationStatus::kConflicted;
    case 4:
      return mojom::BipObservationStatus::kStalePageEpoch;
    case 5:
      return mojom::BipObservationStatus::kDocumentInactive;
    case 6:
      return mojom::BipObservationStatus::kBudgetExceeded;
    case 7:
      return mojom::BipObservationStatus::kDeadlineExceeded;
    case 8:
      return mojom::BipObservationStatus::kCancelled;
    case 9:
      return mojom::BipObservationStatus::kResourcePressure;
    case 10:
      return mojom::BipObservationStatus::kInternalError;
    default:
      return std::nullopt;
  }
}

// Closed encoding of the bounded semantic graph body.
constexpr std::optional<mojom::BipGraphEncoding>
BipGraphEncodingFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::BipGraphEncoding::kNone;
    case 1:
      return mojom::BipGraphEncoding::kBipContract;
    default:
      return std::nullopt;
  }
}

// The exact media vertical that produced bounded observation facts.
constexpr std::optional<mojom::MediaObservationKind>
MediaObservationKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::MediaObservationKind::kImage;
    case 1:
      return mojom::MediaObservationKind::kVideo;
    case 2:
      return mojom::MediaObservationKind::kPdf;
    case 3:
      return mojom::MediaObservationKind::kPageScreenshot;
    default:
      return std::nullopt;
  }
}

// The closed meaning of one generated media fact.
constexpr std::optional<mojom::MediaFactKind>
MediaFactKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::MediaFactKind::kDescription;
    case 1:
      return mojom::MediaFactKind::kOcrText;
    case 2:
      return mojom::MediaFactKind::kTranscript;
    case 3:
      return mojom::MediaFactKind::kPdfText;
    case 4:
      return mojom::MediaFactKind::kPdfTableRow;
    case 5:
      return mojom::MediaFactKind::kMetadata;
    default:
      return std::nullopt;
  }
}

// The closed source of one media fact; inference is explicit and never
// presented as native structure.
constexpr std::optional<mojom::MediaEvidenceKind>
MediaEvidenceKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::MediaEvidenceKind::kDom;
    case 1:
      return mojom::MediaEvidenceKind::kAccessibility;
    case 2:
      return mojom::MediaEvidenceKind::kCaptionTrack;
    case 3:
      return mojom::MediaEvidenceKind::kPdfTextLayer;
    case 4:
      return mojom::MediaEvidenceKind::kVisualInference;
    case 5:
      return mojom::MediaEvidenceKind::kTableHeuristic;
    default:
      return std::nullopt;
  }
}

// Highest browser-validated sensitivity present in the observation.
constexpr std::optional<mojom::BipSensitivity>
BipSensitivityFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::BipSensitivity::kNotSensitive;
    case 1:
      return mojom::BipSensitivity::kPersonal;
    case 2:
      return mojom::BipSensitivity::kAccount;
    case 3:
      return mojom::BipSensitivity::kPayment;
    case 4:
      return mojom::BipSensitivity::kIdentity;
    case 5:
      return mojom::BipSensitivity::kHealth;
    case 6:
      return mojom::BipSensitivity::kFinancial;
    case 7:
      return mojom::BipSensitivity::kLegal;
    case 8:
      return mojom::BipSensitivity::kPrivateCommunication;
    case 9:
      return mojom::BipSensitivity::kAdministration;
    case 10:
      return mojom::BipSensitivity::kCredential;
    case 11:
      return mojom::BipSensitivity::kUnknownSensitive;
    case 12:
      return mojom::BipSensitivity::kOneTimeCode;
    case 13:
      return mojom::BipSensitivity::kChallengeResponse;
    default:
      return std::nullopt;
  }
}

// Closed account operations resolved to compile-time routes only by the browser
// broker.
constexpr std::optional<mojom::AccountNetworkOperation>
AccountNetworkOperationFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::AccountNetworkOperation::kExchangeAuthorizationCode;
    case 1:
      return mojom::AccountNetworkOperation::kExchangeNativeCredential;
    case 2:
      return mojom::AccountNetworkOperation::kRequestEmailLink;
    case 3:
      return mojom::AccountNetworkOperation::kRefreshSession;
    case 4:
      return mojom::AccountNetworkOperation::kRevokeSession;
    case 5:
      return mojom::AccountNetworkOperation::kFetchEntitlement;
    default:
      return std::nullopt;
  }
}

// Why the core planned this entitlement fetch. Content-free; the browser may
// use it for its own log line and nothing else.
constexpr std::optional<mojom::EntitlementFetchReason>
EntitlementFetchReasonFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::EntitlementFetchReason::kBootstrap;
    case 1:
      return mojom::EntitlementFetchReason::kSignIn;
    case 2:
      return mojom::EntitlementFetchReason::kCadence;
    case 3:
      return mojom::EntitlementFetchReason::kQuotaRefused;
    default:
      return std::nullopt;
  }
}

// The closed browser actions a policy grant may propose.
constexpr std::optional<mojom::BrowserActionOperation>
BrowserActionOperationFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::BrowserActionOperation::kDispatch;
    case 1:
      return mojom::BrowserActionOperation::kReconcile;
    case 2:
      return mojom::BrowserActionOperation::kReleaseTaskTabs;
    case 3:
      return mojom::BrowserActionOperation::kExportArtifact;
    default:
      return std::nullopt;
  }
}

// Compile-time runtime families with separate process sandboxes.
constexpr std::optional<mojom::ToolRuntimeKind>
ToolRuntimeKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::ToolRuntimeKind::kPython;
    case 1:
      return mojom::ToolRuntimeKind::kLocalModel;
    case 2:
      return mojom::ToolRuntimeKind::kMedia;
    case 3:
      return mojom::ToolRuntimeKind::kWasm;
    default:
      return std::nullopt;
  }
}

// Closed tool operations; workers never receive command lines or executable
// source.
constexpr std::optional<mojom::ToolOperation>
ToolOperationFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::ToolOperation::kRunBundledPythonModule;
    case 1:
      return mojom::ToolOperation::kGenerateLocalModel;
    case 2:
      return mojom::ToolOperation::kProbeMedia;
    case 3:
      return mojom::ToolOperation::kExtractAudio;
    case 4:
      return mojom::ToolOperation::kSampleFrames;
    case 5:
      return mojom::ToolOperation::kTranscodePreset;
    case 6:
      return mojom::ToolOperation::kRunSignedWasmTransform;
    case 7:
      return mojom::ToolOperation::kEmbedLocalModel;
    default:
      return std::nullopt;
  }
}

// Closed on-device model artifact formats the browser knows how to open; the
// reducer names a registered model and never a format the browser did not
// register.
constexpr std::optional<mojom::ToolModelArtifactKind>
ToolModelArtifactKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::ToolModelArtifactKind::kLitertTflite;
    case 1:
      return mojom::ToolModelArtifactKind::kOnnxRuntime;
    case 2:
      return mojom::ToolModelArtifactKind::kGguf;
    default:
      return std::nullopt;
  }
}

// Closed element encoding of an embedding vector, stated so two processes never
// disagree about width or byte order.
constexpr std::optional<mojom::EmbeddingElementKind>
EmbeddingElementKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::EmbeddingElementKind::kFloat32Le;
    default:
      return std::nullopt;
  }
}

// Closed streaming tool-output representations.
constexpr std::optional<mojom::ToolChunkKind>
ToolChunkKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::ToolChunkKind::kTextUtf8;
    case 1:
      return mojom::ToolChunkKind::kBinary;
    default:
      return std::nullopt;
  }
}

// Closed local-model terminal reasons that do not expose provider details.
constexpr std::optional<mojom::LocalModelFinishReason>
LocalModelFinishReasonFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::LocalModelFinishReason::kStop;
    case 1:
      return mojom::LocalModelFinishReason::kTokenLimit;
    default:
      return std::nullopt;
  }
}

// Closed content-free tool worker terminal outcomes.
constexpr std::optional<mojom::ToolTerminalStatus>
ToolTerminalStatusFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::ToolTerminalStatus::kCompleted;
    case 1:
      return mojom::ToolTerminalStatus::kCancelled;
    case 2:
      return mojom::ToolTerminalStatus::kDeadlineExceeded;
    case 3:
      return mojom::ToolTerminalStatus::kResourceLimit;
    case 4:
      return mojom::ToolTerminalStatus::kRuntimeCrashed;
    case 5:
      return mojom::ToolTerminalStatus::kInvalidInput;
    case 6:
      return mojom::ToolTerminalStatus::kUnsupported;
    case 7:
      return mojom::ToolTerminalStatus::kOutcomeUnknown;
    case 8:
      return mojom::ToolTerminalStatus::kModelArtifactMissing;
    case 9:
      return mojom::ToolTerminalStatus::kModelArtifactIncompatible;
    case 10:
      return mojom::ToolTerminalStatus::kLocalRuntimeUnavailable;
    default:
      return std::nullopt;
  }
}

// The closed operations on opaque platform secret handles.
constexpr std::optional<mojom::SecureStoreOperation>
SecureStoreOperationFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::SecureStoreOperation::kGenerateEntropy;
    case 1:
      return mojom::SecureStoreOperation::kWriteTransient;
    case 2:
      return mojom::SecureStoreOperation::kDeleteHandle;
    default:
      return std::nullopt;
  }
}

// Closed purpose binding for transient secure material.
constexpr std::optional<mojom::SecretMaterialPurpose>
SecretMaterialPurposeFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::SecretMaterialPurpose::kPkceVerifier;
    case 1:
      return mojom::SecretMaterialPurpose::kGoogleRawNonce;
    default:
      return std::nullopt;
  }
}

// Closed visible platform account surfaces.
constexpr std::optional<mojom::AuthSurfaceOperation>
AuthSurfaceOperationFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::AuthSurfaceOperation::kOpenOauth;
    case 1:
      return mojom::AuthSurfaceOperation::kRequestNativeCredential;
    default:
      return std::nullopt;
  }
}

// The closed terminal outcome reported by the browser action broker.
constexpr std::optional<mojom::BrowserActionOutcome>
BrowserActionOutcomeFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::BrowserActionOutcome::kCompleted;
    case 1:
      return mojom::BrowserActionOutcome::kRefused;
    case 2:
      return mojom::BrowserActionOutcome::kOutcomeUnknown;
    default:
      return std::nullopt;
  }
}

// Closed browser-ledger result for a policy-minted grant.
constexpr std::optional<mojom::CapabilityRegistrationStatus>
CapabilityRegistrationStatusFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::CapabilityRegistrationStatus::kRegistered;
    case 1:
      return mojom::CapabilityRegistrationStatus::kDuplicate;
    case 2:
      return mojom::CapabilityRegistrationStatus::kStaleGeneration;
    case 3:
      return mojom::CapabilityRegistrationStatus::kLeaseMissing;
    case 4:
      return mojom::CapabilityRegistrationStatus::kInvalidGrant;
    default:
      return std::nullopt;
  }
}

// Closed browser-registry result for one sequenced pending-approval snapshot.
constexpr std::optional<mojom::PendingApprovalRegistrationStatus>
PendingApprovalRegistrationStatusFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::PendingApprovalRegistrationStatus::kRegistered;
    case 1:
      return mojom::PendingApprovalRegistrationStatus::kStaleGeneration;
    case 2:
      return mojom::PendingApprovalRegistrationStatus::kStaleSequence;
    case 3:
      return mojom::PendingApprovalRegistrationStatus::kInvalidBinding;
    case 4:
      return mojom::PendingApprovalRegistrationStatus::kTooManyBindings;
    default:
      return std::nullopt;
  }
}

// The closed consequence class evaluated by Rust policy.
constexpr std::optional<mojom::PolicyActionClass>
PolicyActionClassFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::PolicyActionClass::kObservePage;
    case 1:
      return mojom::PolicyActionClass::kScrollIntoView;
    case 2:
      return mojom::PolicyActionClass::kOpenLink;
    case 3:
      return mojom::PolicyActionClass::kCreateTaskTab;
    case 4:
      return mojom::PolicyActionClass::kSyntheticClick;
    case 5:
      return mojom::PolicyActionClass::kMoveFocus;
    case 6:
      return mojom::PolicyActionClass::kFillField;
    case 7:
      return mojom::PolicyActionClass::kSelectOption;
    case 8:
      return mojom::PolicyActionClass::kToggleControl;
    case 9:
      return mojom::PolicyActionClass::kSubmitForm;
    case 10:
      return mojom::PolicyActionClass::kStartDownload;
    case 11:
      return mojom::PolicyActionClass::kUploadFile;
    case 12:
      return mojom::PolicyActionClass::kSendMessage;
    case 13:
      return mojom::PolicyActionClass::kPurchase;
    case 14:
      return mojom::PolicyActionClass::kExtractCredential;
    case 15:
      return mojom::PolicyActionClass::kBypassAccessControl;
    case 16:
      return mojom::PolicyActionClass::kExecuteToolJob;
    case 17:
      return mojom::PolicyActionClass::kLibraryRead;
    case 18:
      return mojom::PolicyActionClass::kLibraryWrite;
    case 19:
      return mojom::PolicyActionClass::kMemoryRead;
    case 20:
      return mojom::PolicyActionClass::kMemoryWrite;
    case 21:
      return mojom::PolicyActionClass::kControlTab;
    case 22:
      return mojom::PolicyActionClass::kProfileStoreRead;
    default:
      return std::nullopt;
  }
}

// The exact closed operation carried from a typed task proposal through policy
// and execution.
constexpr std::optional<mojom::TaskActionOperationKind>
TaskActionOperationKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::TaskActionOperationKind::kNavigate;
    case 1:
      return mojom::TaskActionOperationKind::kSearch;
    case 2:
      return mojom::TaskActionOperationKind::kHistoryBack;
    case 3:
      return mojom::TaskActionOperationKind::kHistoryForward;
    case 4:
      return mojom::TaskActionOperationKind::kTabsOpen;
    case 5:
      return mojom::TaskActionOperationKind::kTabsList;
    case 6:
      return mojom::TaskActionOperationKind::kTabsActivate;
    case 7:
      return mojom::TaskActionOperationKind::kTabsClose;
    case 8:
      return mojom::TaskActionOperationKind::kDomQuery;
    case 9:
      return mojom::TaskActionOperationKind::kDomRead;
    case 10:
      return mojom::TaskActionOperationKind::kDomClick;
    case 11:
      return mojom::TaskActionOperationKind::kDomScroll;
    case 12:
      return mojom::TaskActionOperationKind::kFormInspect;
    case 13:
      return mojom::TaskActionOperationKind::kFormFill;
    case 14:
      return mojom::TaskActionOperationKind::kFormSubmit;
    case 15:
      return mojom::TaskActionOperationKind::kDownloadStart;
    case 16:
      return mojom::TaskActionOperationKind::kDownloadList;
    case 17:
      return mojom::TaskActionOperationKind::kSelectionRead;
    case 18:
      return mojom::TaskActionOperationKind::kImageDescribe;
    case 19:
      return mojom::TaskActionOperationKind::kImageReadText;
    case 20:
      return mojom::TaskActionOperationKind::kVideoInspect;
    case 21:
      return mojom::TaskActionOperationKind::kPdfInspect;
    case 22:
      return mojom::TaskActionOperationKind::kToolJob;
    case 23:
      return mojom::TaskActionOperationKind::kLinkOpen;
    case 24:
      return mojom::TaskActionOperationKind::kFormSelect;
    case 25:
      return mojom::TaskActionOperationKind::kFormToggle;
    case 26:
      return mojom::TaskActionOperationKind::kLibrarySearch;
    case 27:
      return mojom::TaskActionOperationKind::kLibrarySave;
    case 28:
      return mojom::TaskActionOperationKind::kLibraryRemove;
    case 29:
      return mojom::TaskActionOperationKind::kMemorySearch;
    case 30:
      return mojom::TaskActionOperationKind::kMemorySave;
    case 31:
      return mojom::TaskActionOperationKind::kMemoryUpdate;
    case 32:
      return mojom::TaskActionOperationKind::kMemoryDelete;
    case 33:
      return mojom::TaskActionOperationKind::kDomFocus;
    case 34:
      return mojom::TaskActionOperationKind::kPageScreenshotInspect;
    case 35:
      return mojom::TaskActionOperationKind::kDownloadCancel;
    case 36:
      return mojom::TaskActionOperationKind::kReload;
    case 37:
      return mojom::TaskActionOperationKind::kStopLoading;
    case 38:
      return mojom::TaskActionOperationKind::kHistorySearch;
    case 39:
      return mojom::TaskActionOperationKind::kHistoryRecent;
    case 40:
      return mojom::TaskActionOperationKind::kBookmarksSearch;
    case 41:
      return mojom::TaskActionOperationKind::kBookmarksList;
    case 42:
      return mojom::TaskActionOperationKind::kOpenTabsList;
    default:
      return std::nullopt;
  }
}

// Closed task input shape. The isolated core may name a supplied-value position
// or a boolean state but can never carry the browser-held bytes.
constexpr std::optional<mojom::TaskActionInputKind>
TaskActionInputKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::TaskActionInputKind::kNone;
    case 1:
      return mojom::TaskActionInputKind::kSuppliedValue;
    case 2:
      return mojom::TaskActionInputKind::kToggleState;
    default:
      return std::nullopt;
  }
}

// The closed principal whose authority was narrowed.
constexpr std::optional<mojom::PolicyPrincipalKind>
PolicyPrincipalKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::PolicyPrincipalKind::kAssistant;
    case 1:
      return mojom::PolicyPrincipalKind::kSkill;
    default:
      return std::nullopt;
  }
}

// The closed authority context evaluated by Rust policy.
constexpr std::optional<mojom::PolicyEvaluationContext>
PolicyEvaluationContextFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::PolicyEvaluationContext::kTask;
    case 1:
      return mojom::PolicyEvaluationContext::kDirectUserObservation;
    case 2:
      return mojom::PolicyEvaluationContext::kTaskDiscovery;
    default:
      return std::nullopt;
  }
}

// The closed identity family to which authority is bound.
constexpr std::optional<mojom::AuthoritySubjectKind>
AuthoritySubjectKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::AuthoritySubjectKind::kTask;
    case 1:
      return mojom::AuthoritySubjectKind::kDirectUserIntent;
    default:
      return std::nullopt;
  }
}

// The exact normalized origin form bound into policy scope.
constexpr std::optional<mojom::PolicyOriginKind>
PolicyOriginKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::PolicyOriginKind::kTuple;
    case 1:
      return mojom::PolicyOriginKind::kOpaque;
    default:
      return std::nullopt;
  }
}

// The effective monotonic risk class evaluated by policy.
constexpr std::optional<mojom::PolicyRiskClass>
PolicyRiskClassFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::PolicyRiskClass::kLocalRead;
    case 1:
      return mojom::PolicyRiskClass::kReversibleDisclosure;
    case 2:
      return mojom::PolicyRiskClass::kSensitiveDisclosure;
    case 3:
      return mojom::PolicyRiskClass::kExcludedCommitment;
    case 4:
      return mojom::PolicyRiskClass::kProhibitedAbuse;
    default:
      return std::nullopt;
  }
}

// Closed result of one stateless Rust policy evaluation.
constexpr std::optional<mojom::PolicyEvaluationStatus>
PolicyEvaluationStatusFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::PolicyEvaluationStatus::kGranted;
    case 1:
      return mojom::PolicyEvaluationStatus::kApprovalRequired;
    case 2:
      return mojom::PolicyEvaluationStatus::kDenied;
    case 3:
      return mojom::PolicyEvaluationStatus::kInvalidRequest;
    case 4:
      return mojom::PolicyEvaluationStatus::kCoreUnavailable;
    default:
      return std::nullopt;
  }
}

// The BIP action result code, member for member and in its normative order, so
// a browser refusal and a Rust policy refusal end an action under one closed
// vocabulary. Carried on a DENIED policy evaluation as the code the refused
// action is recorded with.
constexpr std::optional<mojom::TaskActionResultCode>
TaskActionResultCodeFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::TaskActionResultCode::kVerified;
    case 1:
      return mojom::TaskActionResultCode::kDeniedByPolicy;
    case 2:
      return mojom::TaskActionResultCode::kApprovalRequired;
    case 3:
      return mojom::TaskActionResultCode::kApprovalDenied;
    case 4:
      return mojom::TaskActionResultCode::kActorLeaseMissing;
    case 5:
      return mojom::TaskActionResultCode::kCapabilityExpired;
    case 6:
      return mojom::TaskActionResultCode::kTabGone;
    case 7:
      return mojom::TaskActionResultCode::kFrameGone;
    case 8:
      return mojom::TaskActionResultCode::kDocumentInactive;
    case 9:
      return mojom::TaskActionResultCode::kStalePageEpoch;
    case 10:
      return mojom::TaskActionResultCode::kStaleGraph;
    case 11:
      return mojom::TaskActionResultCode::kNodeGone;
    case 12:
      return mojom::TaskActionResultCode::kOriginChanged;
    case 13:
      return mojom::TaskActionResultCode::kRoleOrActionChanged;
    case 14:
      return mojom::TaskActionResultCode::kNotVisible;
    case 15:
      return mojom::TaskActionResultCode::kOccluded;
    case 16:
      return mojom::TaskActionResultCode::kNotEnabled;
    case 17:
      return mojom::TaskActionResultCode::kNotEditable;
    case 18:
      return mojom::TaskActionResultCode::kSensitiveField;
    case 19:
      return mojom::TaskActionResultCode::kDestinationChanged;
    case 20:
      return mojom::TaskActionResultCode::kUnsupported;
    case 21:
      return mojom::TaskActionResultCode::kBudgetExceeded;
    case 22:
      return mojom::TaskActionResultCode::kDispatchFailed;
    case 23:
      return mojom::TaskActionResultCode::kNavigationStarted;
    case 24:
      return mojom::TaskActionResultCode::kPostconditionTimeout;
    case 25:
      return mojom::TaskActionResultCode::kPostconditionFailed;
    case 26:
      return mojom::TaskActionResultCode::kCancelledByUser;
    case 27:
      return mojom::TaskActionResultCode::kCancelledByNavigation;
    case 28:
      return mojom::TaskActionResultCode::kRendererCrashed;
    case 29:
      return mojom::TaskActionResultCode::kOutcomeUnknown;
    case 30:
      return mojom::TaskActionResultCode::kInternalError;
    case 31:
      return mojom::TaskActionResultCode::kEgressNotAuthorized;
    case 32:
      return mojom::TaskActionResultCode::kDestinationClassRestricted;
    case 33:
      return mojom::TaskActionResultCode::kUntrustedContentOrigin;
    case 34:
      return mojom::TaskActionResultCode::kPreparedEffectChanged;
    case 35:
      return mojom::TaskActionResultCode::kCommitWithoutPrepare;
    case 36:
      return mojom::TaskActionResultCode::kGraphMovedDuringPreflight;
    case 37:
      return mojom::TaskActionResultCode::kValueReferenceUnknown;
    default:
      return std::nullopt;
  }
}

// What the browser is asked to do about one asset's bytes.
constexpr std::optional<mojom::AssetDeliveryOperation>
AssetDeliveryOperationFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::AssetDeliveryOperation::kFetchAsset;
    case 1:
      return mojom::AssetDeliveryOperation::kRemoveAsset;
    default:
      return std::nullopt;
  }
}

// The target a device's asset bytes were built for. Closed: an unknown platform
// must refuse rather than resolve to a default.
constexpr std::optional<mojom::AssetPlatform>
AssetPlatformFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::AssetPlatform::kAndroidArm64;
    case 1:
      return mojom::AssetPlatform::kAndroidX64;
    case 2:
      return mojom::AssetPlatform::kMacosArm64;
    case 3:
      return mojom::AssetPlatform::kMacosX64;
    case 4:
      return mojom::AssetPlatform::kWindowsX64;
    case 5:
      return mojom::AssetPlatform::kWindowsArm64;
    case 6:
      return mojom::AssetPlatform::kUnsupported;
    default:
      return std::nullopt;
  }
}

// What an asset is for, and therefore which part of the product consumes it.
// This is also what names an asset to a person: a row carries no name of its
// own.
constexpr std::optional<mojom::AssetKind> AssetKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::AssetKind::kPythonStdlib;
    case 1:
      return mojom::AssetKind::kPythonPackages;
    case 2:
      return mojom::AssetKind::kModelWeights;
    case 3:
      return mojom::AssetKind::kModelTokenizer;
    case 4:
      return mojom::AssetKind::kFilterList;
    case 5:
      return mojom::AssetKind::kCountryFlags;
    case 6:
      return mojom::AssetKind::kStartScenes;
    default:
      return std::nullopt;
  }
}

// What the transferred bytes are, and therefore what installing them means.
constexpr std::optional<mojom::AssetContainer>
AssetContainerFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::AssetContainer::kRaw;
    case 1:
      return mojom::AssetContainer::kZip;
    default:
      return std::nullopt;
  }
}

// How much of an asset is on the device.
constexpr std::optional<mojom::AssetPresence>
AssetPresenceFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::AssetPresence::kAbsent;
    case 1:
      return mojom::AssetPresence::kPartial;
    case 2:
      return mojom::AssetPresence::kComplete;
    case 3:
      return mojom::AssetPresence::kInstalled;
    default:
      return std::nullopt;
  }
}

// Why the plane will not install an asset. Each names a condition a person can
// be shown, not a component that returned nothing.
constexpr std::optional<mojom::AssetRefusalReason>
AssetRefusalReasonFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::AssetRefusalReason::kUnknownAsset;
    case 1:
      return mojom::AssetRefusalReason::kNoVariantForPlatform;
    case 2:
      return mojom::AssetRefusalReason::kNotPublishedYet;
    case 3:
      return mojom::AssetRefusalReason::kCatalogRowIncomplete;
    case 4:
      return mojom::AssetRefusalReason::kVariantTooLarge;
    case 5:
      return mojom::AssetRefusalReason::kAttemptsExhausted;
    case 6:
      return mojom::AssetRefusalReason::kIntegrityFailed;
    case 7:
      return mojom::AssetRefusalReason::kNetworkNotPermitted;
    case 8:
      return mojom::AssetRefusalReason::kDeclinedByPerson;
    default:
      return std::nullopt;
  }
}

// How one transfer ended. The integrity members are separate from the transport
// ones because a truncated transfer and a wrong artifact are different
// accidents with different retry rules.
constexpr std::optional<mojom::AssetTransferOutcome>
AssetTransferOutcomeFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::AssetTransferOutcome::kInterrupted;
    case 1:
      return mojom::AssetTransferOutcome::kOriginRefusedTemporary;
    case 2:
      return mojom::AssetTransferOutcome::kOriginRefusedPermanent;
    case 3:
      return mojom::AssetTransferOutcome::kIntegritySound;
    case 4:
      return mojom::AssetTransferOutcome::kIntegrityWrongLength;
    case 5:
      return mojom::AssetTransferOutcome::kIntegrityWrongDigest;
    case 6:
      return mojom::AssetTransferOutcome::kInstalled;
    case 7:
      return mojom::AssetTransferOutcome::kDeclined;
    default:
      return std::nullopt;
  }
}

// What the device's connection costs right now.
constexpr std::optional<mojom::AssetNetworkCost>
AssetNetworkCostFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::AssetNetworkCost::kOffline;
    case 1:
      return mojom::AssetNetworkCost::kMetered;
    case 2:
      return mojom::AssetNetworkCost::kUnmetered;
    default:
      return std::nullopt;
  }
}

// How a person authenticates to one model provider. Spelled exactly as the
// model catalog's auth_methods column so the contract and the catalog can be
// compared by eye.
constexpr std::optional<mojom::ProviderAuthMethod>
ProviderAuthMethodFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::ProviderAuthMethod::kApiKey;
    case 1:
      return mojom::ProviderAuthMethod::kOauth;
    default:
      return std::nullopt;
  }
}

// One rung of the thinking ladder, ordered from least to most, spelled exactly
// as the model catalog's thinking_levels column and as
// model_router::ThinkingLevel. There is no member for 'let Taffy decide': that
// is the absence of a preference, so the field carrying one is optional rather
// than holding a rung that means no rung.
constexpr std::optional<mojom::ThinkingLevel>
ThinkingLevelFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::ThinkingLevel::kOff;
    case 1:
      return mojom::ThinkingLevel::kMinimal;
    case 2:
      return mojom::ThinkingLevel::kLow;
    case 3:
      return mojom::ThinkingLevel::kMedium;
    case 4:
      return mojom::ThinkingLevel::kHigh;
    case 5:
      return mojom::ThinkingLevel::kXhigh;
    case 6:
      return mojom::ThinkingLevel::kMax;
    default:
      return std::nullopt;
  }
}

// The registry state the browser layer reports for one stored credential. There
// is no ABSENT member on purpose: absence is a deletion, expressed by
// FORGET_PROVIDER_CREDENTIAL, and a state report about nothing is refused.
constexpr std::optional<mojom::ProviderCredentialState>
ProviderCredentialStateFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::ProviderCredentialState::kUsable;
    case 1:
      return mojom::ProviderCredentialState::kNeedsSignIn;
    case 2:
      return mojom::ProviderCredentialState::kRefreshFailed;
    default:
      return std::nullopt;
  }
}

// The closed request family a model effect names. Every member but MANAGED is a
// provider family, spelled exactly as the model catalog's wire_api column;
// MANAGED is the product's own edge envelope and is never a catalog value.
constexpr std::optional<mojom::ProviderWireApi>
ProviderWireApiFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::ProviderWireApi::kAnthropicMessages;
    case 1:
      return mojom::ProviderWireApi::kOpenAiResponses;
    case 2:
      return mojom::ProviderWireApi::kOpenAiCompletions;
    case 3:
      return mojom::ProviderWireApi::kGoogleGenerativeLanguage;
    case 4:
      return mojom::ProviderWireApi::kManaged;
    case 5:
      return mojom::ProviderWireApi::kOpenAiCodexResponses;
    case 6:
      return mojom::ProviderWireApi::kGoogleCloudCodeAssist;
    default:
      return std::nullopt;
  }
}

// A definitive provider answer classified by the browser transport and consumed
// only by the Rust router's retry table. It is absent when dispatch never
// started or the outcome is unknown.
constexpr std::optional<mojom::ModelErrorClass>
ModelErrorClassFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::ModelErrorClass::kAuth;
    case 1:
      return mojom::ModelErrorClass::kQuota;
    case 2:
      return mojom::ModelErrorClass::kOverloaded;
    case 3:
      return mojom::ModelErrorClass::kInvalidRequest;
    case 4:
      return mojom::ModelErrorClass::kNetwork;
    case 5:
      return mojom::ModelErrorClass::kOverflow;
    case 6:
      return mojom::ModelErrorClass::kCanceled;
    case 7:
      return mojom::ModelErrorClass::kUnknown;
    default:
      return std::nullopt;
  }
}

// Which authority named the address a model request is sent to. The browser
// revalidates against a different rule for each, so the request has to say
// which one it is claiming rather than leave the browser to infer it from the
// address's shape.
constexpr std::optional<mojom::ModelEndpointKind>
ModelEndpointKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::ModelEndpointKind::kCatalogOrigin;
    case 1:
      return mojom::ModelEndpointKind::kUserBaseUrl;
    default:
      return std::nullopt;
  }
}

// What a person's own endpoint turned out to be, read from the server's own
// answer. Held equal to core_api::ServerKindView member for member and wire for
// wire.
constexpr std::optional<mojom::ServerKind> ServerKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::ServerKind::kOpenaiCompatible;
    case 1:
      return mojom::ServerKind::kOllama;
    case 2:
      return mojom::ServerKind::kLmStudio;
    case 3:
      return mojom::ServerKind::kVllm;
    case 4:
      return mojom::ServerKind::kLlamaCpp;
    default:
      return std::nullopt;
  }
}

// Closed acknowledgement for one backpressured raw model transport chunk.
constexpr std::optional<mojom::ModelStreamChunkStatus>
ModelStreamChunkStatusFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::ModelStreamChunkStatus::kAccepted;
    case 1:
      return mojom::ModelStreamChunkStatus::kInvalid;
    case 2:
      return mojom::ModelStreamChunkStatus::kStale;
    case 3:
      return mojom::ModelStreamChunkStatus::kUnavailable;
    default:
      return std::nullopt;
  }
}

// The complete closed set of typed records allowed into an explicit encrypted
// backup. Account sessions, credentials, cookies, live authority, caches,
// derived indexes, and downloaded artifacts have no member and cannot cross
// this seam.
constexpr std::optional<mojom::BackupRecordKind>
BackupRecordKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::BackupRecordKind::kAssistantConfiguration;
    case 1:
      return mojom::BackupRecordKind::kSavedWorkspace;
    case 2:
      return mojom::BackupRecordKind::kLibraryEntry;
    case 3:
      return mojom::BackupRecordKind::kMemoryRecord;
    case 4:
      return mojom::BackupRecordKind::kUserAuthoredSkill;
    case 5:
      return mojom::BackupRecordKind::kLearnedProcedure;
    case 6:
      return mojom::BackupRecordKind::kBookmark;
    case 7:
      return mojom::BackupRecordKind::kBrowserPreference;
    default:
      return std::nullopt;
  }
}

// Whether one backup descriptor contributes payload bytes or an explicit
// deletion marker.
constexpr std::optional<mojom::BackupRecordState>
BackupRecordStateFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::BackupRecordState::kActive;
    case 1:
      return mojom::BackupRecordState::kTombstone;
    default:
      return std::nullopt;
  }
}

// A closed terminal answer from the sandboxed backup manifest and restore
// planner.
constexpr std::optional<mojom::BackupPlanningStatus>
BackupPlanningStatusFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::BackupPlanningStatus::kSucceeded;
    case 1:
      return mojom::BackupPlanningStatus::kInvalidRequest;
    case 2:
      return mojom::BackupPlanningStatus::kInvalidManifest;
    case 3:
      return mojom::BackupPlanningStatus::kSnapshotMismatch;
    case 4:
      return mojom::BackupPlanningStatus::kStagedPayloadMismatch;
    case 5:
      return mojom::BackupPlanningStatus::kRestoreConflict;
    case 6:
      return mojom::BackupPlanningStatus::kDigestUnavailable;
    case 7:
      return mojom::BackupPlanningStatus::kUnavailable;
    default:
      return std::nullopt;
  }
}

// The closed profile destination class for an explicit restore.
constexpr std::optional<mojom::BackupRestoreTargetKind>
BackupRestoreTargetKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::BackupRestoreTargetKind::kNewRegularProfile;
    case 1:
      return mojom::BackupRestoreTargetKind::kExistingRegularProfile;
    default:
      return std::nullopt;
  }
}

// The deterministic action for one authenticated manifest record against the
// selected target snapshot.
constexpr std::optional<mojom::BackupRestoreAction>
BackupRestoreActionFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::BackupRestoreAction::kStageCreate;
    case 1:
      return mojom::BackupRestoreAction::kStageDeletion;
    case 2:
      return mojom::BackupRestoreAction::kAlreadyPresent;
    case 3:
      return mojom::BackupRestoreAction::kKeepNewerCurrent;
    case 4:
      return mojom::BackupRestoreAction::kBlockedByDeletion;
    case 5:
      return mojom::BackupRestoreAction::kNeedsExplicitConflictChoice;
    default:
      return std::nullopt;
  }
}

// The closed result of advancing one retained restore protocol; only SUCCEEDED
// permits the corresponding next action.
constexpr std::optional<mojom::BackupRestoreProtocolStatus>
BackupRestoreProtocolStatusFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::BackupRestoreProtocolStatus::kSucceeded;
    case 1:
      return mojom::BackupRestoreProtocolStatus::kInvalidOperation;
    case 2:
      return mojom::BackupRestoreProtocolStatus::kUnavailable;
    case 3:
      return mojom::BackupRestoreProtocolStatus::kBindingMismatch;
    case 4:
      return mojom::BackupRestoreProtocolStatus::kWrongPhase;
    case 5:
      return mojom::BackupRestoreProtocolStatus::kConfirmationMismatch;
    case 6:
      return mojom::BackupRestoreProtocolStatus::kSnapshotMismatch;
    case 7:
      return mojom::BackupRestoreProtocolStatus::kReconcileRequired;
    default:
      return std::nullopt;
  }
}

// The browser's closed durable observation after one consumptive commit
// authorization was issued.
constexpr std::optional<mojom::BackupRestoreCommitOutcome>
BackupRestoreCommitOutcomeFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::BackupRestoreCommitOutcome::kCommitted;
    case 1:
      return mojom::BackupRestoreCommitOutcome::kDefinitelyNotCommitted;
    case 2:
      return mojom::BackupRestoreCommitOutcome::kOutcomeUnknown;
    default:
      return std::nullopt;
  }
}

// The person's closed decision for one committed hidden candidate.
constexpr std::optional<mojom::BackupRestoreResolutionChoice>
BackupRestoreResolutionChoiceFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::BackupRestoreResolutionChoice::kAcceptCandidate;
    case 1:
      return mojom::BackupRestoreResolutionChoice::kDiscardCandidate;
    default:
      return std::nullopt;
  }
}

// The browser's closed durable observation after candidate publication or
// deletion was authorized.
constexpr std::optional<mojom::BackupRestoreResolutionOutcome>
BackupRestoreResolutionOutcomeFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::BackupRestoreResolutionOutcome::kCompleted;
    case 1:
      return mojom::BackupRestoreResolutionOutcome::kDefinitelyNotCompleted;
    case 2:
      return mojom::BackupRestoreResolutionOutcome::kOutcomeUnknown;
    default:
      return std::nullopt;
  }
}

// The closed append-only physical fact carried by one content-free recovery
// record.
constexpr std::optional<mojom::BackupRestoreRecoveryFactKind>
BackupRestoreRecoveryFactKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::BackupRestoreRecoveryFactKind::kIntentRecorded;
    case 1:
      return mojom::BackupRestoreRecoveryFactKind::kOutcomeObserved;
    default:
      return std::nullopt;
  }
}

// The closed physical action named by a recovery intent; an intent never
// authorizes replay.
constexpr std::optional<mojom::BackupRestorePhysicalIntent>
BackupRestorePhysicalIntentFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::BackupRestorePhysicalIntent::kCommitCandidate;
    case 1:
      return mojom::BackupRestorePhysicalIntent::kAcceptCandidate;
    case 2:
      return mojom::BackupRestorePhysicalIntent::kDiscardCandidate;
    default:
      return std::nullopt;
  }
}

// A durable physical observation; unknown never means retry or permission.
constexpr std::optional<mojom::BackupRestoreObservedOutcome>
BackupRestoreObservedOutcomeFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::BackupRestoreObservedOutcome::kCompleted;
    case 1:
      return mojom::BackupRestoreObservedOutcome::kDefinitelyNotCompleted;
    case 2:
      return mojom::BackupRestoreObservedOutcome::kOutcomeUnknown;
    default:
      return std::nullopt;
  }
}

// A read-only reduction of durable recovery facts; no member grants physical
// authority.
constexpr std::optional<mojom::BackupRestoreRecoveryClassificationKind>
BackupRestoreRecoveryClassificationKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::BackupRestoreRecoveryClassificationKind::kReconcileRequired;
    case 1:
      return mojom::BackupRestoreRecoveryClassificationKind::kRollbackAvailable;
    case 2:
      return mojom::BackupRestoreRecoveryClassificationKind::kCleanupRequired;
    case 3:
      return mojom::BackupRestoreRecoveryClassificationKind::kPublished;
    case 4:
      return mojom::BackupRestoreRecoveryClassificationKind::kVerifiedDeleted;
    default:
      return std::nullopt;
  }
}

// Why a content-free recovery history cannot be trusted as one restore
// aggregate.
constexpr std::optional<mojom::BackupRestoreRecoveryError>
BackupRestoreRecoveryErrorFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::BackupRestoreRecoveryError::kMissingCommitIntent;
    case 1:
      return mojom::BackupRestoreRecoveryError::kTooManyRecords;
    case 2:
      return mojom::BackupRestoreRecoveryError::kUnsupportedVersion;
    case 3:
      return mojom::BackupRestoreRecoveryError::kInvalidSequence;
    case 4:
      return mojom::BackupRestoreRecoveryError::kInvalidBinding;
    case 5:
      return mojom::BackupRestoreRecoveryError::kBindingChanged;
    case 6:
      return mojom::BackupRestoreRecoveryError::kInvalidIntentId;
    case 7:
      return mojom::BackupRestoreRecoveryError::kIntentIdReused;
    case 8:
      return mojom::BackupRestoreRecoveryError::kUnexpectedIntent;
    case 9:
      return mojom::BackupRestoreRecoveryError::kUnresolvedIntent;
    case 10:
      return mojom::BackupRestoreRecoveryError::kOutcomeWithoutIntent;
    case 11:
      return mojom::BackupRestoreRecoveryError::kOutcomeIntentMismatch;
    case 12:
      return mojom::BackupRestoreRecoveryError::kDuplicateUnknownOutcome;
    case 13:
      return mojom::BackupRestoreRecoveryError::kTerminalHistoryExtended;
    default:
      return std::nullopt;
  }
}

// Closed result of read-only recovery inspection; no member carries physical
// authority.
constexpr std::optional<mojom::BackupRestoreRecoveryInspectionStatus>
BackupRestoreRecoveryInspectionStatusFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::BackupRestoreRecoveryInspectionStatus::kSucceeded;
    case 1:
      return mojom::BackupRestoreRecoveryInspectionStatus::kInvalidOperation;
    case 2:
      return mojom::BackupRestoreRecoveryInspectionStatus::kInvalidRecord;
    case 3:
      return mojom::BackupRestoreRecoveryInspectionStatus::kInvalidHistory;
    case 4:
      return mojom::BackupRestoreRecoveryInspectionStatus::kUnavailable;
    default:
      return std::nullopt;
  }
}

// Closed availability of a transient saved-flow review.
constexpr std::optional<mojom::SavedFlowQueryStatus>
SavedFlowQueryStatusFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::SavedFlowQueryStatus::kAvailable;
    case 1:
      return mojom::SavedFlowQueryStatus::kPrivateProfile;
    case 2:
      return mojom::SavedFlowQueryStatus::kUnavailable;
    case 3:
      return mojom::SavedFlowQueryStatus::kInvalidRequest;
    case 4:
      return mojom::SavedFlowQueryStatus::kStaleRequest;
    default:
      return std::nullopt;
  }
}

// The read-only question, never task authority.
constexpr std::optional<mojom::SavedFlowQueryKind>
SavedFlowQueryKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::SavedFlowQueryKind::kExactGoal;
    case 1:
      return mojom::SavedFlowQueryKind::kReview;
    case 2:
      return mojom::SavedFlowQueryKind::kPublicStart;
    default:
      return std::nullopt;
  }
}

// What became of one request for values, stated as the next move it implies
// rather than as the browser clause it came from (decision 0215). The browser's
// fourteen abandonment clauses and its capture's eleven refusal clauses keep
// their own granularity in the device log and fold into these seven on the way
// out, because a clause is a fact about the browser's internals and a member
// here is an instruction. Closed and failing closed: a value this build does
// not recognise is refused, and the wire carries an integer, so nothing derived
// from a page can cross a boundary whose whole property is that it carries a
// count (decision 0088).
constexpr std::optional<mojom::FieldValueAskOutcome>
FieldValueAskOutcomeFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::FieldValueAskOutcome::kAnswered;
    case 1:
      return mojom::FieldValueAskOutcome::kDismissed;
    case 2:
      return mojom::FieldValueAskOutcome::kNotAField;
    case 3:
      return mojom::FieldValueAskOutcome::kChallengeOffScreen;
    case 4:
      return mojom::FieldValueAskOutcome::kCannotBeShown;
    case 5:
      return mojom::FieldValueAskOutcome::kPageMoved;
    case 6:
      return mojom::FieldValueAskOutcome::kNoSurface;
    default:
      return std::nullopt;
  }
}

}  // namespace taffy::core_service::wire

#endif  // TAFFY_CONTRACTS_CORE_SERVICE_GENERATED_CPP_CORE_SERVICE_ENUMS_H_
