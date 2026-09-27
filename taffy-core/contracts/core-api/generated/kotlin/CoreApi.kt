// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Generated from schema/contract.json. Do not edit.
// Contract core_api 3.49.

package taffy.core_api

const val MAX_SAVED_FLOW_QUERY_RESULTS: Int = 4
const val MAX_COMMAND_PAYLOAD_BYTES: Int = 65536
const val MAX_EVENT_PAYLOAD_BYTES: Int = 262144
const val MAX_ACTIVE_TASKS: Int = 64
const val MAX_PROGRESS_BASIS_POINTS: Int = 10000
const val MAX_MESSAGE_KEY_BYTES: Int = 256
const val MAX_AUTH_EMAIL_BYTES: Int = 320
const val MAX_AUTH_DISPLAY_NAME_BYTES: Int = 512
const val MAX_AUTH_CREDENTIAL_HANDLE_BYTES: Int = 256
const val MAX_AUTH_METHODS: Int = 4
const val MAX_TASK_GOAL_BYTES: Int = 8192
const val MAX_TASK_CONTROLS: Int = 4
const val MAX_TASK_ARTIFACTS: Int = 16
const val MAX_TASK_ACTIVITY: Int = 32
const val MAX_TASK_ARTIFACT_EXPORT_BYTES: Int = 16777216
const val MAX_IDENTIFIER_BYTES: Int = 256
const val MAX_WORKSPACES: Int = 32
const val MAX_WORKSPACE_SOURCES: Int = 64
const val MAX_WORKSPACE_FACTS: Int = 256
const val MAX_WORKSPACE_DISPLAY_NAME_BYTES: Int = 256
const val MAX_WORKSPACE_CONFIRMATION_TOKEN_BYTES: Int = 64
const val MAX_FACT_SOURCES: Int = 16
const val MAX_WORKSPACE_TITLE_BYTES: Int = 1024
const val MAX_SOURCE_HOST_BYTES: Int = 253
const val MAX_TASK_CONSENT_SOURCES: Int = 64
const val MAX_NEW_SOURCE_CAP: Int = 64
const val MAX_FACT_FIELD_BYTES: Int = 256
const val MAX_FACT_VALUE_BYTES: Int = 16384
const val MAX_EXPORT_CONTENT_BYTES: Int = 131072
const val MAX_LIBRARY_ENTRIES: Int = 1024
const val MAX_LIBRARY_SOURCES: Int = 16
const val MAX_LIBRARY_QUERY_BYTES: Int = 512
const val MAX_LIBRARY_SEARCH_RESULTS: Int = 32
const val MAX_LIBRARY_REFRESH_SOURCES: Int = 64
const val MAX_LIBRARY_REFRESH_RESULTS: Int = 64
const val MAX_LIBRARY_REFRESH_PREVIEW_ID_BYTES: Int = 64
const val MAX_MEMORY_RECORDS: Int = 512
const val MAX_MEMORY_STATEMENT_BYTES: Int = 2048
const val MAX_MEMORY_QUERY_BYTES: Int = 512
const val MAX_MEMORY_SEARCH_RESULTS: Int = 32
const val MAX_PAGE_INSPECTOR_PAYLOAD_BYTES: Int = 131072
const val MAX_PAGE_INSPECTOR_DOCUMENTS: Int = 1
const val MAX_PAGE_INSPECTOR_CLAIMS: Int = 4
const val MAX_PAGE_INSPECTOR_NODES: Int = 128
const val MAX_PAGE_INSPECTOR_EDGES: Int = 256
const val MAX_PAGE_INSPECTOR_ADAPTERS: Int = 8
const val MAX_PAGE_INSPECTOR_FRAMES: Int = 1
const val MAX_PAGE_INSPECTOR_WARNINGS: Int = 32
const val MAX_PAGE_INSPECTOR_BUDGETS: Int = 7
const val MAX_PAGE_INSPECTOR_IDENTIFIER_BYTES: Int = 64
const val MAX_PAGE_INSPECTOR_HOST_BYTES: Int = 253
const val MAX_PAGE_INSPECTOR_NAME_BYTES: Int = 512
const val MAX_PAGE_SNAPSHOT_EXPORT_BYTES: Int = 262144
const val MAX_ASSETS: Int = 64
const val MAX_PART_MEMBER_PATH_BYTES: Int = 256
const val MAX_PART_MEMBER_BYTES: Int = 4194304
const val MAX_PROVIDER_ID_BYTES: Int = 64
const val MAX_PROVIDER_DISPLAY_NAME_BYTES: Int = 128
const val MAX_PROVIDER_ENDPOINT_BYTES: Int = 512
const val MAX_CUSTOM_PROVIDERS: Int = 32
const val MAX_PROVIDER_ROSTER_ENTRIES: Int = 96
const val MAX_USER_INPUT_ANSWER_BYTES: Int = 512
const val MAX_PROVIDER_MODEL_ENTRIES: Int = 256
const val MAX_MODEL_ID_BYTES: Int = 128
const val MAX_MODEL_DISPLAY_NAME_BYTES: Int = 128
const val MAX_MODEL_ROLES: Int = 4
const val MAX_MODEL_INPUT_MODALITIES: Int = 2
const val MAX_MODEL_THINKING_LEVELS: Int = 7
const val MAX_PROVIDER_PRESENTATION_BYTES: Int = 512
const val MAX_CUSTOM_MODEL_ENTRIES: Int = 32
const val MAX_COMPOSER_PREFIX_BYTES: Int = 4096
const val MAX_COMPOSER_SUFFIX_BYTES: Int = 1024
const val MAX_COMPOSER_COMPLETION_BYTES: Int = 512
const val MAX_TASK_ANSWER_DELTA_BYTES: Int = 16384
const val MAX_TASK_ANSWER_RESIDENCY_BYTES: Int = 131072
const val MAX_ASSISTANT_ABILITIES: Int = 16
const val MAX_BUILTIN_SKILLS: Int = 16
const val MAX_CORE_STATUS_PROJECTION_OMISSIONS: Int = 13
const val MAX_SITE_SKILLS: Int = 64
const val MAX_SKILL_ID_BYTES: Int = 128
const val MAX_SKILL_ORIGIN_BYTES: Int = 2048
const val MAX_SKILL_MATCH_CLAUSES: Int = 8
const val MAX_SKILL_STEPS: Int = 32
const val MAX_SKILL_ARGUMENTS_PER_STEP: Int = 16
const val MAX_SKILL_TOOL_NAME_BYTES: Int = 128
const val MAX_PERSONALITY_SCALE: Int = 2
const val MAX_SAVED_SIGN_INS: Int = 256
const val MAX_SAVED_DETAILS: Int = 64
const val MAX_SAVED_SIGN_IN_SITE_BYTES: Int = 253
const val MAX_SAVED_SIGN_IN_USERNAME_BYTES: Int = 320
const val MAX_SAVED_DETAIL_NAME_BYTES: Int = 256
const val MAX_SAVED_DETAIL_EMAIL_BYTES: Int = 320
const val MAX_SAVED_DETAIL_PHONE_BYTES: Int = 128
const val MAX_SAVED_DETAIL_ADDRESS_BYTES: Int = 2048
const val MAX_SAVED_DETAIL_POSTCODE_BYTES: Int = 64
const val MAX_SAVED_DETAIL_COUNTRY_BYTES: Int = 128

enum class CoreCommandKind(val wire: UInt) {
    START_TASK(0u),
    CANCEL_TASK(1u),
    APPROVE_ACTION(2u),
    RETRY_CORE(3u),
    PERMISSION_RESULT(4u),
    START_AUTH(5u),
    REQUEST_EMAIL_LINK(6u),
    SIGN_OUT(7u),
    AUTH_CREDENTIAL_RESULT(8u),
    CORRECT_WORKSPACE_FACT(9u),
    EXCLUDE_WORKSPACE_SOURCE(10u),
    REQUEST_WORKSPACE_EXPORT(11u),
    REQUEST_ASSET(12u),
    REMOVE_ASSET(13u),
    SET_ASSET_POLICY(14u),
    SAVE_PROVIDER_CREDENTIAL(15u),
    FORGET_PROVIDER_CREDENTIAL(16u),
    START_PROVIDER_AUTH(17u),
    SAVE_CUSTOM_PROVIDER(18u),
    REMOVE_CUSTOM_PROVIDER(19u),
    COMPLETE_HANDOVER(20u),
    SUPPLY_USER_INPUT(21u),
    SET_PROVIDER_CREDENTIAL_STATE(22u),
    PROBE_PROVIDER_KEY(23u),
    SET_PROVIDER_MODEL_PREFERENCE(24u),
    PROBE_CUSTOM_ENDPOINT(25u),
    REQUEST_COMPOSER_COMPLETION(26u),
    CANCEL_COMPOSER_COMPLETION(27u),
    PAUSE_TASK(28u),
    RESUME_TASK(29u),
    TAKE_OVER(30u),
    SET_ASSISTANT_CONFIGURATION(31u),
    SAVE_WORKSPACE(32u),
    RENAME_WORKSPACE(33u),
    DELETE_WORKSPACE(34u),
    DISCARD_WORKSPACE(35u),
    SEARCH_LIBRARY(36u),
    SAVE_LIBRARY_FACT(37u),
    REMOVE_LIBRARY_ENTRY(38u),
    REQUEST_LIBRARY_EXPORT(39u),
    SEARCH_MEMORY(40u),
    UPSERT_MEMORY(41u),
    DELETE_MEMORY(42u),
    ACCEPT_TASK_ARTIFACT(43u),
    REQUEST_TASK_ARTIFACT_EXPORT(44u),
    MUTATE_SITE_SKILL(45u),
    CANCEL_PROVIDER_AUTH(46u),
    START_LIBRARY_REFRESH(47u),
    FOLLOW_UP(48u);

    companion object {
        fun fromWire(value: UInt): CoreCommandKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class AssistantAbilityView(val wire: UInt) {
    PAGES_LOOKUP(0u),
    PAGES_COMPARE(1u),
    PAGES_SUMMARIZE(2u),
    PAGES_TABLE(3u),
    PRODUCTS(4u),
    OFFERS(5u),
    FORM(6u),
    DOWNLOADS(7u),
    PDF(8u),
    SHEET(9u),
    DOCUMENT(10u),
    DEPTH(11u),
    TRIP(12u),
    PICTURES(13u),
    VIDEO(14u),
    KEEP(15u);

    companion object {
        fun fromWire(value: UInt): AssistantAbilityView? = entries.firstOrNull { it.wire == value }
    }
}

enum class BuiltinSkillIdView(val wire: UInt) {
    GENERAL_WEB_RESEARCH(0u),
    DEEP_RESEARCH(1u),
    PRODUCT_COMPARISON(2u),
    MULTI_TAB_COMPARISON(3u),
    WEBSITE_SUMMARIZER(4u),
    PDF_ANALYSIS(5u),
    DATA_EXTRACTION(6u),
    FORM_ASSISTANT(7u),
    SHOPPING(8u),
    DOWNLOAD_ORGANIZER(9u),
    TRAVEL_RESEARCH(10u),
    VIDEO_TRANSCRIPT_ANALYZER(11u),
    IMAGE_UNDERSTANDING(12u),
    LIBRARY_BUILDER(13u),
    SPREADSHEET_BUILDER(14u),
    DOCUMENT_GENERATOR(15u);

    companion object {
        fun fromWire(value: UInt): BuiltinSkillIdView? = entries.firstOrNull { it.wire == value }
    }
}

enum class BuiltinSkillAvailabilityView(val wire: UInt) {
    AVAILABLE(0u),
    REQUIRED_TOOL_UNAVAILABLE(1u),
    REQUIRED_PART_MISSING(2u),
    PROFILE_UNAVAILABLE(3u),
    POLICY_UNAVAILABLE(4u);

    companion object {
        fun fromWire(value: UInt): BuiltinSkillAvailabilityView? = entries.firstOrNull { it.wire == value }
    }
}

enum class CoreStatusProjectionMode(val wire: UInt) {
    COMPLETE(0u),
    RECOVERY_REQUIRED(1u);

    companion object {
        fun fromWire(value: UInt): CoreStatusProjectionMode? = entries.firstOrNull { it.wire == value }
    }
}

enum class CoreStatusProjectionFamily(val wire: UInt) {
    ACTIVE_TASKS(0u),
    WORKSPACES(1u),
    WORKSPACE_EXPORT(2u),
    ASSET_DELIVERY(3u),
    PROVIDER_ROSTER(4u),
    PROVIDER_PROBES(5u),
    PROVIDER_MODELS(6u),
    LIBRARY(7u),
    LIBRARY_EXPORT(8u),
    MEMORY(9u),
    SAVED_SIGN_INS(10u),
    SAVED_DETAILS(11u),
    SITE_SKILLS(12u);

    companion object {
        fun fromWire(value: UInt): CoreStatusProjectionFamily? = entries.firstOrNull { it.wire == value }
    }
}

enum class SiteSkillMutationKind(val wire: UInt) {
    TEACH(0u),
    UPDATE(1u),
    SET_ENABLED(2u),
    REMOVE(3u);

    companion object {
        fun fromWire(value: UInt): SiteSkillMutationKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class SiteSkillClauseKind(val wire: UInt) {
    ROLE_PRESENT(0u),
    PHRASE_AT(1u),
    STATE_AT(2u);

    companion object {
        fun fromWire(value: UInt): SiteSkillClauseKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class SiteSkillArgumentKind(val wire: UInt) {
    FROM_EARLIER_STEP(0u),
    FROM_PERSON(1u),
    CHOICE(2u),
    COUNT(3u),
    FLAG(4u),
    PUBLIC_ADDRESS(5u),
    SEMANTIC_TARGET(6u);

    companion object {
        fun fromWire(value: UInt): SiteSkillArgumentKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class SiteSkillProvenanceView(val wire: UInt) {
    AUTHORED(0u),
    RECORDED_FROM_TASK(1u);

    companion object {
        fun fromWire(value: UInt): SiteSkillProvenanceView? = entries.firstOrNull { it.wire == value }
    }
}

enum class SiteSkillStatusView(val wire: UInt) {
    DRAFT(0u),
    ACTIVE(1u),
    SUPERSEDED(2u),
    RETIRED(3u),
    DISABLED(4u);

    companion object {
        fun fromWire(value: UInt): SiteSkillStatusView? = entries.firstOrNull { it.wire == value }
    }
}

enum class PersonalityPresetView(val wire: UInt) {
    CAREFUL_RESEARCHER(0u),
    QUICK_SHOPPER(1u),
    TRIP_PLANNER(2u);

    companion object {
        fun fromWire(value: UInt): PersonalityPresetView? = entries.firstOrNull { it.wire == value }
    }
}

enum class TaskTemplateId(val wire: UInt) {
    COMPARE_PRODUCTS(0u),
    SUMMARIZE_EVIDENCE(1u),
    BUILD_SOURCE_TABLE(2u),
    WEB_ERRAND(3u);

    companion object {
        fun fromWire(value: UInt): TaskTemplateId? = entries.firstOrNull { it.wire == value }
    }
}

enum class TaskProviderRoute(val wire: UInt) {
    NOT_CONFIGURED(0u),
    DIRECT_USER_KEY(1u),
    MANAGED_SERVICE(2u),
    NO_MODEL_REQUIRED(3u);

    companion object {
        fun fromWire(value: UInt): TaskProviderRoute? = entries.firstOrNull { it.wire == value }
    }
}

enum class TaskAttachedStore(val wire: UInt) {
    HISTORY(0u),
    BOOKMARKS(1u),
    OPEN_TABS(2u);

    companion object {
        fun fromWire(value: UInt): TaskAttachedStore? = entries.firstOrNull { it.wire == value }
    }
}

enum class CoreAvailability(val wire: UInt) {
    STARTING(0u),
    READY(1u),
    UNAVAILABLE(2u),
    CIRCUIT_OPEN(3u);

    companion object {
        fun fromWire(value: UInt): CoreAvailability? = entries.firstOrNull { it.wire == value }
    }
}

enum class SavedDataAvailability(val wire: UInt) {
    LOADING(0u),
    READY(1u),
    UNAVAILABLE(2u);

    companion object {
        fun fromWire(value: UInt): SavedDataAvailability? = entries.firstOrNull { it.wire == value }
    }
}

enum class TaskPhase(val wire: UInt) {
    IDLE(0u),
    PLANNING(1u),
    WAITING_FOR_USER(2u),
    RUNNING(3u),
    COMPLETED(4u),
    FAILED(5u),
    CANCELLED(6u),
    OUTCOME_UNKNOWN(7u),
    PAUSED(8u),
    PARTIAL(9u);

    companion object {
        fun fromWire(value: UInt): TaskPhase? = entries.firstOrNull { it.wire == value }
    }
}

enum class TaskControlKind(val wire: UInt) {
    PAUSE(0u),
    RESUME(1u),
    TAKE_OVER(2u),
    STOP(3u);

    companion object {
        fun fromWire(value: UInt): TaskControlKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class CoreFailureCode(val wire: UInt) {
    CANCELLED(0u),
    DEADLINE_EXCEEDED(1u),
    CORE_UNAVAILABLE(2u),
    POLICY_DENIED(3u),
    INVALID_REQUEST(4u),
    BACKPRESSURE(5u),
    OUTCOME_UNKNOWN(6u),
    INTERNAL(7u),
    BUDGET_EXCEEDED(8u),
    PROVIDER_UNAVAILABLE(9u),
    SOURCES_UNAVAILABLE(10u),
    JOURNAL_UNUSABLE(11u),
    UNVERIFIABLE_ACTION(12u),
    PROVIDER_REFUSED(13u),
    PROVIDER_LIMIT(14u),
    OFFLINE(15u),
    POLICY_REFUSED(16u);

    companion object {
        fun fromWire(value: UInt): CoreFailureCode? = entries.firstOrNull { it.wire == value }
    }
}

enum class PlatformPermission(val wire: UInt) {
    NOTIFICATIONS(0u),
    CAMERA(1u),
    MICROPHONE(2u),
    LOCATION(3u),
    READ_USER_FILE(4u),
    WRITE_USER_FILE(5u);

    companion object {
        fun fromWire(value: UInt): PlatformPermission? = entries.firstOrNull { it.wire == value }
    }
}

enum class PermissionDecision(val wire: UInt) {
    GRANTED(0u),
    DENIED(1u),
    UNAVAILABLE(2u);

    companion object {
        fun fromWire(value: UInt): PermissionDecision? = entries.firstOrNull { it.wire == value }
    }
}

enum class AuthProvider(val wire: UInt) {
    GOOGLE(0u),
    EMAIL_LINK(1u),
    GITHUB(2u),
    FACEBOOK(3u);

    companion object {
        fun fromWire(value: UInt): AuthProvider? = entries.firstOrNull { it.wire == value }
    }
}

enum class AuthMethodAvailability(val wire: UInt) {
    AVAILABLE(0u),
    NOT_CONFIGURED(1u),
    PLATFORM_UNAVAILABLE(2u);

    companion object {
        fun fromWire(value: UInt): AuthMethodAvailability? = entries.firstOrNull { it.wire == value }
    }
}

enum class AuthPhase(val wire: UInt) {
    INITIALIZING(0u),
    SIGNED_OUT(1u),
    IN_FLIGHT(2u),
    LINK_SENT(3u),
    SIGNED_IN(4u),
    FAILED(5u);

    companion object {
        fun fromWire(value: UInt): AuthPhase? = entries.firstOrNull { it.wire == value }
    }
}

enum class AuthFailureCode(val wire: UInt) {
    NOT_CONFIGURED(0u),
    CANCELLED(1u),
    NO_CREDENTIAL(2u),
    NETWORK(3u),
    REJECTED(4u),
    INVALID_REDIRECT(5u),
    CORE_UNAVAILABLE(6u),
    UNKNOWN(7u);

    companion object {
        fun fromWire(value: UInt): AuthFailureCode? = entries.firstOrNull { it.wire == value }
    }
}

enum class AuthCredentialStatus(val wire: UInt) {
    SUCCESS(0u),
    CANCELLED(1u),
    NO_CREDENTIAL(2u),
    UNAVAILABLE(3u);

    companion object {
        fun fromWire(value: UInt): AuthCredentialStatus? = entries.firstOrNull { it.wire == value }
    }
}

enum class WorkspacePhase(val wire: UInt) {
    RUNNING(0u),
    WAITING_FOR_USER(1u),
    PAUSED(2u),
    DONE(3u),
    PARTLY_DONE(4u),
    STOPPED(5u),
    FAILED(6u);

    companion object {
        fun fromWire(value: UInt): WorkspacePhase? = entries.firstOrNull { it.wire == value }
    }
}

enum class WorkspaceFactKind(val wire: UInt) {
    FROM_PAGE(0u),
    SUMMARIZED(1u),
    TAFFY_INFERENCE(2u),
    USER_ENTERED(3u);

    companion object {
        fun fromWire(value: UInt): WorkspaceFactKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class WorkspaceExportFormat(val wire: UInt) {
    MARKDOWN(0u),
    CSV(1u);

    companion object {
        fun fromWire(value: UInt): WorkspaceExportFormat? = entries.firstOrNull { it.wire == value }
    }
}

enum class TaskActivityKind(val wire: UInt) {
    OPENED_PAGE(0u),
    READ_PAGE(1u),
    PAGE_UNAVAILABLE(2u),
    MOVE_REFUSED(3u),
    ASKED_YOU(4u),
    YOU_ANSWERED(5u),
    HANDED_BACK(6u),
    YOU_TOOK_OVER(7u),
    BUILT_OUTPUT(8u);

    companion object {
        fun fromWire(value: UInt): TaskActivityKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class TaskArtifactKind(val wire: UInt) {
    MARKDOWN(0u),
    CSV(1u),
    XLSX(2u),
    PDF(3u),
    DOCX(4u),
    PPTX(5u),
    WAVE_AUDIO(6u),
    FRAME_ARCHIVE(7u);

    companion object {
        fun fromWire(value: UInt): TaskArtifactKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class LibraryAvailability(val wire: UInt) {
    AVAILABLE(0u),
    PRIVATE_PROFILE(1u),
    UNAVAILABLE(2u);

    companion object {
        fun fromWire(value: UInt): LibraryAvailability? = entries.firstOrNull { it.wire == value }
    }
}

enum class LibraryRefreshDisposition(val wire: UInt) {
    UNCHANGED(0u),
    CHANGED(1u),
    MISSING(2u);

    companion object {
        fun fromWire(value: UInt): LibraryRefreshDisposition? = entries.firstOrNull { it.wire == value }
    }
}

enum class MemoryAvailability(val wire: UInt) {
    AVAILABLE(0u),
    PRIVATE_PROFILE(1u),
    UNAVAILABLE(2u);

    companion object {
        fun fromWire(value: UInt): MemoryAvailability? = entries.firstOrNull { it.wire == value }
    }
}

enum class MemorySourceKind(val wire: UInt) {
    YOU_WROTE(0u),
    TAFFY_SUGGESTED(1u);

    companion object {
        fun fromWire(value: UInt): MemorySourceKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class MemoryScopeKind(val wire: UInt) {
    ALL_TASKS(0u),
    WORKSPACE(1u);

    companion object {
        fun fromWire(value: UInt): MemoryScopeKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class MemorySensitivity(val wire: UInt) {
    STANDARD(0u),
    SENSITIVE(1u);

    companion object {
        fun fromWire(value: UInt): MemorySensitivity? = entries.firstOrNull { it.wire == value }
    }
}

enum class PageInspectorAvailability(val wire: UInt) {
    AVAILABLE(0u),
    NO_SELECTED_PAGE(1u),
    DOCUMENT_UNAVAILABLE(2u),
    CORE_UNAVAILABLE(3u),
    POLICY_DENIED(4u),
    BACKPRESSURE(5u),
    STALE_DOCUMENT(6u),
    INVALID_RESPONSE(7u);

    companion object {
        fun fromWire(value: UInt): PageInspectorAvailability? = entries.firstOrNull { it.wire == value }
    }
}

enum class SiteSkillOfferAvailability(val wire: UInt) {
    AVAILABLE(0u),
    CORE_UNAVAILABLE(1u),
    STALE_DOCUMENT(2u),
    PRIVATE_PROFILE(3u),
    INCOMPLETE(4u),
    INVALID_RESPONSE(5u);

    companion object {
        fun fromWire(value: UInt): SiteSkillOfferAvailability? = entries.firstOrNull { it.wire == value }
    }
}

enum class PageSnapshotExportFormat(val wire: UInt) {
    MARKDOWN(0u),
    CANONICAL_JSON(1u);

    companion object {
        fun fromWire(value: UInt): PageSnapshotExportFormat? = entries.firstOrNull { it.wire == value }
    }
}

enum class PageSnapshotExportAvailability(val wire: UInt) {
    AVAILABLE(0u),
    NO_SELECTED_PAGE(1u),
    DOCUMENT_UNAVAILABLE(2u),
    CORE_UNAVAILABLE(3u),
    POLICY_DENIED(4u),
    BACKPRESSURE(5u),
    STALE_DOCUMENT(6u),
    PRIVATE_PROFILE(7u),
    INCOMPLETE(8u),
    OVERSIZE(9u),
    INVALID_RESPONSE(10u),
    CANCELLED(11u),
    REPLAY_CONFLICT(12u);

    companion object {
        fun fromWire(value: UInt): PageSnapshotExportAvailability? = entries.firstOrNull { it.wire == value }
    }
}

enum class PageInspectorDocumentKind(val wire: UInt) {
    SEMANTIC_PAGE(0u);

    companion object {
        fun fromWire(value: UInt): PageInspectorDocumentKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class PageInspectorClaim(val wire: UInt) {
    BROWSER_VALIDATED(0u),
    POLICY_ADMITTED(1u),
    REDACTED(2u),
    BOUNDED(3u);

    companion object {
        fun fromWire(value: UInt): PageInspectorClaim? = entries.firstOrNull { it.wire == value }
    }
}

enum class PageInspectorDocumentState(val wire: UInt) {
    ACTIVE(0u),
    FROZEN(1u),
    UNAVAILABLE(2u);

    companion object {
        fun fromWire(value: UInt): PageInspectorDocumentState? = entries.firstOrNull { it.wire == value }
    }
}

enum class PageInspectorNodeRole(val wire: UInt) {
    DOCUMENT(0u),
    SECTION(1u),
    TEXT(2u),
    LIST(3u),
    TABLE(4u),
    LINK(5u),
    CONTROL(6u),
    MEDIA(7u),
    COMMERCE(8u),
    REFERENCE(9u),
    UNKNOWN(10u);

    companion object {
        fun fromWire(value: UInt): PageInspectorNodeRole? = entries.firstOrNull { it.wire == value }
    }
}

enum class PageInspectorSensitivity(val wire: UInt) {
    PUBLIC(0u),
    WITHHELD(1u),
    UNKNOWN(2u);

    companion object {
        fun fromWire(value: UInt): PageInspectorSensitivity? = entries.firstOrNull { it.wire == value }
    }
}

enum class PageInspectorRelationship(val wire: UInt) {
    HIERARCHY(0u),
    LABEL(1u),
    DESCRIPTION(2u),
    CONTROL(3u),
    TABLE_HEADER(4u),
    ENTITY(5u),
    SOURCE(6u),
    OTHER(7u);

    companion object {
        fun fromWire(value: UInt): PageInspectorRelationship? = entries.firstOrNull { it.wire == value }
    }
}

enum class PageInspectorAdapterKind(val wire: UInt) {
    STRUCTURE(0u),
    ACCESSIBILITY(1u),
    FORMS(2u),
    METADATA(3u),
    BROWSER(4u);

    companion object {
        fun fromWire(value: UInt): PageInspectorAdapterKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class PageInspectorAdapterStatus(val wire: UInt) {
    COMPLETE(0u),
    PARTIAL(1u),
    CONFLICT(2u),
    UNSUPPORTED(3u),
    FAILED(4u);

    companion object {
        fun fromWire(value: UInt): PageInspectorAdapterStatus? = entries.firstOrNull { it.wire == value }
    }
}

enum class PageInspectorBudgetKind(val wire: UInt) {
    ITEMS(0u),
    TEXT(1u),
    TOTAL_BYTES(2u),
    DEPTH(3u),
    FRAMES(4u),
    MESSAGE(5u),
    DEADLINE(6u);

    companion object {
        fun fromWire(value: UInt): PageInspectorBudgetKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class PageInspectorWarningCode(val wire: UInt) {
    SOURCE_UNAVAILABLE(0u),
    SOURCE_FAILED(1u),
    CONFLICT(2u),
    FRAME_OMITTED(3u),
    CONTENT_PARTIAL(4u),
    SEMANTICS_MISSING(5u),
    CONTENT_WITHHELD(6u),
    LOCATION_MINIMIZED(7u),
    DEADLINE(8u),
    RESOURCE_PRESSURE(9u),
    SUSPICIOUS_CONTENT(10u);

    companion object {
        fun fromWire(value: UInt): PageInspectorWarningCode? = entries.firstOrNull { it.wire == value }
    }
}

enum class CoreApiSubmissionStatus(val wire: UInt) {
    ACCEPTED(0u),
    INVALID_REQUEST(1u),
    STALE_GENERATION(2u),
    STALE_REVISION(3u),
    DEADLINE_EXCEEDED(4u),
    BACKPRESSURE(5u),
    CORE_UNAVAILABLE(6u),
    DUPLICATE(7u),
    SOURCE_NOT_OPEN(8u),
    SOURCE_AMBIGUOUS(9u),
    WINDOW_UNAVAILABLE(10u);

    companion object {
        fun fromWire(value: UInt): CoreApiSubmissionStatus? = entries.firstOrNull { it.wire == value }
    }
}

enum class AssetKindView(val wire: UInt) {
    PYTHON_STDLIB(0u),
    PYTHON_PACKAGES(1u),
    MODEL_WEIGHTS(2u),
    MODEL_TOKENIZER(3u),
    FILTER_LIST(4u),
    COUNTRY_FLAGS(5u),
    START_SCENES(6u);

    companion object {
        fun fromWire(value: UInt): AssetKindView? = entries.firstOrNull { it.wire == value }
    }
}

enum class AssetPresenceView(val wire: UInt) {
    ABSENT(0u),
    PARTIAL(1u),
    COMPLETE(2u),
    INSTALLED(3u);

    companion object {
        fun fromWire(value: UInt): AssetPresenceView? = entries.firstOrNull { it.wire == value }
    }
}

enum class AssetNetworkCostView(val wire: UInt) {
    OFFLINE(0u),
    METERED(1u),
    UNMETERED(2u);

    companion object {
        fun fromWire(value: UInt): AssetNetworkCostView? = entries.firstOrNull { it.wire == value }
    }
}

enum class AssetRefusalView(val wire: UInt) {
    UNKNOWN_ASSET(0u),
    NO_VARIANT_FOR_PLATFORM(1u),
    NOT_PUBLISHED_YET(2u),
    CATALOG_ROW_INCOMPLETE(3u),
    VARIANT_TOO_LARGE(4u),
    ATTEMPTS_EXHAUSTED(5u),
    INTEGRITY_FAILED(6u),
    NETWORK_NOT_PERMITTED(7u),
    DECLINED_BY_PERSON(8u);

    companion object {
        fun fromWire(value: UInt): AssetRefusalView? = entries.firstOrNull { it.wire == value }
    }
}

enum class PartMemberStatus(val wire: UInt) {
    OK(0u),
    NOT_INSTALLED(1u),
    NOT_FOUND(2u),
    TOO_LARGE(3u),
    UNREADABLE(4u),
    INVALID_REQUEST(5u);

    companion object {
        fun fromWire(value: UInt): PartMemberStatus? = entries.firstOrNull { it.wire == value }
    }
}

enum class ProviderAuthMethodView(val wire: UInt) {
    API_KEY(0u),
    OAUTH(1u);

    companion object {
        fun fromWire(value: UInt): ProviderAuthMethodView? = entries.firstOrNull { it.wire == value }
    }
}

enum class ProviderWireApiView(val wire: UInt) {
    ANTHROPIC_MESSAGES(0u),
    OPEN_AI_RESPONSES(1u),
    OPEN_AI_COMPLETIONS(2u),
    GOOGLE_GENERATIVE_LANGUAGE(3u),
    MANAGED(4u),
    OPEN_AI_CODEX_RESPONSES(5u),
    GOOGLE_CLOUD_CODE_ASSIST(6u);

    companion object {
        fun fromWire(value: UInt): ProviderWireApiView? = entries.firstOrNull { it.wire == value }
    }
}

enum class ProviderProbeVerdictView(val wire: UInt) {
    USABLE(0u),
    AUTH(1u),
    BILLING(2u),
    RATE_LIMIT(3u),
    OVERLOADED(4u),
    TIMEOUT(5u),
    NETWORK(6u),
    MODEL_NOT_FOUND(7u),
    UNKNOWN(8u),
    ENDPOINT_REACHED(9u),
    NO_MODEL_LISTED(10u);

    companion object {
        fun fromWire(value: UInt): ProviderProbeVerdictView? = entries.firstOrNull { it.wire == value }
    }
}

enum class ProviderCredentialStateView(val wire: UInt) {
    USABLE(0u),
    NEEDS_SIGN_IN(1u),
    REFRESH_FAILED(2u);

    companion object {
        fun fromWire(value: UInt): ProviderCredentialStateView? = entries.firstOrNull { it.wire == value }
    }
}

enum class ProviderRefusalView(val wire: UInt) {
    RATE_LIMIT(0u),
    BILLING(1u),
    OVERLOADED(2u);

    companion object {
        fun fromWire(value: UInt): ProviderRefusalView? = entries.firstOrNull { it.wire == value }
    }
}

enum class ProviderOriginView(val wire: UInt) {
    CATALOG(0u),
    CUSTOM(1u);

    companion object {
        fun fromWire(value: UInt): ProviderOriginView? = entries.firstOrNull { it.wire == value }
    }
}

enum class CatalogLayerView(val wire: UInt) {
    EMBEDDED_BASELINE(0u),
    REMOTE_OVERLAY(1u),
    USER_OVERRIDE(2u);

    companion object {
        fun fromWire(value: UInt): CatalogLayerView? = entries.firstOrNull { it.wire == value }
    }
}

enum class ThinkingLevelView(val wire: UInt) {
    OFF(0u),
    MINIMAL(1u),
    LOW(2u),
    MEDIUM(3u),
    HIGH(4u),
    XHIGH(5u),
    MAX(6u);

    companion object {
        fun fromWire(value: UInt): ThinkingLevelView? = entries.firstOrNull { it.wire == value }
    }
}

enum class ModelRoleView(val wire: UInt) {
    PRIMARY_REASONING(0u),
    FAST_BROWSING(1u),
    VISION(2u),
    EMBEDDING(3u);

    companion object {
        fun fromWire(value: UInt): ModelRoleView? = entries.firstOrNull { it.wire == value }
    }
}

enum class InputModalityView(val wire: UInt) {
    TEXT(0u),
    IMAGE(1u);

    companion object {
        fun fromWire(value: UInt): InputModalityView? = entries.firstOrNull { it.wire == value }
    }
}

enum class ServerKindView(val wire: UInt) {
    OPENAI_COMPATIBLE(0u),
    OLLAMA(1u),
    LM_STUDIO(2u),
    VLLM(3u),
    LLAMA_CPP(4u);

    companion object {
        fun fromWire(value: UInt): ServerKindView? = entries.firstOrNull { it.wire == value }
    }
}

enum class SavedFlowQueryAvailability(val wire: UInt) {
    AVAILABLE(0u),
    PRIVATE_PROFILE(1u),
    UNAVAILABLE(2u),
    INVALID_REQUEST(3u),
    STALE_REQUEST(4u);

    companion object {
        fun fromWire(value: UInt): SavedFlowQueryAvailability? = entries.firstOrNull { it.wire == value }
    }
}

data class OperationEnvelope(
    val operation_id: String,
    val service_generation: ULong,
    val task_revision: ULong,
    val deadline_monotonic_ms: ULong,
    val idempotency_key: String
)

data class SiteSkillObservedClause(
    val kind: SiteSkillClauseKind,
    val role: UInt,
    val detail: UInt
)

data class SiteSkillSemanticTarget(
    val role: UInt,
    val phrase: UInt
)

data class SiteSkillObservedArgument(
    val parameter: UInt,
    val kind: SiteSkillArgumentKind,
    val value: ULong,
    val purpose: UInt,
    val public_address: String?,
    val semantic_target: SiteSkillSemanticTarget?
)

data class SiteSkillObservedStep(
    val verb: String,
    val arguments: List<SiteSkillObservedArgument>,
    val postcondition: UInt,
    val has_fill: Boolean,
    val fill_purpose: UInt
)

data class SiteSkillMutationBody(
    val kind: SiteSkillMutationKind,
    val skill_id: String,
    val expected_version: UInt,
    val origin: String,
    val clauses: List<SiteSkillObservedClause>,
    val steps: List<SiteSkillObservedStep>,
    val admitted: UInt,
    val enabled: Boolean,
    val recorded_at_epoch_ms: ULong
)

data class SiteSkillView(
    val skill_id: String,
    val origin: String,
    val provenance: SiteSkillProvenanceView,
    val status: SiteSkillStatusView,
    val active_version: UInt,
    val step_count: UInt,
    val installed_at_epoch_ms: ULong,
    val updated_at_epoch_ms: ULong,
    val recorded_from_task_id: String?,
    val reviewed_steps: List<SiteSkillObservedStep>
)

data class SiteSkillOfferView(
    val offer_id: String,
    val skill_id: String,
    val active_version: UInt,
    val step_count: UInt
)

data class CoreCommand(
    val operation: OperationEnvelope,
    val kind: CoreCommandKind,
    val start_task: StartTaskBody?,
    val cancel_task: CancelTaskBody?,
    val approve_action: ApproveActionBody?,
    val retry_core: RetryCoreBody?,
    val permission_result: PermissionResultBody?,
    val start_auth: StartAuthBody?,
    val request_email_link: EmailLinkBody?,
    val sign_out: SignOutBody?,
    val auth_credential_result: AuthCredentialResultBody?,
    val correct_workspace_fact: CorrectWorkspaceFactBody?,
    val exclude_workspace_source: ExcludeWorkspaceSourceBody?,
    val request_workspace_export: RequestWorkspaceExportBody?,
    val request_asset: RequestAssetBody?,
    val remove_asset: RemoveAssetBody?,
    val set_asset_policy: SetAssetPolicyBody?,
    val save_provider_credential: SaveProviderCredentialBody?,
    val forget_provider_credential: ForgetProviderCredentialBody?,
    val start_provider_auth: StartProviderAuthBody?,
    val save_custom_provider: SaveCustomProviderBody?,
    val remove_custom_provider: RemoveCustomProviderBody?,
    val complete_handover: CompleteHandoverBody?,
    val supply_user_input: SupplyUserInputBody?,
    val set_provider_credential_state: SetProviderCredentialStateBody?,
    val probe_provider_key: ProbeProviderKeyBody?,
    val set_provider_model_preference: SetProviderModelPreferenceBody?,
    val probe_custom_endpoint: ProbeCustomEndpointBody?,
    val request_composer_completion: RequestComposerCompletionBody?,
    val cancel_composer_completion: CancelComposerCompletionBody?,
    val pause_task: PauseTaskBody?,
    val resume_task: ResumeTaskBody?,
    val take_over: TakeOverBody?,
    val set_assistant_configuration: SetAssistantConfigurationBody?,
    val save_workspace: SaveWorkspaceBody?,
    val rename_workspace: RenameWorkspaceBody?,
    val delete_workspace: DeleteWorkspaceBody?,
    val discard_workspace: DiscardWorkspaceBody?,
    val search_library: SearchLibraryBody?,
    val save_library_fact: SaveLibraryFactBody?,
    val remove_library_entry: RemoveLibraryEntryBody?,
    val request_library_export: RequestLibraryExportBody?,
    val search_memory: SearchMemoryBody?,
    val upsert_memory: UpsertMemoryBody?,
    val delete_memory: DeleteMemoryBody?,
    val accept_task_artifact: AcceptTaskArtifactBody?,
    val request_task_artifact_export: RequestTaskArtifactExportBody?,
    val mutate_site_skill: SiteSkillMutationBody?,
    val cancel_provider_auth: CancelProviderAuthBody?,
    val start_library_refresh: StartLibraryRefreshBody?,
    val follow_up: FollowUpBody?
) {
    fun hasValidBody(): Boolean {
        val bodyCount = listOf(start_task, cancel_task, approve_action, retry_core, permission_result, start_auth, request_email_link, sign_out, auth_credential_result, correct_workspace_fact, exclude_workspace_source, request_workspace_export, request_asset, remove_asset, set_asset_policy, save_provider_credential, forget_provider_credential, start_provider_auth, save_custom_provider, remove_custom_provider, complete_handover, supply_user_input, set_provider_credential_state, probe_provider_key, set_provider_model_preference, probe_custom_endpoint, request_composer_completion, cancel_composer_completion, pause_task, resume_task, take_over, set_assistant_configuration, save_workspace, rename_workspace, delete_workspace, discard_workspace, search_library, save_library_fact, remove_library_entry, request_library_export, search_memory, upsert_memory, delete_memory, accept_task_artifact, request_task_artifact_export, mutate_site_skill, cancel_provider_auth, start_library_refresh, follow_up).count { it != null }
        if (bodyCount != 1) return false
        return when (kind) {
            CoreCommandKind.START_TASK -> start_task != null
            CoreCommandKind.CANCEL_TASK -> cancel_task != null
            CoreCommandKind.APPROVE_ACTION -> approve_action != null
            CoreCommandKind.RETRY_CORE -> retry_core != null
            CoreCommandKind.PERMISSION_RESULT -> permission_result != null
            CoreCommandKind.START_AUTH -> start_auth != null
            CoreCommandKind.REQUEST_EMAIL_LINK -> request_email_link != null
            CoreCommandKind.SIGN_OUT -> sign_out != null
            CoreCommandKind.AUTH_CREDENTIAL_RESULT -> auth_credential_result != null
            CoreCommandKind.CORRECT_WORKSPACE_FACT -> correct_workspace_fact != null
            CoreCommandKind.EXCLUDE_WORKSPACE_SOURCE -> exclude_workspace_source != null
            CoreCommandKind.REQUEST_WORKSPACE_EXPORT -> request_workspace_export != null
            CoreCommandKind.REQUEST_ASSET -> request_asset != null
            CoreCommandKind.REMOVE_ASSET -> remove_asset != null
            CoreCommandKind.SET_ASSET_POLICY -> set_asset_policy != null
            CoreCommandKind.SAVE_PROVIDER_CREDENTIAL -> save_provider_credential != null
            CoreCommandKind.FORGET_PROVIDER_CREDENTIAL -> forget_provider_credential != null
            CoreCommandKind.START_PROVIDER_AUTH -> start_provider_auth != null
            CoreCommandKind.SAVE_CUSTOM_PROVIDER -> save_custom_provider != null
            CoreCommandKind.REMOVE_CUSTOM_PROVIDER -> remove_custom_provider != null
            CoreCommandKind.COMPLETE_HANDOVER -> complete_handover != null
            CoreCommandKind.SUPPLY_USER_INPUT -> supply_user_input != null
            CoreCommandKind.SET_PROVIDER_CREDENTIAL_STATE -> set_provider_credential_state != null
            CoreCommandKind.PROBE_PROVIDER_KEY -> probe_provider_key != null
            CoreCommandKind.SET_PROVIDER_MODEL_PREFERENCE -> set_provider_model_preference != null
            CoreCommandKind.PROBE_CUSTOM_ENDPOINT -> probe_custom_endpoint != null
            CoreCommandKind.REQUEST_COMPOSER_COMPLETION -> request_composer_completion != null
            CoreCommandKind.CANCEL_COMPOSER_COMPLETION -> cancel_composer_completion != null
            CoreCommandKind.PAUSE_TASK -> pause_task != null
            CoreCommandKind.RESUME_TASK -> resume_task != null
            CoreCommandKind.TAKE_OVER -> take_over != null
            CoreCommandKind.SET_ASSISTANT_CONFIGURATION -> set_assistant_configuration != null
            CoreCommandKind.SAVE_WORKSPACE -> save_workspace != null
            CoreCommandKind.RENAME_WORKSPACE -> rename_workspace != null
            CoreCommandKind.DELETE_WORKSPACE -> delete_workspace != null
            CoreCommandKind.DISCARD_WORKSPACE -> discard_workspace != null
            CoreCommandKind.SEARCH_LIBRARY -> search_library != null
            CoreCommandKind.SAVE_LIBRARY_FACT -> save_library_fact != null
            CoreCommandKind.REMOVE_LIBRARY_ENTRY -> remove_library_entry != null
            CoreCommandKind.REQUEST_LIBRARY_EXPORT -> request_library_export != null
            CoreCommandKind.SEARCH_MEMORY -> search_memory != null
            CoreCommandKind.UPSERT_MEMORY -> upsert_memory != null
            CoreCommandKind.DELETE_MEMORY -> delete_memory != null
            CoreCommandKind.ACCEPT_TASK_ARTIFACT -> accept_task_artifact != null
            CoreCommandKind.REQUEST_TASK_ARTIFACT_EXPORT -> request_task_artifact_export != null
            CoreCommandKind.MUTATE_SITE_SKILL -> mutate_site_skill != null
            CoreCommandKind.CANCEL_PROVIDER_AUTH -> cancel_provider_auth != null
            CoreCommandKind.START_LIBRARY_REFRESH -> start_library_refresh != null
            CoreCommandKind.FOLLOW_UP -> follow_up != null
        }
    }

    companion object {
        fun startTask(operation: OperationEnvelope, body: StartTaskBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.START_TASK,
                start_task = body,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun cancelTask(operation: OperationEnvelope, body: CancelTaskBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.CANCEL_TASK,
                start_task = null,
                cancel_task = body,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun approveAction(operation: OperationEnvelope, body: ApproveActionBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.APPROVE_ACTION,
                start_task = null,
                cancel_task = null,
                approve_action = body,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun retryCore(operation: OperationEnvelope, body: RetryCoreBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.RETRY_CORE,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = body,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun permissionResult(operation: OperationEnvelope, body: PermissionResultBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.PERMISSION_RESULT,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = body,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun startAuth(operation: OperationEnvelope, body: StartAuthBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.START_AUTH,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = body,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun requestEmailLink(operation: OperationEnvelope, body: EmailLinkBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.REQUEST_EMAIL_LINK,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = body,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun signOut(operation: OperationEnvelope, body: SignOutBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.SIGN_OUT,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = body,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun authCredentialResult(operation: OperationEnvelope, body: AuthCredentialResultBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.AUTH_CREDENTIAL_RESULT,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = body,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun correctWorkspaceFact(operation: OperationEnvelope, body: CorrectWorkspaceFactBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.CORRECT_WORKSPACE_FACT,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = body,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun excludeWorkspaceSource(operation: OperationEnvelope, body: ExcludeWorkspaceSourceBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.EXCLUDE_WORKSPACE_SOURCE,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = body,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun requestWorkspaceExport(operation: OperationEnvelope, body: RequestWorkspaceExportBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.REQUEST_WORKSPACE_EXPORT,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = body,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun requestAsset(operation: OperationEnvelope, body: RequestAssetBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.REQUEST_ASSET,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = body,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun removeAsset(operation: OperationEnvelope, body: RemoveAssetBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.REMOVE_ASSET,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = body,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun setAssetPolicy(operation: OperationEnvelope, body: SetAssetPolicyBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.SET_ASSET_POLICY,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = body,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun saveProviderCredential(operation: OperationEnvelope, body: SaveProviderCredentialBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.SAVE_PROVIDER_CREDENTIAL,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = body,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun forgetProviderCredential(operation: OperationEnvelope, body: ForgetProviderCredentialBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.FORGET_PROVIDER_CREDENTIAL,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = body,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun startProviderAuth(operation: OperationEnvelope, body: StartProviderAuthBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.START_PROVIDER_AUTH,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = body,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun saveCustomProvider(operation: OperationEnvelope, body: SaveCustomProviderBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.SAVE_CUSTOM_PROVIDER,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = body,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun removeCustomProvider(operation: OperationEnvelope, body: RemoveCustomProviderBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.REMOVE_CUSTOM_PROVIDER,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = body,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun completeHandover(operation: OperationEnvelope, body: CompleteHandoverBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.COMPLETE_HANDOVER,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = body,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun supplyUserInput(operation: OperationEnvelope, body: SupplyUserInputBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.SUPPLY_USER_INPUT,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = body,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun setProviderCredentialState(operation: OperationEnvelope, body: SetProviderCredentialStateBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.SET_PROVIDER_CREDENTIAL_STATE,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = body,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun probeProviderKey(operation: OperationEnvelope, body: ProbeProviderKeyBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.PROBE_PROVIDER_KEY,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = body,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun setProviderModelPreference(operation: OperationEnvelope, body: SetProviderModelPreferenceBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.SET_PROVIDER_MODEL_PREFERENCE,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = body,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun probeCustomEndpoint(operation: OperationEnvelope, body: ProbeCustomEndpointBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.PROBE_CUSTOM_ENDPOINT,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = body,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun requestComposerCompletion(operation: OperationEnvelope, body: RequestComposerCompletionBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.REQUEST_COMPOSER_COMPLETION,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = body,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun cancelComposerCompletion(operation: OperationEnvelope, body: CancelComposerCompletionBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.CANCEL_COMPOSER_COMPLETION,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = body,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun pauseTask(operation: OperationEnvelope, body: PauseTaskBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.PAUSE_TASK,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = body,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun resumeTask(operation: OperationEnvelope, body: ResumeTaskBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.RESUME_TASK,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = body,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun takeOver(operation: OperationEnvelope, body: TakeOverBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.TAKE_OVER,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = body,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun setAssistantConfiguration(operation: OperationEnvelope, body: SetAssistantConfigurationBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.SET_ASSISTANT_CONFIGURATION,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = body,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun saveWorkspace(operation: OperationEnvelope, body: SaveWorkspaceBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.SAVE_WORKSPACE,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = body,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun renameWorkspace(operation: OperationEnvelope, body: RenameWorkspaceBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.RENAME_WORKSPACE,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = body,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun deleteWorkspace(operation: OperationEnvelope, body: DeleteWorkspaceBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.DELETE_WORKSPACE,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = body,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun discardWorkspace(operation: OperationEnvelope, body: DiscardWorkspaceBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.DISCARD_WORKSPACE,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = body,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun searchLibrary(operation: OperationEnvelope, body: SearchLibraryBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.SEARCH_LIBRARY,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = body,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun saveLibraryFact(operation: OperationEnvelope, body: SaveLibraryFactBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.SAVE_LIBRARY_FACT,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = body,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun removeLibraryEntry(operation: OperationEnvelope, body: RemoveLibraryEntryBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.REMOVE_LIBRARY_ENTRY,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = body,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun requestLibraryExport(operation: OperationEnvelope, body: RequestLibraryExportBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.REQUEST_LIBRARY_EXPORT,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = body,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun searchMemory(operation: OperationEnvelope, body: SearchMemoryBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.SEARCH_MEMORY,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = body,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun upsertMemory(operation: OperationEnvelope, body: UpsertMemoryBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.UPSERT_MEMORY,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = body,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun deleteMemory(operation: OperationEnvelope, body: DeleteMemoryBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.DELETE_MEMORY,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = body,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun acceptTaskArtifact(operation: OperationEnvelope, body: AcceptTaskArtifactBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.ACCEPT_TASK_ARTIFACT,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = body,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun requestTaskArtifactExport(operation: OperationEnvelope, body: RequestTaskArtifactExportBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.REQUEST_TASK_ARTIFACT_EXPORT,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = body,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun mutateSiteSkill(operation: OperationEnvelope, body: SiteSkillMutationBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.MUTATE_SITE_SKILL,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = body,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = null,
            )

        fun cancelProviderAuth(operation: OperationEnvelope, body: CancelProviderAuthBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.CANCEL_PROVIDER_AUTH,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = body,
                start_library_refresh = null,
                follow_up = null,
            )

        fun startLibraryRefresh(operation: OperationEnvelope, body: StartLibraryRefreshBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.START_LIBRARY_REFRESH,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = body,
                follow_up = null,
            )

        fun followUp(operation: OperationEnvelope, body: FollowUpBody): CoreCommand =
            CoreCommand(
                operation = operation,
                kind = CoreCommandKind.FOLLOW_UP,
                start_task = null,
                cancel_task = null,
                approve_action = null,
                retry_core = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                request_asset = null,
                remove_asset = null,
                set_asset_policy = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_key = null,
                set_provider_model_preference = null,
                probe_custom_endpoint = null,
                request_composer_completion = null,
                cancel_composer_completion = null,
                pause_task = null,
                resume_task = null,
                take_over = null,
                set_assistant_configuration = null,
                save_workspace = null,
                rename_workspace = null,
                delete_workspace = null,
                discard_workspace = null,
                search_library = null,
                save_library_fact = null,
                remove_library_entry = null,
                request_library_export = null,
                search_memory = null,
                upsert_memory = null,
                delete_memory = null,
                accept_task_artifact = null,
                request_task_artifact_export = null,
                mutate_site_skill = null,
                cancel_provider_auth = null,
                start_library_refresh = null,
                follow_up = body,
            )

    }
}

data class TaskConsentPreview(
    val source_hosts: List<String>,
    val source_discovery_enabled: Boolean,
    val new_source_cap: UInt,
    val provider_route: TaskProviderRoute,
    val attached_stores: List<TaskAttachedStore>
)

data class StartTaskBody(
    val request_id: String,
    val goal: String,
    val template_id: TaskTemplateId,
    val workspace_id: String?,
    val consent_preview: TaskConsentPreview,
    val skill_offer_id: String?
)

data class CancelTaskBody(
    val task_id: String
)

data class PauseTaskBody(
    val task_id: String
)

data class ResumeTaskBody(
    val task_id: String
)

data class TakeOverBody(
    val task_id: String
)

data class AssistantConfigurationView(
    val revision: ULong,
    val disabled_abilities: List<AssistantAbilityView>,
    val preset: PersonalityPresetView,
    val pace: UInt,
    val length: UInt,
    val check_in: UInt
)

data class BuiltinSkillReferenceView(
    val skill_id: BuiltinSkillIdView,
    val version: UInt
)

data class BuiltinSkillView(
    val reference: BuiltinSkillReferenceView,
    val required_ability: AssistantAbilityView,
    val enabled: Boolean,
    val availability: BuiltinSkillAvailabilityView,
    val required_tool_count: UInt,
    val available_tool_count: UInt,
    val required_part_count: UInt,
    val installed_part_count: UInt
)

data class CoreStatusProjectionOmission(
    val family: CoreStatusProjectionFamily,
    val revision: ULong,
    val item_count: UInt
)

data class SetAssistantConfigurationBody(
    val expected_revision: ULong,
    val disabled_abilities: List<AssistantAbilityView>,
    val preset: PersonalityPresetView,
    val pace: UInt,
    val length: UInt,
    val check_in: UInt
)

data class CompleteHandoverBody(
    val task_id: String
)

data class SupplyUserInputBody(
    val task_id: String,
    val answer: String
)

data class FollowUpBody(
    val task_id: String,
    val question: String
)

data class ApproveActionBody(
    val task_id: String,
    val action_id: String
)

data class RetryCoreBody(
    val observed_generation: ULong
)

data class CoreFailure(
    val code: CoreFailureCode,
    val retryable: Boolean,
    val message_key: String?
)

data class TaskActivityView(
    val sequence: ULong,
    val kind: TaskActivityKind,
    val host: String?,
    val count: UInt,
    val at_epoch_ms: ULong
)

data class TaskArtifactView(
    val artifact_id: String,
    val kind: TaskArtifactKind,
    val workspace_revision: ULong,
    val accepted: Boolean
)

data class TaskViewState(
    val task_id: String,
    val revision: ULong,
    val phase: TaskPhase,
    val progress_basis_points: UInt,
    val status_message_key: String?,
    val failure: CoreFailure?,
    val goal: String,
    val template_id: TaskTemplateId,
    val pending_action: ActionApprovalView?,
    val workspace_id: String?,
    val pending_ask_prompt: String?,
    val pending_field_value_request: String?,
    val allowed_controls: List<TaskControlKind>,
    val artifacts: List<TaskArtifactView>,
    val activity: List<TaskActivityView>
)

data class ActionApprovalView(
    val action_id: String,
    val host: String?,
    val item_count: UInt,
    val summary_message_key: String
)

data class StoredCredentialView(
    val auth_method: ProviderAuthMethodView,
    val state: ProviderCredentialStateView,
    val subscription_backed: Boolean,
    val account_label: String?,
    val plan_label: String?
)

data class ThinkingPreferenceView(
    val level: ThinkingLevelView
)

data class ProviderRefusalStateView(
    val refusal: ProviderRefusalView,
    val at_monotonic_ms: ULong
)

data class ProviderPresentationView(
    val key_prefix: String?,
    val get_key_url: String?,
    val docs_url: String?
)

data class ProviderModelView(
    val provider_id: String,
    val model_id: String,
    val display_name: String,
    val context_window: ULong,
    val max_output_tokens: ULong,
    val reasoning: Boolean,
    val tool_calling: Boolean,
    val roles: List<ModelRoleView>,
    val input_modalities: List<InputModalityView>,
    val thinking_levels: List<ThinkingLevelView>
)

data class ProviderRosterEntry(
    val provider_id: String,
    val display_name: String,
    val origin: ProviderOriginView,
    val auth_methods: List<ProviderAuthMethodView>,
    val stored: StoredCredentialView?,
    val signing_in: Boolean,
    val enabled: Boolean,
    val endpoint_host: String?,
    val configurable: Boolean,
    val endpoint_changed: Boolean,
    val catalog_layer: CatalogLayerView,
    val selected_model_id: String?,
    val thinking: ThinkingPreferenceView?,
    val presentation: ProviderPresentationView?,
    val endpoint_base: String?,
    val last_refusal: ProviderRefusalStateView?,
    val model_count: UInt,
    val subscription: Boolean,
    val refused_endpoint_host: String?
)

data class SetProviderCredentialStateBody(
    val provider_id: String,
    val state: ProviderCredentialStateView
)

data class ProbeProviderKeyBody(
    val provider_id: String,
    val credential_handle: String
)

data class ProviderProbeView(
    val provider_id: String,
    val verdict: ProviderProbeVerdictView,
    val at_monotonic_ms: ULong,
    val endpoint: ProbeEndpointView?
)

data class ProbeEndpointView(
    val server_kind: ServerKindView,
    val model_count: UInt,
    val models: List<CustomModelSpecView>,
    val proved_base: String?
)

data class DetectedServerView(
    val server_kind: ServerKindView
)

data class SavedSignInView(
    val id: String,
    val site: String,
    val username: String,
    val last_used_epoch_ms: ULong
)

data class SavedSignInsView(
    val availability: SavedDataAvailability,
    val revision: ULong,
    val records: List<SavedSignInView>
)

data class SavedDetailView(
    val id: String,
    val given_name: String,
    val family_name: String,
    val email: String,
    val phone: String,
    val address: String,
    val postcode: String,
    val country: String
)

data class SavedDetailsView(
    val availability: SavedDataAvailability,
    val revision: ULong,
    val people: List<SavedDetailView>
)

data class CoreStatus(
    val availability: CoreAvailability,
    val generation: ULong,
    val active_tasks: List<TaskViewState>,
    val auth_state: AuthViewState?,
    val workspaces: List<WorkspaceViewState>,
    val workspace_export: WorkspaceExportView?,
    val asset_delivery: AssetDeliveryView?,
    val provider_roster: List<ProviderRosterEntry>,
    val provider_probes: List<ProviderProbeView>,
    val provider_models: List<ProviderModelView>,
    val assistant_configuration: AssistantConfigurationView,
    val library: LibraryViewState,
    val library_export: LibraryExportView?,
    val memory: MemoryViewState,
    val saved_sign_ins: SavedSignInsView,
    val saved_details: SavedDetailsView,
    val site_skills: List<SiteSkillView>,
    val builtin_skills: List<BuiltinSkillView>,
    val projection_mode: CoreStatusProjectionMode,
    val projection_omissions: List<CoreStatusProjectionOmission>
)

data class WorkspaceViewState(
    val workspace_id: String,
    val revision: ULong,
    val goal: String,
    val phase: WorkspacePhase,
    val last_updated_epoch_ms: ULong,
    val template_id: TaskTemplateId,
    val sources: List<WorkspaceSourceView>,
    val facts: List<WorkspaceFactView>,
    val saved: Boolean,
    val display_name: String,
    val deletion_preview: WorkspaceDeletionPreviewView?
)

data class WorkspaceDeletionPreviewView(
    val sources: UInt,
    val facts: UInt,
    val artifact_metadata: UInt,
    val derived_indexes: UInt,
    val confirmation_token: String
)

data class WorkspaceSourceView(
    val source_id: String,
    val title: String,
    val host: String,
    val read_at_epoch_ms: ULong,
    val fact_count: UInt,
    val excluded: Boolean
)

data class WorkspaceFactView(
    val fact_id: String,
    val field: String,
    val value: String,
    val kind: WorkspaceFactKind,
    val sources: List<String>,
    val correction: String?,
    val has_conflict: Boolean,
    val needs_new_source: Boolean
)

data class WorkspaceExportView(
    val request_id: String,
    val workspace_id: String,
    val revision: ULong,
    val format: WorkspaceExportFormat,
    val content: String
)

data class LibrarySourceView(
    val source_id: String,
    val title: String,
    val host: String,
    val observed_at_epoch_ms: ULong
)

data class LibraryEntryView(
    val entry_id: String,
    val revision: ULong,
    val collection_id: String,
    val collection_name: String,
    val source_workspace_id: String,
    val source_workspace_revision: ULong,
    val source_fact_id: String,
    val field: String,
    val original_value: String,
    val correction: String?,
    val kind: WorkspaceFactKind,
    val sources: List<LibrarySourceView>,
    val captured_at_epoch_ms: ULong,
    val last_checked_epoch_ms: ULong,
    val has_conflict: Boolean
)

data class LibrarySearchHitView(
    val entry_id: String,
    val age_ms: ULong
)

data class LibrarySearchView(
    val request_id: String,
    val query: String,
    val library_revision: ULong,
    val hits: List<LibrarySearchHitView>
)

data class LibraryRefreshSourceView(
    val source_id: String,
    val title: String,
    val host: String
)

data class LibraryRefreshPreviewView(
    val preview_id: String,
    val collection_id: String,
    val library_revision: ULong,
    val source_workspace_revision: ULong,
    val provider_route: TaskProviderRoute,
    val navigation_count: UInt,
    val observation_count: UInt,
    val sources: List<LibraryRefreshSourceView>
)

data class LibraryRefreshResultItemView(
    val source_id: String,
    val disposition: LibraryRefreshDisposition
)

data class LibraryRefreshResultView(
    val preview_id: String,
    val collection_id: String,
    val items: List<LibraryRefreshResultItemView>
)

data class LibraryViewState(
    val availability: LibraryAvailability,
    val revision: ULong,
    val entries: List<LibraryEntryView>,
    val search: LibrarySearchView?,
    val refresh_previews: List<LibraryRefreshPreviewView>,
    val refresh_results: List<LibraryRefreshResultView>
)

data class LibraryExportView(
    val request_id: String,
    val library_revision: ULong,
    val collection_id: String?,
    val format: WorkspaceExportFormat,
    val content: String
)

data class MemoryWorkspaceView(
    val workspace_id: String,
    val display_name: String
)

data class MemoryRecordView(
    val memory_id: String,
    val revision: ULong,
    val statement: String,
    val source_kind: MemorySourceKind,
    val source_task_id: String?,
    val source_workspace: MemoryWorkspaceView?,
    val scope_kind: MemoryScopeKind,
    val scope_workspace: MemoryWorkspaceView?,
    val sensitivity: MemorySensitivity,
    val created_at_epoch_ms: ULong,
    val updated_at_epoch_ms: ULong,
    val reviewed_at_epoch_ms: ULong,
    val expires_at_epoch_ms: ULong
)

data class MemorySearchHitView(
    val memory_id: String
)

data class MemorySearchView(
    val request_id: String,
    val query: String,
    val memory_revision: ULong,
    val hits: List<MemorySearchHitView>
)

data class MemoryViewState(
    val availability: MemoryAvailability,
    val revision: ULong,
    val records: List<MemoryRecordView>,
    val search: MemorySearchView?
)

data class CorrectWorkspaceFactBody(
    val workspace_id: String,
    val expected_revision: ULong,
    val fact_id: String,
    val value: String
)

data class ExcludeWorkspaceSourceBody(
    val workspace_id: String,
    val expected_revision: ULong,
    val source_id: String
)

data class RequestWorkspaceExportBody(
    val request_id: String,
    val workspace_id: String,
    val expected_revision: ULong,
    val format: WorkspaceExportFormat
)

data class SaveWorkspaceBody(
    val workspace_id: String,
    val expected_revision: ULong
)

data class RenameWorkspaceBody(
    val workspace_id: String,
    val expected_revision: ULong,
    val display_name: String
)

data class DeleteWorkspaceBody(
    val workspace_id: String,
    val expected_revision: ULong,
    val confirmation_token: String
)

data class DiscardWorkspaceBody(
    val workspace_id: String,
    val expected_revision: ULong
)

data class SearchLibraryBody(
    val request_id: String,
    val query: String,
    val limit: UInt,
    val requested_at_epoch_ms: ULong
)

data class SaveLibraryFactBody(
    val workspace_id: String,
    val expected_workspace_revision: ULong,
    val fact_id: String,
    val expected_library_revision: ULong,
    val expected_entry_revision: ULong,
    val approved_at_epoch_ms: ULong
)

data class RemoveLibraryEntryBody(
    val entry_id: String,
    val expected_library_revision: ULong,
    val expected_entry_revision: ULong,
    val removed_at_epoch_ms: ULong
)

data class RequestLibraryExportBody(
    val request_id: String,
    val expected_library_revision: ULong,
    val collection_id: String?,
    val format: WorkspaceExportFormat
)

data class SearchMemoryBody(
    val request_id: String,
    val query: String,
    val limit: UInt,
    val requested_at_epoch_ms: ULong
)

data class UpsertMemoryBody(
    val memory_id: String?,
    val statement: String,
    val scope_kind: MemoryScopeKind,
    val scope_workspace: MemoryWorkspaceView?,
    val sensitivity: MemorySensitivity,
    val expected_memory_revision: ULong,
    val expected_record_revision: ULong,
    val expires_at_epoch_ms: ULong,
    val approved_at_epoch_ms: ULong
)

data class DeleteMemoryBody(
    val memory_id: String,
    val expected_memory_revision: ULong,
    val expected_record_revision: ULong,
    val deleted_at_epoch_ms: ULong
)

data class AcceptTaskArtifactBody(
    val task_id: String,
    val artifact_id: String
)

data class RequestTaskArtifactExportBody(
    val request_id: String,
    val task_id: String,
    val artifact_id: String,
    val kind: TaskArtifactKind
)

data class StartAuthBody(
    val provider: AuthProvider
)

data class EmailLinkBody(
    val email: String
)

data class SignOutBody(
    val account_id: String?
)

data class AuthCredentialResultBody(
    val provider: AuthProvider,
    val credential_handle: String?,
    val status: AuthCredentialStatus,
    val flow_id: String
)

data class AuthAccountView(
    val account_id: String,
    val display_name: String?,
    val email: String?,
    val method: AuthProvider
)

data class AuthFailure(
    val code: AuthFailureCode,
    val retryable: Boolean
)

data class AuthMethodView(
    val provider: AuthProvider,
    val availability: AuthMethodAvailability
)

data class EntitlementView(
    val plan_id: String,
    val credits_granted: ULong,
    val credits_remaining: ULong,
    val next_renewal_epoch_seconds: ULong,
    val valid_until_epoch_seconds: ULong
)

data class AuthViewState(
    val phase: AuthPhase,
    val account: AuthAccountView?,
    val pending_email: String?,
    val failure: AuthFailure?,
    val methods: List<AuthMethodView>,
    val entitlement: EntitlementView?
)

data class PageInspectorDocumentView(
    val document_id: String,
    val kind: PageInspectorDocumentKind,
    val host: String,
    val claims: List<PageInspectorClaim>
)

data class PageInspectorDocumentsView(
    val availability: PageInspectorAvailability,
    val documents: List<PageInspectorDocumentView>
)

data class PageInspectorAdapterView(
    val kind: PageInspectorAdapterKind,
    val status: PageInspectorAdapterStatus,
    val version: UInt
)

data class PageInspectorNodeView(
    val display_id: String,
    val role: PageInspectorNodeRole,
    val name: String?,
    val sensitivity: PageInspectorSensitivity,
    val text_run_count: UInt,
    val text_byte_count: ULong,
    val value_present: Boolean,
    val value_withheld: Boolean
)

data class PageInspectorEdgeView(
    val from_display_id: String,
    val to_display_id: String,
    val relationship: PageInspectorRelationship,
    val inferred: Boolean
)

data class PageInspectorFrameView(
    val main_frame: Boolean,
    val out_of_process: Boolean,
    val cross_origin: Boolean,
    val included: Boolean
)

data class PageInspectorTruncationView(
    val truncated: Boolean,
    val budgets_reached: List<PageInspectorBudgetKind>,
    val omitted_node_count: UInt,
    val omitted_text_bytes: UInt,
    val omitted_frame_count: UInt,
    val may_change_answer: Boolean
)

data class PageInspectorRedactionView(
    val redacted_field_count: UInt,
    val suppressed_secret_count: UInt,
    val sensitive_zone_count: UInt,
    val filtered_frame_count: UInt
)

data class PageInspectorSnapshotView(
    val document_id: String,
    val document_revision: ULong,
    val host: String,
    val secure_context: Boolean,
    val private_profile: Boolean,
    val document_state: PageInspectorDocumentState,
    val adapters: List<PageInspectorAdapterView>,
    val nodes: List<PageInspectorNodeView>,
    val edges: List<PageInspectorEdgeView>,
    val frames: List<PageInspectorFrameView>,
    val truncation: PageInspectorTruncationView,
    val redaction: PageInspectorRedactionView,
    val warnings: List<PageInspectorWarningCode>,
    val site_skill_offer_availability: SiteSkillOfferAvailability,
    val site_skill_offers: List<SiteSkillOfferView>
)

data class PageInspectorSnapshotResult(
    val availability: PageInspectorAvailability,
    val snapshot: PageInspectorSnapshotView?
)

fun PageInspectorSnapshotResult.hasValidPresence(): Boolean =
    (if (availability == PageInspectorAvailability.AVAILABLE) snapshot != null else snapshot == null)

data class PageSnapshotExportView(
    val request_id: String,
    val document_id: String,
    val document_revision: ULong,
    val origin: String,
    val format: PageSnapshotExportFormat,
    val mime_type: String,
    val suggested_file_name: String,
    val content: ByteArray,
    val node_count: UInt,
    val redacted_field_count: UInt,
    val suppressed_secret_value_count: UInt,
    val withheld_field_count: UInt,
    val captured_at_epoch_ms: ULong,
    val source_query_withheld: Boolean,
    val source_fragment_withheld: Boolean,
    val secure_context: Boolean
)

data class PageSnapshotExportResult(
    val availability: PageSnapshotExportAvailability,
    val snapshot_export: PageSnapshotExportView?
)

data class PermissionResultBody(
    val request_id: String,
    val permission: PlatformPermission,
    val decision: PermissionDecision
)

data class AssetRefusal(
    val reason: AssetRefusalView,
    val retryable: Boolean
)

data class AssetViewState(
    val asset_id: String,
    val asset_revision: String,
    val kind: AssetKindView,
    val presence: AssetPresenceView,
    val written_bytes: ULong,
    val total_bytes: ULong,
    val attempts: UInt,
    val refusal: AssetRefusal?,
    val waiting_until_monotonic_ms: ULong
)

data class AssetDeliveryView(
    val platform_supported: Boolean,
    val network_cost: AssetNetworkCostView,
    val metered_permitted: Boolean,
    val assets: List<AssetViewState>
)

data class RequestAssetBody(
    val asset_id: String,
    val asset_revision: String
)

data class RemoveAssetBody(
    val asset_id: String,
    val asset_revision: String
)

data class SetAssetPolicyBody(
    val network_cost: AssetNetworkCostView,
    val metered_permitted: Boolean
)

data class SaveProviderCredentialBody(
    val provider_id: String,
    val auth_method: ProviderAuthMethodView,
    val credential_handle: String
)

data class ForgetProviderCredentialBody(
    val provider_id: String
)

data class StartProviderAuthBody(
    val provider_id: String
)

data class CancelProviderAuthBody(
    val flow_id: String
)

data class StartLibraryRefreshBody(
    val preview_id: String,
    val collection_id: String,
    val expected_library_revision: ULong,
    val expected_workspace_revision: ULong,
    val source_count: UInt
)

data class CustomModelSpecView(
    val model_id: String,
    val display_name: String,
    val context_window: UInt,
    val max_output_tokens: UInt,
    val reasoning: Boolean,
    val tool_calling: Boolean
)

data class SaveCustomProviderBody(
    val provider_id: String,
    val display_name: String,
    val endpoint: String,
    val wire_api: ProviderWireApiView,
    val credential_handle: String?,
    val models: List<CustomModelSpecView>,
    val detected_server: DetectedServerView?
)

data class ProbeCustomEndpointBody(
    val endpoint: String,
    val wire_api: ProviderWireApiView,
    val credential_handle: String?,
    val provider_id: String
)

data class SetProviderModelPreferenceBody(
    val provider_id: String,
    val model_id: String?,
    val thinking: ThinkingPreferenceView?
)

data class RequestComposerCompletionBody(
    val request_id: String,
    val prefix: String,
    val suffix: String?
)

data class CancelComposerCompletionBody(
    val request_id: String
)

data class RemoveCustomProviderBody(
    val provider_id: String
)

data class SavedFlowQueryResult(
    val request_id: String,
    val service_generation: ULong,
    val availability: SavedFlowQueryAvailability,
    val flows: List<SiteSkillView>
)
