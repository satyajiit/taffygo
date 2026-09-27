// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Generated from schema/contract.json. Do not edit.
// Contract core_service 2.113.

package taffy.core_service

const val MAX_SAVED_FLOW_QUERY_RESULTS: Int = 4
const val MAX_COMMAND_BYTES: Int = 262144
const val MAX_EFFECT_BYTES: Int = 1048576
const val MAX_IN_FLIGHT_PER_PROFILE: Int = 64
const val MAX_QUEUED_BYTES_PER_PROFILE: Int = 4194304
const val MAX_OPERATION_ID_BYTES: Int = 128
const val MAX_IDEMPOTENCY_KEY_BYTES: Int = 256
const val MAX_IDENTIFIER_BYTES: Int = 256
const val MAX_USER_INPUT_ANSWER_BYTES: Int = 512
const val MAX_AUTHORITY_SUBJECT_ID_BYTES: Int = 128
const val MAX_DIRECT_OBSERVATION_NODES: Int = 1500
const val MAX_DIRECT_OBSERVATION_TEXT_BYTES: Int = 65536
const val MAX_DIRECT_OBSERVATION_TOTAL_BYTES: Int = 1048576
const val MAX_DIRECT_OBSERVATION_FRAMES: Int = 1
const val MAX_DIRECT_OBSERVATION_DEADLINE_MS: Int = 1500
const val MAX_DIRECT_OBSERVATION_LEASE_MS: Int = 2000
const val MAX_PAGE_SNAPSHOT_EXPORT_BYTES: Int = 262144
const val MAX_TASK_ARTIFACT_EXPORT_BYTES: Int = 262144
const val MAX_ACCOUNT_EMAIL_BYTES: Int = 320
const val MAX_ACCOUNT_DISPLAY_NAME_BYTES: Int = 128
const val MAX_ACCOUNT_SCOPES: Int = 3
const val MAX_ACCOUNT_RESPONSE_BYTES: Int = 65536
const val MAX_ACCOUNT_ACCESS_TOKEN_BYTES: Int = 16384
const val MAX_ACCOUNT_REFRESH_TOKEN_BYTES: Int = 16384
const val MAX_ACCOUNT_ID_TOKEN_BYTES: Int = 16384
const val MAX_ACCOUNT_LINKED_PROVIDERS: Int = 4
const val MAX_ENTITLED_MODELS: Int = 64
const val MAX_ACCOUNT_SESSION_LIFETIME_SECONDS: Int = 31536000
const val MAX_PENDING_ACCOUNT_FLOWS: Int = 8
const val GOOGLE_NONCE_ENTROPY_BYTES: Int = 32
const val GOOGLE_NONCE_HASH_HEX_BYTES: Int = 64
const val MAX_GOOGLE_RAW_NONCE_BYTES: Int = 43
const val MAX_GOAL_BYTES: Int = 65536
const val MAX_TASK_CONSENT_SOURCES: Int = 64
const val MAX_NORMALIZED_ORIGIN_BYTES: Int = 2048
const val MAX_LIBRARY_REFRESH_SOURCES: Int = 64
const val MAX_SOURCE_LOCATOR_BYTES: Int = 4096
const val MAX_SOURCE_TITLE_BYTES: Int = 1024
const val MAX_SOURCE_HOST_BYTES: Int = 253
const val MAX_LIBRARY_REFRESH_PREVIEW_ID_BYTES: Int = 64
const val MAX_DESTINATION_ADDRESS_BYTES: Int = 4096
const val MAX_CANONICAL_ACTION_INTENT_BYTES: Int = 16384
const val MAX_TRANSIENT_SEARCH_QUERY_BYTES: Int = 4096
const val MAX_NEW_SOURCE_CAP: Int = 64
const val MAX_WORKSPACES_PER_PROFILE: Int = 32
const val MAX_WORKSPACE_SNAPSHOT_BYTES: Int = 262144
const val MAX_WORKSPACE_BOOTSTRAP_BYTES: Int = 4194304
const val MAX_WORKSPACE_VALUE_BYTES: Int = 16384
const val MAX_WORKSPACE_DISPLAY_NAME_BYTES: Int = 256
const val MAX_WORKSPACE_CONFIRMATION_TOKEN_BYTES: Int = 64
const val MAX_LIBRARY_ENTRIES: Int = 1024
const val MAX_LIBRARY_SOURCES: Int = 16
const val MAX_LIBRARY_QUERY_BYTES: Int = 512
const val MAX_LIBRARY_SEARCH_TERMS: Int = 16
const val MAX_LIBRARY_SEARCH_RESULTS: Int = 32
const val MAX_MEMORY_RECORDS: Int = 512
const val MAX_MEMORY_STATEMENT_BYTES: Int = 2048
const val MAX_MEMORY_QUERY_BYTES: Int = 512
const val MAX_MEMORY_SEARCH_TERMS: Int = 16
const val MAX_MEMORY_SEARCH_RESULTS: Int = 32
const val MAX_ASSET_PATH_BYTES: Int = 256
const val MAX_ASSET_TRANSFER_BYTES: Int = 4294967296
const val MAX_TOOL_ALLOWLIST_ENTRIES: Int = 128
const val MAX_TASK_BUDGET_ENTRIES: Int = 10
const val MAX_PENDING_APPROVALS_PER_PROFILE: Int = 64
const val MAX_PENDING_PERMISSIONS_PER_PROFILE: Int = 64
const val MAX_PENDING_TASK_POLICY_PER_PROFILE: Int = 64
const val MAX_TASK_EFFECTS_PER_STATE: Int = 64
const val MAX_TASK_TAB_RESULTS: Int = 16
const val MAX_TASK_DOWNLOAD_RESULTS: Int = 16
const val MAX_TASK_STORE_RESULTS: Int = 32
const val MAX_TASK_STORE_ROW_FIELD_BYTES: Int = 512
const val MAX_TASK_STORE_QUERY_BYTES: Int = 512
const val MAX_TASK_SUPPLIED_VALUES: Int = 8
const val MAX_TASK_ACTION_PRECONDITIONS: Int = 4
const val MAX_TASK_OBSERVATION_NODES: Int = 1500
const val MAX_TASK_OBSERVATION_TEXT_BYTES: Int = 65536
const val MAX_TASK_OBSERVATION_TOTAL_BYTES: Int = 1048576
const val MAX_TASK_OBSERVATION_FRAMES: Int = 1
const val MAX_TASK_OBSERVATION_DEADLINE_MS: Int = 1500
const val MAX_MEDIA_FACTS: Int = 256
const val MAX_MEDIA_FACT_TEXT_BYTES: Int = 8192
const val MAX_MEDIA_FACT_LOCATOR_BYTES: Int = 512
const val MAX_MEDIA_FACT_TOTAL_BYTES: Int = 131072
const val MAX_MEDIA_ATTACHMENT_HANDLE_BYTES: Int = 128
const val MAX_MEDIA_ATTACHMENT_BYTES: Int = 4194304
const val MAX_MEDIA_DIMENSION_PX: Int = 4096
const val MAX_MEDIA_PDF_PAGES: Int = 256
const val MAX_TASK_REVISIONS_PER_PROFILE: Int = 64
const val MAX_TASK_CONTROLS: Int = 4
const val MAX_TOOL_JOB_INPUT_BYTES: Int = 16777216
const val MAX_TOOL_JOB_OUTPUT_BYTES: Int = 16777216
const val MAX_TOOL_OUTPUT_CHUNK_BYTES: Int = 65536
const val MAX_TOOL_OUTPUT_CHUNKS: Int = 256
const val MAX_TOOL_PROGRESS_EVENTS: Int = 256
const val MAX_TOOL_JOB_ID_BYTES: Int = 128
const val MAX_TOOL_OPERATION_ID_BYTES: Int = 128
const val MAX_TOOL_IDEMPOTENCY_KEY_BYTES: Int = 128
const val MAX_TOOL_ID_BYTES: Int = 128
const val MAX_TOOL_VERSION_BYTES: Int = 64
const val MAX_TOOL_CONVERSATION_ID_BYTES: Int = 128
const val MAX_TOOL_ENTRYPOINT_ID_BYTES: Int = 128
const val MAX_TOOL_HANDLE_ID_BYTES: Int = 256
const val MAX_TOOL_PRESET_ID_BYTES: Int = 128
const val MAX_TOOL_JOB_MEMORY_BYTES: Int = 4294967296
const val MAX_TOOL_JOB_CPU_MS: Int = 600000
const val MAX_TOOL_JOB_TEMPORARY_BYTES: Int = 1073741824
const val MAX_TOOL_JOB_WALL_TIME_MS: Int = 600000
const val MAX_TOOL_INLINE_PAYLOAD_BYTES: Int = 262144
const val MAX_TOOL_STREAM_OUTPUT_CHUNKS: Int = 65536
const val MAX_TOOL_STREAM_CHUNK_BYTES: Int = 4096
const val MAX_TOOL_MODEL_ID_BYTES: Int = 128
const val MAX_TOOL_MODEL_REVISION_BYTES: Int = 64
const val MAX_TOOL_MODEL_ARTIFACT_BYTES: Int = 2147483648
const val MAX_REGISTERED_MODEL_ARTIFACTS: Int = 64
const val MAX_TOOL_EMBEDDING_DIMENSIONS: Int = 4096
const val MAX_TOOL_EMBEDDING_VALUE_BYTES: Int = 16384
const val MAX_PROVIDER_ID_BYTES: Int = 64
const val MAX_PROVIDER_DISPLAY_NAME_BYTES: Int = 128
const val MAX_PROVIDER_ENDPOINT_BYTES: Int = 512
const val MAX_CUSTOM_PROVIDERS: Int = 32
const val MAX_MODEL_ID_BYTES: Int = 128
const val MAX_AVAILABLE_MODEL_IDS: Int = 64
const val MAX_MODEL_STATIC_HEADERS: Int = 8
const val MAX_MODEL_HEADER_NAME_BYTES: Int = 64
const val MAX_MODEL_HEADER_VALUE_BYTES: Int = 1024
const val MAX_MODEL_STREAM_CHUNK_BYTES: Int = 16384
const val MAX_TASK_ANSWER_DELTA_BYTES: Int = 16384
const val MAX_TASK_ANSWER_EVENTS_PER_BATCH: Int = 64
const val MAX_SKILLS_PER_PROFILE: Int = 64
const val MAX_SKILL_VERSIONS_PER_SKILL: Int = 16
const val MAX_SKILL_ID_BYTES: Int = 128
const val MAX_SKILL_DEFINITION_BYTES: Int = 65536
const val MAX_SKILL_STEPS: Int = 32
const val MAX_SKILL_MATCH_CLAUSES: Int = 8
const val MAX_SKILL_ARGUMENTS_PER_STEP: Int = 16
const val MAX_SKILL_RECALL_ENTRIES: Int = 32
const val MAX_CUSTOM_MODEL_ENTRIES: Int = 32
const val MAX_MODEL_DISPLAY_NAME_BYTES: Int = 128
const val MAX_COMPOSER_PREFIX_BYTES: Int = 4096
const val MAX_COMPOSER_SUFFIX_BYTES: Int = 1024
const val MAX_COMPOSER_COMPLETION_BYTES: Int = 512
const val MAX_PROVIDER_LISTING_BYTES: Int = 262144
const val MAX_ASSISTANT_ABILITIES: Int = 16
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
const val MAX_BACKUP_RECORDS: Int = 100000
const val MAX_BACKUP_RECORD_BYTES: Int = 67108864
const val MAX_BACKUP_PLAINTEXT_BYTES: Int = 8589934592
const val MAX_BACKUP_MANIFEST_BYTES: Int = 67108864
const val MAX_BACKUP_ID_BYTES: Int = 160
const val MAX_BACKUP_TIMESTAMP_BYTES: Int = 40
const val MAX_BACKUP_SELECTION_KINDS: Int = 8
const val MAX_BACKUP_RESTORE_RECOVERY_RECORDS: Int = 128
const val MAX_BACKUP_SEALED_CHUNKS: Int = 8193
const val BACKUP_PAYLOAD_CHUNK_BYTES: Int = 1048576

enum class CoreServiceCommandKind(val wire: UInt) {
    START_TASK(0u),
    CANCEL_TASK(1u),
    USER_DECISION(2u),
    AUTH_CALLBACK(3u),
    PERMISSION_RESULT(4u),
    START_AUTH(5u),
    REQUEST_EMAIL_LINK(6u),
    SIGN_OUT(7u),
    AUTH_CREDENTIAL_RESULT(8u),
    CORRECT_WORKSPACE_FACT(9u),
    EXCLUDE_WORKSPACE_SOURCE(10u),
    REQUEST_WORKSPACE_EXPORT(11u),
    SET_ASSET_DELIVERY_POLICY(12u),
    REQUEST_ASSET(13u),
    REMOVE_ASSET(14u),
    SAVE_PROVIDER_CREDENTIAL(15u),
    FORGET_PROVIDER_CREDENTIAL(16u),
    START_PROVIDER_AUTH(17u),
    PROVIDER_AUTH_CALLBACK(18u),
    SAVE_CUSTOM_PROVIDER(19u),
    REMOVE_CUSTOM_PROVIDER(20u),
    COMPLETE_HANDOVER(21u),
    EXPIRE_HANDOVER(22u),
    SUPPLY_USER_INPUT(23u),
    SET_PROVIDER_CREDENTIAL_STATE(24u),
    PROBE_PROVIDER_CREDENTIAL(25u),
    SUPPLY_FIELD_VALUES(26u),
    SET_PROVIDER_MODEL_PREFERENCE(27u),
    PROBE_CUSTOM_ENDPOINT(28u),
    REQUEST_COMPOSER_COMPLETION(29u),
    CANCEL_COMPOSER_COMPLETION(30u),
    PAUSE_TASK(31u),
    RESUME_TASK(32u),
    TAKE_OVER(33u),
    SET_ASSISTANT_CONFIGURATION(34u),
    SAVE_WORKSPACE(35u),
    RENAME_WORKSPACE(36u),
    DELETE_WORKSPACE(37u),
    DISCARD_WORKSPACE(38u),
    SEARCH_LIBRARY(39u),
    SAVE_LIBRARY_FACT(40u),
    REMOVE_LIBRARY_ENTRY(41u),
    REQUEST_LIBRARY_EXPORT(42u),
    SEARCH_MEMORY(43u),
    UPSERT_MEMORY(44u),
    DELETE_MEMORY(45u),
    ACCEPT_TASK_ARTIFACT(46u),
    EXPORT_TASK_ARTIFACT(47u),
    REPLACE_SAVED_DATA_SNAPSHOT(48u),
    MUTATE_SKILL(49u),
    CANCEL_PROVIDER_AUTH(50u),
    FOLLOW_UP(51u);

    companion object {
        fun fromWire(value: UInt): CoreServiceCommandKind? = entries.firstOrNull { it.wire == value }
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

enum class TaskControlKind(val wire: UInt) {
    PAUSE(0u),
    RESUME(1u),
    TAKE_OVER(2u),
    STOP(3u);

    companion object {
        fun fromWire(value: UInt): TaskControlKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class EffectKind(val wire: UInt) {
    STORAGE_COMMIT(0u),
    PAGE_OBSERVATION(1u),
    MODEL_REQUEST(2u),
    NETWORK_REQUEST(3u),
    BROWSER_ACTION(4u),
    TOOL_JOB(5u),
    SECURE_STORE(6u),
    OPEN_AUTH_SURFACE(7u),
    REQUEST_PERMISSION(8u),
    DELIVER_ASSET(9u),
    FETCH_CATALOG(10u),
    FETCH_PROVIDER_LISTING(11u),
    DELIVER_COMPOSER_COMPLETION(12u),
    PROBE_CUSTOM_ENDPOINT(13u);

    companion object {
        fun fromWire(value: UInt): EffectKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class CatalogNetworkOperation(val wire: UInt) {
    FETCH_PUBLISHED_CATALOG(0u);

    companion object {
        fun fromWire(value: UInt): CatalogNetworkOperation? = entries.firstOrNull { it.wire == value }
    }
}

enum class CatalogFetchDisposition(val wire: UInt) {
    SUCCESS(0u),
    NOT_MODIFIED(1u),
    UNAVAILABLE(2u),
    OVERSIZED(3u),
    MALFORMED_TRANSPORT(4u);

    companion object {
        fun fromWire(value: UInt): CatalogFetchDisposition? = entries.firstOrNull { it.wire == value }
    }
}

enum class RetryClass(val wire: UInt) {
    IDEMPOTENT(0u),
    CONSEQUENTIAL(1u),
    NEVER(2u);

    companion object {
        fun fromWire(value: UInt): RetryClass? = entries.firstOrNull { it.wire == value }
    }
}

enum class EffectStatus(val wire: UInt) {
    COMPLETED(0u),
    DENIED(1u),
    CANCELLED(2u),
    DEADLINE_EXCEEDED(3u),
    RESOURCE_LIMIT(4u),
    UNAVAILABLE(5u),
    OUTCOME_UNKNOWN(6u),
    INVALID_RESULT(7u);

    companion object {
        fun fromWire(value: UInt): EffectStatus? = entries.firstOrNull { it.wire == value }
    }
}

enum class DisclosureClass(val wire: UInt) {
    CONTENT_FREE(0u),
    ACCOUNT_METADATA(1u),
    USER_SELECTED_CONTENT(2u),
    PAGE_CONTENT(3u);

    companion object {
        fun fromWire(value: UInt): DisclosureClass? = entries.firstOrNull { it.wire == value }
    }
}

enum class InitializationStatus(val wire: UInt) {
    READY(0u),
    INVALID_BOOTSTRAP(1u),
    INCOMPATIBLE_VERSION(2u),
    RESOURCE_LIMIT(3u);

    companion object {
        fun fromWire(value: UInt): InitializationStatus? = entries.firstOrNull { it.wire == value }
    }
}

enum class AdmissionStatus(val wire: UInt) {
    ACCEPTED(0u),
    STALE_GENERATION(1u),
    STALE_REVISION(2u),
    DEADLINE_EXCEEDED(3u),
    BACKPRESSURE(4u),
    INVALID_COMMAND(5u),
    CORE_UNAVAILABLE(6u),
    DUPLICATE(7u);

    companion object {
        fun fromWire(value: UInt): AdmissionStatus? = entries.firstOrNull { it.wire == value }
    }
}

enum class TaskKind(val wire: UInt) {
    RESEARCH(0u),
    ERRAND(1u);

    companion object {
        fun fromWire(value: UInt): TaskKind? = entries.firstOrNull { it.wire == value }
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

enum class BuiltinSkillId(val wire: UInt) {
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
        fun fromWire(value: UInt): BuiltinSkillId? = entries.firstOrNull { it.wire == value }
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

enum class TaskControlMode(val wire: UInt) {
    USER(0u),
    SHARED(1u),
    ASSISTANT(2u);

    companion object {
        fun fromWire(value: UInt): TaskControlMode? = entries.firstOrNull { it.wire == value }
    }
}

enum class TaskMilestone(val wire: UInt) {
    M0(0u),
    M1(1u),
    M2(2u),
    M3(3u),
    M4(4u),
    M5(5u),
    M6(6u),
    M7(7u),
    M8(8u);

    companion object {
        fun fromWire(value: UInt): TaskMilestone? = entries.firstOrNull { it.wire == value }
    }
}

enum class TaskBudgetKind(val wire: UInt) {
    MAX_SOURCES(0u),
    MAX_WORKING_TABS(1u),
    MAX_WALL_TIME_MS(2u),
    MAX_MODEL_REQUESTS(3u),
    MAX_INPUT_UNITS(4u),
    MAX_OUTPUT_UNITS(5u),
    MAX_COST_UNITS(6u),
    MAX_NAVIGATION_DEPTH(7u),
    MAX_RETRIES_PER_STEP(8u),
    MAX_ARTIFACT_BYTES(9u);

    companion object {
        fun fromWire(value: UInt): TaskBudgetKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class UserDecisionKind(val wire: UInt) {
    ACCEPT(0u),
    DENY(1u),
    DISMISS(2u);

    companion object {
        fun fromWire(value: UInt): UserDecisionKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class CancelReason(val wire: UInt) {
    USER(0u),
    PROFILE_SHUTDOWN(1u),
    DEADLINE(2u);

    companion object {
        fun fromWire(value: UInt): CancelReason? = entries.firstOrNull { it.wire == value }
    }
}

enum class TaskSettlementKind(val wire: UInt) {
    PAUSE(0u),
    CANCEL(1u);

    companion object {
        fun fromWire(value: UInt): TaskSettlementKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class TerminalTaskKind(val wire: UInt) {
    COMPLETED(0u),
    PARTIAL(1u),
    FAILED(2u),
    CANCELLED(3u);

    companion object {
        fun fromWire(value: UInt): TerminalTaskKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class TaskReducerEffectKind(val wire: UInt) {
    REVOKE_AUTHORITY(0u),
    ASK_POLICY(1u),
    REQUEST_APPROVAL(2u),
    REQUEST_PERMISSION(3u),
    DISPATCH_ACTION(4u),
    AWAIT_IN_FLIGHT_WORK(5u),
    RECONCILE_ACTION(6u),
    RELEASE_TASK_TABS(7u),
    GENERATE_ARTIFACT(8u),
    EXPORT_ARTIFACT(9u),
    CALL_MODEL(10u),
    AWAIT_HANDOVER(11u),
    RUN_TOOL_JOB(12u),
    REQUEST_FIELD_VALUES(13u),
    PREPARE_DISCOVERY_TAB(14u),
    RUN_LIBRARY_TOOL(15u),
    RUN_MEMORY_TOOL(16u);

    companion object {
        fun fromWire(value: UInt): TaskReducerEffectKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class TaskRevocationReason(val wire: UInt) {
    USER_TOOK_OVER(0u),
    DIRECT_USER_INPUT(1u),
    TASK_CANCELLED(2u),
    POLICY_REVOKED(3u),
    TAB_CLOSED(4u);

    companion object {
        fun fromWire(value: UInt): TaskRevocationReason? = entries.firstOrNull { it.wire == value }
    }
}

enum class TaskRecoveryRule(val wire: UInt) {
    RETRY_WITHIN_EPOCH_AND_BUDGET(0u),
    RETRY_AFTER_STATE_CHECK(1u),
    RECONCILE_FIRST(2u),
    NEVER_AUTOMATICALLY(3u);

    companion object {
        fun fromWire(value: UInt): TaskRecoveryRule? = entries.firstOrNull { it.wire == value }
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

enum class TaskActionPrecondition(val wire: UInt) {
    DOCUMENT_UNCHANGED(0u),
    GRAPH_REVISION_AT_LEAST(1u),
    NODE_PRESENT(2u),
    DESTINATION_UNCHANGED(3u);

    companion object {
        fun fromWire(value: UInt): TaskActionPrecondition? = entries.firstOrNull { it.wire == value }
    }
}

enum class TaskActionPostcondition(val wire: UInt) {
    OBSERVATION_CAPTURED(0u),
    DOCUMENT_NAVIGATED(1u),
    NODE_STATE_CHANGED(2u),
    DOWNLOAD_STARTED(3u),
    PLATFORM_ACKNOWLEDGED(4u),
    TASK_TABS_LISTED(5u),
    TASK_TAB_ACTIVE(6u),
    TASK_TAB_ABSENT(7u),
    DOWNLOAD_CANCELLED(8u),
    PAGE_RELOADED(9u),
    LOADING_STOPPED(10u),
    STORE_ROWS_LISTED(11u);

    companion object {
        fun fromWire(value: UInt): TaskActionPostcondition? = entries.firstOrNull { it.wire == value }
    }
}

enum class TaskTabPostcondition(val wire: UInt) {
    LISTED(0u),
    ACTIVE(1u),
    ABSENT(2u);

    companion object {
        fun fromWire(value: UInt): TaskTabPostcondition? = entries.firstOrNull { it.wire == value }
    }
}

enum class TaskDownloadPostcondition(val wire: UInt) {
    STARTED(0u),
    LISTED(1u),
    CANCELLED(2u);

    companion object {
        fun fromWire(value: UInt): TaskDownloadPostcondition? = entries.firstOrNull { it.wire == value }
    }
}

enum class TaskDownloadState(val wire: UInt) {
    CREATED(0u),
    IN_PROGRESS(1u),
    PAUSED(2u),
    COMPLETE(3u),
    INTERRUPTED(4u),
    CANCELLED(5u);

    companion object {
        fun fromWire(value: UInt): TaskDownloadState? = entries.firstOrNull { it.wire == value }
    }
}

enum class TaskDownloadMediaType(val wire: UInt) {
    UNKNOWN(0u),
    APPLICATION(1u),
    AUDIO(2u),
    FONT(3u),
    IMAGE(4u),
    MESSAGE(5u),
    MODEL(6u),
    MULTIPART(7u),
    TEXT(8u),
    VIDEO(9u);

    companion object {
        fun fromWire(value: UInt): TaskDownloadMediaType? = entries.firstOrNull { it.wire == value }
    }
}

enum class TaskDownloadDirectoryClass(val wire: UInt) {
    UNDECIDED(0u),
    PERSON_CHOSEN(1u),
    DEFAULT_DOWNLOADS(2u),
    APPLICATION_PRIVATE(3u);

    companion object {
        fun fromWire(value: UInt): TaskDownloadDirectoryClass? = entries.firstOrNull { it.wire == value }
    }
}

enum class TaskEffectCompletionStatus(val wire: UInt) {
    SUCCEEDED(0u),
    REFUSED(1u),
    UNAVAILABLE(2u),
    CANCELLED(3u),
    OUTCOME_UNKNOWN(4u),
    VALUE_REFERENCE_UNKNOWN(5u);

    companion object {
        fun fromWire(value: UInt): TaskEffectCompletionStatus? = entries.firstOrNull { it.wire == value }
    }
}

enum class AuthCallbackStatus(val wire: UInt) {
    AUTHORIZATION_CODE(0u),
    DENIED(1u),
    PROVIDER_ERROR(2u),
    DEADLINE_EXCEEDED(3u),
    PLATFORM_UNAVAILABLE(4u);

    companion object {
        fun fromWire(value: UInt): AuthCallbackStatus? = entries.firstOrNull { it.wire == value }
    }
}

enum class AccountAuthMethod(val wire: UInt) {
    GOOGLE(0u),
    EMAIL_LINK(1u),
    GITHUB(2u),
    FACEBOOK(3u);

    companion object {
        fun fromWire(value: UInt): AccountAuthMethod? = entries.firstOrNull { it.wire == value }
    }
}

enum class AccountTokenValidationStatus(val wire: UInt) {
    VALIDATED(0u),
    INVALID_RESPONSE(1u),
    STALE_GENERATION(2u),
    DEADLINE_EXCEEDED(3u),
    RESOURCE_LIMIT(4u);

    companion object {
        fun fromWire(value: UInt): AccountTokenValidationStatus? = entries.firstOrNull { it.wire == value }
    }
}

enum class AccountScope(val wire: UInt) {
    OPEN_ID(0u),
    EMAIL(1u),
    PROFILE(2u);

    companion object {
        fun fromWire(value: UInt): AccountScope? = entries.firstOrNull { it.wire == value }
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

enum class PlatformPermission(val wire: UInt) {
    NOTIFICATIONS(0u),
    MICROPHONE(1u),
    CAMERA(2u),
    LOCATION(3u);

    companion object {
        fun fromWire(value: UInt): PlatformPermission? = entries.firstOrNull { it.wire == value }
    }
}

enum class PermissionDecision(val wire: UInt) {
    GRANTED(0u),
    DENIED(1u),
    DISMISSED(2u),
    UNAVAILABLE(3u);

    companion object {
        fun fromWire(value: UInt): PermissionDecision? = entries.firstOrNull { it.wire == value }
    }
}

enum class StorageOperation(val wire: UInt) {
    APPEND_TASK_COMMIT(0u),
    QUERY_WORKSPACE(1u),
    DELETE_SOURCE(2u),
    UPSERT_WORKSPACE(3u),
    INSTALL_SKILL(4u),
    SET_SKILL_STATUS(5u),
    RECORD_SKILL_RUN(6u),
    FORGET_SKILL(7u),
    SET_ASSISTANT_CONFIGURATION(8u),
    DELETE_WORKSPACE(9u),
    UPSERT_LIBRARY_ENTRY(10u),
    REMOVE_LIBRARY_ENTRY(11u),
    UPSERT_MEMORY(12u),
    DELETE_MEMORY(13u);

    companion object {
        fun fromWire(value: UInt): StorageOperation? = entries.firstOrNull { it.wire == value }
    }
}

enum class LibraryFactKind(val wire: UInt) {
    FROM_PAGE(0u),
    SUMMARIZED(1u),
    TAFFY_INFERENCE(2u),
    USER_ENTERED(3u);

    companion object {
        fun fromWire(value: UInt): LibraryFactKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class MemorySourceKind(val wire: UInt) {
    USER_ENTERED(0u),
    ACCEPTED_TASK_SUGGESTION(1u);

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

enum class AssistantAbility(val wire: UInt) {
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
        fun fromWire(value: UInt): AssistantAbility? = entries.firstOrNull { it.wire == value }
    }
}

enum class PersonalityPreset(val wire: UInt) {
    CAREFUL_RESEARCHER(0u),
    QUICK_SHOPPER(1u),
    TRIP_PLANNER(2u);

    companion object {
        fun fromWire(value: UInt): PersonalityPreset? = entries.firstOrNull { it.wire == value }
    }
}

enum class SkillProvenance(val wire: UInt) {
    AUTHORED(0u),
    RECORDED_FROM_TASK(1u),
    INSTALLED_FROM_PACK(2u);

    companion object {
        fun fromWire(value: UInt): SkillProvenance? = entries.firstOrNull { it.wire == value }
    }
}

enum class SkillMutationKind(val wire: UInt) {
    TEACH(0u),
    UPDATE(1u),
    SET_ENABLED(2u),
    REMOVE(3u);

    companion object {
        fun fromWire(value: UInt): SkillMutationKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class SkillClauseKind(val wire: UInt) {
    ROLE_PRESENT(0u),
    PHRASE_AT(1u),
    STATE_AT(2u);

    companion object {
        fun fromWire(value: UInt): SkillClauseKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class SkillArgumentKind(val wire: UInt) {
    FROM_EARLIER_STEP(0u),
    FROM_PERSON(1u),
    CHOICE(2u),
    COUNT(3u),
    FLAG(4u),
    PUBLIC_ADDRESS(5u),
    SEMANTIC_TARGET(6u);

    companion object {
        fun fromWire(value: UInt): SkillArgumentKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class SkillStatus(val wire: UInt) {
    DRAFT(0u),
    ACTIVE(1u),
    SUPERSEDED(2u),
    RETIRED(3u),
    DISABLED(4u);

    companion object {
        fun fromWire(value: UInt): SkillStatus? = entries.firstOrNull { it.wire == value }
    }
}

enum class SkillRunOutcome(val wire: UInt) {
    COMPLETED(0u),
    REFUSED(1u),
    ABANDONED(2u),
    UNAVAILABLE(3u);

    companion object {
        fun fromWire(value: UInt): SkillRunOutcome? = entries.firstOrNull { it.wire == value }
    }
}

enum class WorkspaceExportFormat(val wire: UInt) {
    MARKDOWN(0u),
    CSV(1u);

    companion object {
        fun fromWire(value: UInt): WorkspaceExportFormat? = entries.firstOrNull { it.wire == value }
    }
}

enum class PageSnapshotExportFormat(val wire: UInt) {
    MARKDOWN(0u),
    CANONICAL_JSON(1u);

    companion object {
        fun fromWire(value: UInt): PageSnapshotExportFormat? = entries.firstOrNull { it.wire == value }
    }
}

enum class PageSnapshotExportStatus(val wire: UInt) {
    EXPORTED(0u),
    STALE_PAGE(1u),
    PRIVATE_PROFILE(2u),
    INCOMPLETE(3u),
    OVERSIZE(4u),
    MALFORMED(5u),
    CANCELLED(6u),
    REPLAY_CONFLICT(7u),
    UNAVAILABLE(8u);

    companion object {
        fun fromWire(value: UInt): PageSnapshotExportStatus? = entries.firstOrNull { it.wire == value }
    }
}

enum class SiteSkillMatchStatus(val wire: UInt) {
    AVAILABLE(0u),
    STALE_PAGE(1u),
    PRIVATE_PROFILE(2u),
    INCOMPLETE(3u),
    MALFORMED(4u),
    UNAVAILABLE(5u);

    companion object {
        fun fromWire(value: UInt): SiteSkillMatchStatus? = entries.firstOrNull { it.wire == value }
    }
}

enum class ObservationScope(val wire: UInt) {
    CURRENT_DOCUMENT(0u),
    SELECTED_SOURCES(1u);

    companion object {
        fun fromWire(value: UInt): ObservationScope? = entries.firstOrNull { it.wire == value }
    }
}

enum class BipObservationStatus(val wire: UInt) {
    OK(0u),
    UNSUPPORTED(1u),
    INCOMPLETE(2u),
    CONFLICTED(3u),
    STALE_PAGE_EPOCH(4u),
    DOCUMENT_INACTIVE(5u),
    BUDGET_EXCEEDED(6u),
    DEADLINE_EXCEEDED(7u),
    CANCELLED(8u),
    RESOURCE_PRESSURE(9u),
    INTERNAL_ERROR(10u);

    companion object {
        fun fromWire(value: UInt): BipObservationStatus? = entries.firstOrNull { it.wire == value }
    }
}

enum class BipGraphEncoding(val wire: UInt) {
    NONE(0u),
    BIP_CONTRACT(1u);

    companion object {
        fun fromWire(value: UInt): BipGraphEncoding? = entries.firstOrNull { it.wire == value }
    }
}

enum class MediaObservationKind(val wire: UInt) {
    IMAGE(0u),
    VIDEO(1u),
    PDF(2u),
    PAGE_SCREENSHOT(3u);

    companion object {
        fun fromWire(value: UInt): MediaObservationKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class MediaFactKind(val wire: UInt) {
    DESCRIPTION(0u),
    OCR_TEXT(1u),
    TRANSCRIPT(2u),
    PDF_TEXT(3u),
    PDF_TABLE_ROW(4u),
    METADATA(5u);

    companion object {
        fun fromWire(value: UInt): MediaFactKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class MediaEvidenceKind(val wire: UInt) {
    DOM(0u),
    ACCESSIBILITY(1u),
    CAPTION_TRACK(2u),
    PDF_TEXT_LAYER(3u),
    VISUAL_INFERENCE(4u),
    TABLE_HEURISTIC(5u);

    companion object {
        fun fromWire(value: UInt): MediaEvidenceKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class BipSensitivity(val wire: UInt) {
    NOT_SENSITIVE(0u),
    PERSONAL(1u),
    ACCOUNT(2u),
    PAYMENT(3u),
    IDENTITY(4u),
    HEALTH(5u),
    FINANCIAL(6u),
    LEGAL(7u),
    PRIVATE_COMMUNICATION(8u),
    ADMINISTRATION(9u),
    CREDENTIAL(10u),
    UNKNOWN_SENSITIVE(11u),
    ONE_TIME_CODE(12u),
    CHALLENGE_RESPONSE(13u);

    companion object {
        fun fromWire(value: UInt): BipSensitivity? = entries.firstOrNull { it.wire == value }
    }
}

enum class AccountNetworkOperation(val wire: UInt) {
    EXCHANGE_AUTHORIZATION_CODE(0u),
    EXCHANGE_NATIVE_CREDENTIAL(1u),
    REQUEST_EMAIL_LINK(2u),
    REFRESH_SESSION(3u),
    REVOKE_SESSION(4u),
    FETCH_ENTITLEMENT(5u);

    companion object {
        fun fromWire(value: UInt): AccountNetworkOperation? = entries.firstOrNull { it.wire == value }
    }
}

enum class EntitlementFetchReason(val wire: UInt) {
    BOOTSTRAP(0u),
    SIGN_IN(1u),
    CADENCE(2u),
    QUOTA_REFUSED(3u);

    companion object {
        fun fromWire(value: UInt): EntitlementFetchReason? = entries.firstOrNull { it.wire == value }
    }
}

enum class BrowserActionOperation(val wire: UInt) {
    DISPATCH(0u),
    RECONCILE(1u),
    RELEASE_TASK_TABS(2u),
    EXPORT_ARTIFACT(3u);

    companion object {
        fun fromWire(value: UInt): BrowserActionOperation? = entries.firstOrNull { it.wire == value }
    }
}

enum class ToolRuntimeKind(val wire: UInt) {
    PYTHON(0u),
    LOCAL_MODEL(1u),
    MEDIA(2u),
    WASM(3u);

    companion object {
        fun fromWire(value: UInt): ToolRuntimeKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class ToolOperation(val wire: UInt) {
    RUN_BUNDLED_PYTHON_MODULE(0u),
    GENERATE_LOCAL_MODEL(1u),
    PROBE_MEDIA(2u),
    EXTRACT_AUDIO(3u),
    SAMPLE_FRAMES(4u),
    TRANSCODE_PRESET(5u),
    RUN_SIGNED_WASM_TRANSFORM(6u),
    EMBED_LOCAL_MODEL(7u);

    companion object {
        fun fromWire(value: UInt): ToolOperation? = entries.firstOrNull { it.wire == value }
    }
}

enum class ToolModelArtifactKind(val wire: UInt) {
    LITERT_TFLITE(0u),
    ONNX_RUNTIME(1u),
    GGUF(2u);

    companion object {
        fun fromWire(value: UInt): ToolModelArtifactKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class EmbeddingElementKind(val wire: UInt) {
    FLOAT32_LE(0u);

    companion object {
        fun fromWire(value: UInt): EmbeddingElementKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class ToolChunkKind(val wire: UInt) {
    TEXT_UTF8(0u),
    BINARY(1u);

    companion object {
        fun fromWire(value: UInt): ToolChunkKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class LocalModelFinishReason(val wire: UInt) {
    STOP(0u),
    TOKEN_LIMIT(1u);

    companion object {
        fun fromWire(value: UInt): LocalModelFinishReason? = entries.firstOrNull { it.wire == value }
    }
}

enum class ToolTerminalStatus(val wire: UInt) {
    COMPLETED(0u),
    CANCELLED(1u),
    DEADLINE_EXCEEDED(2u),
    RESOURCE_LIMIT(3u),
    RUNTIME_CRASHED(4u),
    INVALID_INPUT(5u),
    UNSUPPORTED(6u),
    OUTCOME_UNKNOWN(7u),
    MODEL_ARTIFACT_MISSING(8u),
    MODEL_ARTIFACT_INCOMPATIBLE(9u),
    LOCAL_RUNTIME_UNAVAILABLE(10u);

    companion object {
        fun fromWire(value: UInt): ToolTerminalStatus? = entries.firstOrNull { it.wire == value }
    }
}

enum class SecureStoreOperation(val wire: UInt) {
    GENERATE_ENTROPY(0u),
    WRITE_TRANSIENT(1u),
    DELETE_HANDLE(2u);

    companion object {
        fun fromWire(value: UInt): SecureStoreOperation? = entries.firstOrNull { it.wire == value }
    }
}

enum class SecretMaterialPurpose(val wire: UInt) {
    PKCE_VERIFIER(0u),
    GOOGLE_RAW_NONCE(1u);

    companion object {
        fun fromWire(value: UInt): SecretMaterialPurpose? = entries.firstOrNull { it.wire == value }
    }
}

enum class AuthSurfaceOperation(val wire: UInt) {
    OPEN_OAUTH(0u),
    REQUEST_NATIVE_CREDENTIAL(1u);

    companion object {
        fun fromWire(value: UInt): AuthSurfaceOperation? = entries.firstOrNull { it.wire == value }
    }
}

enum class BrowserActionOutcome(val wire: UInt) {
    COMPLETED(0u),
    REFUSED(1u),
    OUTCOME_UNKNOWN(2u);

    companion object {
        fun fromWire(value: UInt): BrowserActionOutcome? = entries.firstOrNull { it.wire == value }
    }
}

enum class CapabilityRegistrationStatus(val wire: UInt) {
    REGISTERED(0u),
    DUPLICATE(1u),
    STALE_GENERATION(2u),
    LEASE_MISSING(3u),
    INVALID_GRANT(4u);

    companion object {
        fun fromWire(value: UInt): CapabilityRegistrationStatus? = entries.firstOrNull { it.wire == value }
    }
}

enum class PendingApprovalRegistrationStatus(val wire: UInt) {
    REGISTERED(0u),
    STALE_GENERATION(1u),
    STALE_SEQUENCE(2u),
    INVALID_BINDING(3u),
    TOO_MANY_BINDINGS(4u);

    companion object {
        fun fromWire(value: UInt): PendingApprovalRegistrationStatus? = entries.firstOrNull { it.wire == value }
    }
}

enum class PolicyActionClass(val wire: UInt) {
    OBSERVE_PAGE(0u),
    SCROLL_INTO_VIEW(1u),
    OPEN_LINK(2u),
    CREATE_TASK_TAB(3u),
    SYNTHETIC_CLICK(4u),
    MOVE_FOCUS(5u),
    FILL_FIELD(6u),
    SELECT_OPTION(7u),
    TOGGLE_CONTROL(8u),
    SUBMIT_FORM(9u),
    START_DOWNLOAD(10u),
    UPLOAD_FILE(11u),
    SEND_MESSAGE(12u),
    PURCHASE(13u),
    EXTRACT_CREDENTIAL(14u),
    BYPASS_ACCESS_CONTROL(15u),
    EXECUTE_TOOL_JOB(16u),
    LIBRARY_READ(17u),
    LIBRARY_WRITE(18u),
    MEMORY_READ(19u),
    MEMORY_WRITE(20u),
    CONTROL_TAB(21u),
    PROFILE_STORE_READ(22u);

    companion object {
        fun fromWire(value: UInt): PolicyActionClass? = entries.firstOrNull { it.wire == value }
    }
}

enum class TaskActionOperationKind(val wire: UInt) {
    NAVIGATE(0u),
    SEARCH(1u),
    HISTORY_BACK(2u),
    HISTORY_FORWARD(3u),
    TABS_OPEN(4u),
    TABS_LIST(5u),
    TABS_ACTIVATE(6u),
    TABS_CLOSE(7u),
    DOM_QUERY(8u),
    DOM_READ(9u),
    DOM_CLICK(10u),
    DOM_SCROLL(11u),
    FORM_INSPECT(12u),
    FORM_FILL(13u),
    FORM_SUBMIT(14u),
    DOWNLOAD_START(15u),
    DOWNLOAD_LIST(16u),
    SELECTION_READ(17u),
    IMAGE_DESCRIBE(18u),
    IMAGE_READ_TEXT(19u),
    VIDEO_INSPECT(20u),
    PDF_INSPECT(21u),
    TOOL_JOB(22u),
    LINK_OPEN(23u),
    FORM_SELECT(24u),
    FORM_TOGGLE(25u),
    LIBRARY_SEARCH(26u),
    LIBRARY_SAVE(27u),
    LIBRARY_REMOVE(28u),
    MEMORY_SEARCH(29u),
    MEMORY_SAVE(30u),
    MEMORY_UPDATE(31u),
    MEMORY_DELETE(32u),
    DOM_FOCUS(33u),
    PAGE_SCREENSHOT_INSPECT(34u),
    DOWNLOAD_CANCEL(35u),
    RELOAD(36u),
    STOP_LOADING(37u),
    HISTORY_SEARCH(38u),
    HISTORY_RECENT(39u),
    BOOKMARKS_SEARCH(40u),
    BOOKMARKS_LIST(41u),
    OPEN_TABS_LIST(42u);

    companion object {
        fun fromWire(value: UInt): TaskActionOperationKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class TaskActionInputKind(val wire: UInt) {
    NONE(0u),
    SUPPLIED_VALUE(1u),
    TOGGLE_STATE(2u);

    companion object {
        fun fromWire(value: UInt): TaskActionInputKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class PolicyPrincipalKind(val wire: UInt) {
    ASSISTANT(0u),
    SKILL(1u);

    companion object {
        fun fromWire(value: UInt): PolicyPrincipalKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class PolicyEvaluationContext(val wire: UInt) {
    TASK(0u),
    DIRECT_USER_OBSERVATION(1u),
    TASK_DISCOVERY(2u);

    companion object {
        fun fromWire(value: UInt): PolicyEvaluationContext? = entries.firstOrNull { it.wire == value }
    }
}

enum class AuthoritySubjectKind(val wire: UInt) {
    TASK(0u),
    DIRECT_USER_INTENT(1u);

    companion object {
        fun fromWire(value: UInt): AuthoritySubjectKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class PolicyOriginKind(val wire: UInt) {
    TUPLE(0u),
    OPAQUE(1u);

    companion object {
        fun fromWire(value: UInt): PolicyOriginKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class PolicyRiskClass(val wire: UInt) {
    LOCAL_READ(0u),
    REVERSIBLE_DISCLOSURE(1u),
    SENSITIVE_DISCLOSURE(2u),
    EXCLUDED_COMMITMENT(3u),
    PROHIBITED_ABUSE(4u);

    companion object {
        fun fromWire(value: UInt): PolicyRiskClass? = entries.firstOrNull { it.wire == value }
    }
}

enum class PolicyEvaluationStatus(val wire: UInt) {
    GRANTED(0u),
    APPROVAL_REQUIRED(1u),
    DENIED(2u),
    INVALID_REQUEST(3u),
    CORE_UNAVAILABLE(4u);

    companion object {
        fun fromWire(value: UInt): PolicyEvaluationStatus? = entries.firstOrNull { it.wire == value }
    }
}

enum class TaskActionResultCode(val wire: UInt) {
    VERIFIED(0u),
    DENIED_BY_POLICY(1u),
    APPROVAL_REQUIRED(2u),
    APPROVAL_DENIED(3u),
    ACTOR_LEASE_MISSING(4u),
    CAPABILITY_EXPIRED(5u),
    TAB_GONE(6u),
    FRAME_GONE(7u),
    DOCUMENT_INACTIVE(8u),
    STALE_PAGE_EPOCH(9u),
    STALE_GRAPH(10u),
    NODE_GONE(11u),
    ORIGIN_CHANGED(12u),
    ROLE_OR_ACTION_CHANGED(13u),
    NOT_VISIBLE(14u),
    OCCLUDED(15u),
    NOT_ENABLED(16u),
    NOT_EDITABLE(17u),
    SENSITIVE_FIELD(18u),
    DESTINATION_CHANGED(19u),
    UNSUPPORTED(20u),
    BUDGET_EXCEEDED(21u),
    DISPATCH_FAILED(22u),
    NAVIGATION_STARTED(23u),
    POSTCONDITION_TIMEOUT(24u),
    POSTCONDITION_FAILED(25u),
    CANCELLED_BY_USER(26u),
    CANCELLED_BY_NAVIGATION(27u),
    RENDERER_CRASHED(28u),
    OUTCOME_UNKNOWN(29u),
    INTERNAL_ERROR(30u),
    EGRESS_NOT_AUTHORIZED(31u),
    DESTINATION_CLASS_RESTRICTED(32u),
    UNTRUSTED_CONTENT_ORIGIN(33u),
    PREPARED_EFFECT_CHANGED(34u),
    COMMIT_WITHOUT_PREPARE(35u),
    GRAPH_MOVED_DURING_PREFLIGHT(36u),
    VALUE_REFERENCE_UNKNOWN(37u);

    companion object {
        fun fromWire(value: UInt): TaskActionResultCode? = entries.firstOrNull { it.wire == value }
    }
}

enum class AssetDeliveryOperation(val wire: UInt) {
    FETCH_ASSET(0u),
    REMOVE_ASSET(1u);

    companion object {
        fun fromWire(value: UInt): AssetDeliveryOperation? = entries.firstOrNull { it.wire == value }
    }
}

enum class AssetPlatform(val wire: UInt) {
    ANDROID_ARM64(0u),
    ANDROID_X64(1u),
    MACOS_ARM64(2u),
    MACOS_X64(3u),
    WINDOWS_X64(4u),
    WINDOWS_ARM64(5u),
    UNSUPPORTED(6u);

    companion object {
        fun fromWire(value: UInt): AssetPlatform? = entries.firstOrNull { it.wire == value }
    }
}

enum class AssetKind(val wire: UInt) {
    PYTHON_STDLIB(0u),
    PYTHON_PACKAGES(1u),
    MODEL_WEIGHTS(2u),
    MODEL_TOKENIZER(3u),
    FILTER_LIST(4u),
    COUNTRY_FLAGS(5u),
    START_SCENES(6u);

    companion object {
        fun fromWire(value: UInt): AssetKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class AssetContainer(val wire: UInt) {
    RAW(0u),
    ZIP(1u);

    companion object {
        fun fromWire(value: UInt): AssetContainer? = entries.firstOrNull { it.wire == value }
    }
}

enum class AssetPresence(val wire: UInt) {
    ABSENT(0u),
    PARTIAL(1u),
    COMPLETE(2u),
    INSTALLED(3u);

    companion object {
        fun fromWire(value: UInt): AssetPresence? = entries.firstOrNull { it.wire == value }
    }
}

enum class AssetRefusalReason(val wire: UInt) {
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
        fun fromWire(value: UInt): AssetRefusalReason? = entries.firstOrNull { it.wire == value }
    }
}

enum class AssetTransferOutcome(val wire: UInt) {
    INTERRUPTED(0u),
    ORIGIN_REFUSED_TEMPORARY(1u),
    ORIGIN_REFUSED_PERMANENT(2u),
    INTEGRITY_SOUND(3u),
    INTEGRITY_WRONG_LENGTH(4u),
    INTEGRITY_WRONG_DIGEST(5u),
    INSTALLED(6u),
    DECLINED(7u);

    companion object {
        fun fromWire(value: UInt): AssetTransferOutcome? = entries.firstOrNull { it.wire == value }
    }
}

enum class AssetNetworkCost(val wire: UInt) {
    OFFLINE(0u),
    METERED(1u),
    UNMETERED(2u);

    companion object {
        fun fromWire(value: UInt): AssetNetworkCost? = entries.firstOrNull { it.wire == value }
    }
}

enum class ProviderAuthMethod(val wire: UInt) {
    API_KEY(0u),
    OAUTH(1u);

    companion object {
        fun fromWire(value: UInt): ProviderAuthMethod? = entries.firstOrNull { it.wire == value }
    }
}

enum class ThinkingLevel(val wire: UInt) {
    OFF(0u),
    MINIMAL(1u),
    LOW(2u),
    MEDIUM(3u),
    HIGH(4u),
    XHIGH(5u),
    MAX(6u);

    companion object {
        fun fromWire(value: UInt): ThinkingLevel? = entries.firstOrNull { it.wire == value }
    }
}

enum class ProviderCredentialState(val wire: UInt) {
    USABLE(0u),
    NEEDS_SIGN_IN(1u),
    REFRESH_FAILED(2u);

    companion object {
        fun fromWire(value: UInt): ProviderCredentialState? = entries.firstOrNull { it.wire == value }
    }
}

enum class ProviderWireApi(val wire: UInt) {
    ANTHROPIC_MESSAGES(0u),
    OPEN_AI_RESPONSES(1u),
    OPEN_AI_COMPLETIONS(2u),
    GOOGLE_GENERATIVE_LANGUAGE(3u),
    MANAGED(4u),
    OPEN_AI_CODEX_RESPONSES(5u),
    GOOGLE_CLOUD_CODE_ASSIST(6u);

    companion object {
        fun fromWire(value: UInt): ProviderWireApi? = entries.firstOrNull { it.wire == value }
    }
}

enum class ModelErrorClass(val wire: UInt) {
    AUTH(0u),
    QUOTA(1u),
    OVERLOADED(2u),
    INVALID_REQUEST(3u),
    NETWORK(4u),
    OVERFLOW(5u),
    CANCELED(6u),
    UNKNOWN(7u);

    companion object {
        fun fromWire(value: UInt): ModelErrorClass? = entries.firstOrNull { it.wire == value }
    }
}

enum class ModelEndpointKind(val wire: UInt) {
    CATALOG_ORIGIN(0u),
    USER_BASE_URL(1u);

    companion object {
        fun fromWire(value: UInt): ModelEndpointKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class ServerKind(val wire: UInt) {
    OPENAI_COMPATIBLE(0u),
    OLLAMA(1u),
    LM_STUDIO(2u),
    VLLM(3u),
    LLAMA_CPP(4u);

    companion object {
        fun fromWire(value: UInt): ServerKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class ModelStreamChunkStatus(val wire: UInt) {
    ACCEPTED(0u),
    INVALID(1u),
    STALE(2u),
    UNAVAILABLE(3u);

    companion object {
        fun fromWire(value: UInt): ModelStreamChunkStatus? = entries.firstOrNull { it.wire == value }
    }
}

enum class BackupRecordKind(val wire: UInt) {
    ASSISTANT_CONFIGURATION(0u),
    SAVED_WORKSPACE(1u),
    LIBRARY_ENTRY(2u),
    MEMORY_RECORD(3u),
    USER_AUTHORED_SKILL(4u),
    LEARNED_PROCEDURE(5u),
    BOOKMARK(6u),
    BROWSER_PREFERENCE(7u);

    companion object {
        fun fromWire(value: UInt): BackupRecordKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class BackupRecordState(val wire: UInt) {
    ACTIVE(0u),
    TOMBSTONE(1u);

    companion object {
        fun fromWire(value: UInt): BackupRecordState? = entries.firstOrNull { it.wire == value }
    }
}

enum class BackupPlanningStatus(val wire: UInt) {
    SUCCEEDED(0u),
    INVALID_REQUEST(1u),
    INVALID_MANIFEST(2u),
    SNAPSHOT_MISMATCH(3u),
    STAGED_PAYLOAD_MISMATCH(4u),
    RESTORE_CONFLICT(5u),
    DIGEST_UNAVAILABLE(6u),
    UNAVAILABLE(7u);

    companion object {
        fun fromWire(value: UInt): BackupPlanningStatus? = entries.firstOrNull { it.wire == value }
    }
}

enum class BackupRestoreTargetKind(val wire: UInt) {
    NEW_REGULAR_PROFILE(0u),
    EXISTING_REGULAR_PROFILE(1u);

    companion object {
        fun fromWire(value: UInt): BackupRestoreTargetKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class BackupRestoreAction(val wire: UInt) {
    STAGE_CREATE(0u),
    STAGE_DELETION(1u),
    ALREADY_PRESENT(2u),
    KEEP_NEWER_CURRENT(3u),
    BLOCKED_BY_DELETION(4u),
    NEEDS_EXPLICIT_CONFLICT_CHOICE(5u);

    companion object {
        fun fromWire(value: UInt): BackupRestoreAction? = entries.firstOrNull { it.wire == value }
    }
}

enum class BackupRestoreProtocolStatus(val wire: UInt) {
    SUCCEEDED(0u),
    INVALID_OPERATION(1u),
    UNAVAILABLE(2u),
    BINDING_MISMATCH(3u),
    WRONG_PHASE(4u),
    CONFIRMATION_MISMATCH(5u),
    SNAPSHOT_MISMATCH(6u),
    RECONCILE_REQUIRED(7u);

    companion object {
        fun fromWire(value: UInt): BackupRestoreProtocolStatus? = entries.firstOrNull { it.wire == value }
    }
}

enum class BackupRestoreCommitOutcome(val wire: UInt) {
    COMMITTED(0u),
    DEFINITELY_NOT_COMMITTED(1u),
    OUTCOME_UNKNOWN(2u);

    companion object {
        fun fromWire(value: UInt): BackupRestoreCommitOutcome? = entries.firstOrNull { it.wire == value }
    }
}

enum class BackupRestoreResolutionChoice(val wire: UInt) {
    ACCEPT_CANDIDATE(0u),
    DISCARD_CANDIDATE(1u);

    companion object {
        fun fromWire(value: UInt): BackupRestoreResolutionChoice? = entries.firstOrNull { it.wire == value }
    }
}

enum class BackupRestoreResolutionOutcome(val wire: UInt) {
    COMPLETED(0u),
    DEFINITELY_NOT_COMPLETED(1u),
    OUTCOME_UNKNOWN(2u);

    companion object {
        fun fromWire(value: UInt): BackupRestoreResolutionOutcome? = entries.firstOrNull { it.wire == value }
    }
}

enum class BackupRestoreRecoveryFactKind(val wire: UInt) {
    INTENT_RECORDED(0u),
    OUTCOME_OBSERVED(1u);

    companion object {
        fun fromWire(value: UInt): BackupRestoreRecoveryFactKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class BackupRestorePhysicalIntent(val wire: UInt) {
    COMMIT_CANDIDATE(0u),
    ACCEPT_CANDIDATE(1u),
    DISCARD_CANDIDATE(2u);

    companion object {
        fun fromWire(value: UInt): BackupRestorePhysicalIntent? = entries.firstOrNull { it.wire == value }
    }
}

enum class BackupRestoreObservedOutcome(val wire: UInt) {
    COMPLETED(0u),
    DEFINITELY_NOT_COMPLETED(1u),
    OUTCOME_UNKNOWN(2u);

    companion object {
        fun fromWire(value: UInt): BackupRestoreObservedOutcome? = entries.firstOrNull { it.wire == value }
    }
}

enum class BackupRestoreRecoveryClassificationKind(val wire: UInt) {
    RECONCILE_REQUIRED(0u),
    ROLLBACK_AVAILABLE(1u),
    CLEANUP_REQUIRED(2u),
    PUBLISHED(3u),
    VERIFIED_DELETED(4u);

    companion object {
        fun fromWire(value: UInt): BackupRestoreRecoveryClassificationKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class BackupRestoreRecoveryError(val wire: UInt) {
    MISSING_COMMIT_INTENT(0u),
    TOO_MANY_RECORDS(1u),
    UNSUPPORTED_VERSION(2u),
    INVALID_SEQUENCE(3u),
    INVALID_BINDING(4u),
    BINDING_CHANGED(5u),
    INVALID_INTENT_ID(6u),
    INTENT_ID_REUSED(7u),
    UNEXPECTED_INTENT(8u),
    UNRESOLVED_INTENT(9u),
    OUTCOME_WITHOUT_INTENT(10u),
    OUTCOME_INTENT_MISMATCH(11u),
    DUPLICATE_UNKNOWN_OUTCOME(12u),
    TERMINAL_HISTORY_EXTENDED(13u);

    companion object {
        fun fromWire(value: UInt): BackupRestoreRecoveryError? = entries.firstOrNull { it.wire == value }
    }
}

enum class BackupRestoreRecoveryInspectionStatus(val wire: UInt) {
    SUCCEEDED(0u),
    INVALID_OPERATION(1u),
    INVALID_RECORD(2u),
    INVALID_HISTORY(3u),
    UNAVAILABLE(4u);

    companion object {
        fun fromWire(value: UInt): BackupRestoreRecoveryInspectionStatus? = entries.firstOrNull { it.wire == value }
    }
}

enum class SavedFlowQueryStatus(val wire: UInt) {
    AVAILABLE(0u),
    PRIVATE_PROFILE(1u),
    UNAVAILABLE(2u),
    INVALID_REQUEST(3u),
    STALE_REQUEST(4u);

    companion object {
        fun fromWire(value: UInt): SavedFlowQueryStatus? = entries.firstOrNull { it.wire == value }
    }
}

enum class SavedFlowQueryKind(val wire: UInt) {
    EXACT_GOAL(0u),
    REVIEW(1u),
    PUBLIC_START(2u);

    companion object {
        fun fromWire(value: UInt): SavedFlowQueryKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class FieldValueAskOutcome(val wire: UInt) {
    ANSWERED(0u),
    DISMISSED(1u),
    NOT_A_FIELD(2u),
    CHALLENGE_OFF_SCREEN(3u),
    CANNOT_BE_SHOWN(4u),
    PAGE_MOVED(5u),
    NO_SURFACE(6u);

    companion object {
        fun fromWire(value: UInt): FieldValueAskOutcome? = entries.firstOrNull { it.wire == value }
    }
}

data class OperationEnvelope(
    val operation_id: String,
    val service_generation: ULong,
    val task_revision: ULong,
    val deadline_monotonic_ms: ULong,
    val idempotency_key: String
)

data class PolicyPrincipal(
    val kind: PolicyPrincipalKind,
    val skill_version_id: String?
)

data class AuthoritySubject(
    val kind: AuthoritySubjectKind,
    val authority_subject_id: String
)

data class PolicyOrigin(
    val kind: PolicyOriginKind,
    val serialization: String?,
    val opaque_id: String?
)

data class PolicyCapabilityScope(
    val profile_id: String,
    val tab_id: String,
    val frame_id: String,
    val page_epoch: String,
    val origin: PolicyOrigin,
    val node_id: String?,
    val destination_scope: PolicyOrigin?,
    val required_graph_revision: ULong,
    val allowed_redirects: List<PolicyOrigin>,
    val destination_address: String?
)

data class PolicyApprovalFact(
    val receipt_reference: String,
    val proposal_digest: String,
    val service_generation: ULong,
    val expires_at_monotonic_ms: ULong,
    val expires_at_utc_ms: ULong,
    val browser_session_id: String
)

data class ActorLeaseFact(
    val lease_id: String,
    val service_generation: ULong,
    val task_id: String,
    val profile_id: String,
    val tab_id: String,
    val control_mode: TaskControlMode,
    val expires_at_monotonic_ms: ULong,
    val authority_subject: AuthoritySubject
)

data class TaskDiscoveryAuthorityFact(
    val discovery_tab_id: String,
    val browser_session_id: String,
    val remaining_new_source_cap: UInt
)

data class PolicyEvaluationRequest(
    val operation: OperationEnvelope,
    val now_monotonic_ms: ULong,
    val action_id: String,
    val task_id: String,
    val principal: PolicyPrincipal,
    val action_class: PolicyActionClass,
    val proposal_digest: String,
    val scope: PolicyCapabilityScope,
    val data_classes: List<BipSensitivity>,
    val context_risk: PolicyRiskClass,
    val expires_at_monotonic_ms: ULong,
    val actor_lease: ActorLeaseFact,
    val approval: PolicyApprovalFact?,
    val context: PolicyEvaluationContext,
    val authority_subject: AuthoritySubject,
    val policy_version: UInt,
    val now_utc_ms: ULong,
    val operation_kind: TaskActionOperationKind,
    val canonical_intent_digest: ByteArray,
    val discovery: TaskDiscoveryAuthorityFact?
)

data class PolicyEvaluationResult(
    val operation_id: String,
    val status: PolicyEvaluationStatus,
    val minted_grant: MintedCapabilityGrant?,
    val direct_observation_effect: EffectEnvelope?,
    val denial: PolicyDenial?
)

fun PolicyEvaluationResult.hasValidPresence(): Boolean =
    (if (status == PolicyEvaluationStatus.GRANTED) minted_grant != null else minted_grant == null) &&
        (if (status == PolicyEvaluationStatus.DENIED) denial != null else denial == null)

data class PolicyDenial(
    val code: TaskActionResultCode
)

data class MintedCapabilityGrant(
    val capability_id: String,
    val service_generation: ULong,
    val policy_version: UInt,
    val actor_lease_id: String,
    val task_id: String,
    val action_id: String,
    val action_class: PolicyActionClass,
    val principal: PolicyPrincipal,
    val proposal_digest: String,
    val idempotency_key: String,
    val scope: PolicyCapabilityScope,
    val data_classes: List<BipSensitivity>,
    val effective_risk: PolicyRiskClass,
    val approval: PolicyApprovalFact?,
    val issued_at_monotonic_ms: ULong,
    val expires_at_monotonic_ms: ULong,
    val authority_subject: AuthoritySubject,
    val operation_kind: TaskActionOperationKind,
    val canonical_intent_digest: ByteArray,
    val discovery: TaskDiscoveryAuthorityFact?
)

data class CommittedTaskBatch(
    val effect_id: String,
    val expected_revision: ULong,
    val resulting_revision: ULong,
    val transaction_batch: ByteArray
)

data class TaskRestoreRecord(
    val task_id: String,
    val batches: List<CommittedTaskBatch>,
    val task_id_seed: ByteArray
)

data class AccountSessionHandle(
    val session_handle: String,
    val account_subject: String,
    val expires_at_monotonic_ms: ULong,
    val rotation: ULong,
    val auth_method: AccountAuthMethod,
    val email: String?,
    val display_name: String?
)

data class WorkspaceRestoreRecord(
    val workspace_id: String,
    val revision: ULong,
    val snapshot: ByteArray
)

data class LibrarySourceRecord(
    val source_id: String,
    val title: String,
    val host: String,
    val observed_at_epoch_ms: ULong
)

data class LibraryEntryRecord(
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
    val kind: LibraryFactKind,
    val sources: List<LibrarySourceRecord>,
    val captured_at_epoch_ms: ULong,
    val last_checked_epoch_ms: ULong,
    val has_conflict: Boolean
)

data class MemoryWorkspaceRecord(
    val workspace_id: String,
    val display_name: String
)

data class MemoryRecord(
    val memory_id: String,
    val revision: ULong,
    val statement: String,
    val source_kind: MemorySourceKind,
    val source_task_id: String?,
    val source_workspace: MemoryWorkspaceRecord?,
    val scope_kind: MemoryScopeKind,
    val scope_workspace: MemoryWorkspaceRecord?,
    val sensitivity: MemorySensitivity,
    val created_at_epoch_ms: ULong,
    val updated_at_epoch_ms: ULong,
    val reviewed_at_epoch_ms: ULong,
    val expires_at_epoch_ms: ULong
)

data class AssetFetchRequest(
    val asset_id: String,
    val asset_revision: String,
    val origin_path: String,
    val offset_bytes: ULong,
    val total_bytes: ULong,
    val expected_digest: ByteArray,
    val container: AssetContainer
)

data class AssetRemoveRequest(
    val asset_id: String,
    val asset_revision: String
)

data class AssetDeliveryEffect(
    val operation_kind: AssetDeliveryOperation,
    val fetch: AssetFetchRequest?,
    val remove: AssetRemoveRequest?
) {
    fun hasValidBody(): Boolean {
        val bodyCount = listOf(fetch, remove).count { it != null }
        if (bodyCount != 1) return false
        return when (operation_kind) {
            AssetDeliveryOperation.FETCH_ASSET -> fetch != null
            AssetDeliveryOperation.REMOVE_ASSET -> remove != null
        }
    }

    companion object {
        fun fetch(body: AssetFetchRequest): AssetDeliveryEffect =
            AssetDeliveryEffect(
                operation_kind = AssetDeliveryOperation.FETCH_ASSET,
                fetch = body,
                remove = null,
            )

        fun remove(body: AssetRemoveRequest): AssetDeliveryEffect =
            AssetDeliveryEffect(
                operation_kind = AssetDeliveryOperation.REMOVE_ASSET,
                fetch = null,
                remove = body,
            )

    }
}

data class AssetTransferReport(
    val asset_id: String,
    val asset_revision: String,
    val outcome: AssetTransferOutcome,
    val written_bytes: ULong,
    val observed_bytes: ULong,
    val observed_digest: ByteArray
)

data class AssetRemovalReport(
    val asset_id: String,
    val asset_revision: String,
    val reclaimed_bytes: ULong
)

data class AssetDeliveryEffectResult(
    val operation_kind: AssetDeliveryOperation,
    val transfer: AssetTransferReport?,
    val removal: AssetRemovalReport?
) {
    fun hasValidBody(): Boolean {
        val bodyCount = listOf(transfer, removal).count { it != null }
        if (bodyCount != 1) return false
        return when (operation_kind) {
            AssetDeliveryOperation.FETCH_ASSET -> transfer != null
            AssetDeliveryOperation.REMOVE_ASSET -> removal != null
        }
    }

    companion object {
        fun transfer(body: AssetTransferReport): AssetDeliveryEffectResult =
            AssetDeliveryEffectResult(
                operation_kind = AssetDeliveryOperation.FETCH_ASSET,
                transfer = body,
                removal = null,
            )

        fun removal(body: AssetRemovalReport): AssetDeliveryEffectResult =
            AssetDeliveryEffectResult(
                operation_kind = AssetDeliveryOperation.REMOVE_ASSET,
                transfer = null,
                removal = body,
            )

    }
}

data class CatalogFetchEffect(
    val operation_kind: CatalogNetworkOperation,
    val known_catalog_version: String?,
    val max_response_bytes: UInt
)

data class CatalogFetchEffectResult(
    val operation_kind: CatalogNetworkOperation,
    val disposition: CatalogFetchDisposition,
    val body: ByteArray
)

data class CachedCatalogOverlay(
    val fetched_at_utc_ms: ULong,
    val document: ByteArray
)

data class AssetOnDisk(
    val asset_id: String,
    val asset_revision: String,
    val presence: AssetPresence,
    val written_bytes: ULong
)

data class SetAssetDeliveryPolicyCommand(
    val network_cost: AssetNetworkCost,
    val metered_permitted: Boolean
)

data class RequestAssetCommand(
    val asset_id: String,
    val asset_revision: String
)

data class RemoveAssetCommand(
    val asset_id: String,
    val asset_revision: String
)

data class SkillRecord(
    val skill_id: String,
    val origin: String,
    val provenance: SkillProvenance,
    val status: SkillStatus,
    val active_version: UInt,
    val definition: ByteArray,
    val step_count: UInt,
    val installed_at_utc_ms: ULong,
    val updated_at_utc_ms: ULong
)

data class SkillRunRecord(
    val skill_id: String,
    val version: UInt,
    val task_id: String,
    val outcome: SkillRunOutcome,
    val ran_at_utc_ms: ULong
)

data class AssistantConfiguration(
    val revision: ULong,
    val disabled_abilities: List<AssistantAbility>,
    val preset: PersonalityPreset,
    val pace: UInt,
    val length: UInt,
    val check_in: UInt
)

data class CoreBootstrap(
    val service_generation: ULong,
    val private_profile: Boolean,
    val core_journal_schema_version: UInt,
    val core_journal_schema_checksum: String,
    val tasks: List<TaskRestoreRecord>,
    val generation_capability_entropy: ByteArray,
    val account_session: AccountSessionHandle?,
    val browser_profile_id: String,
    val workspaces: List<WorkspaceRestoreRecord>,
    val browser_session_id: String,
    val available_account_methods: List<AccountAuthMethod>,
    val asset_platform: AssetPlatform,
    val assets: List<AssetOnDisk>,
    val skills: List<SkillRecord>,
    val recall: List<SkillRunRecord>,
    val cached_catalog_overlay: CachedCatalogOverlay?,
    val assistant_configuration: AssistantConfiguration?,
    val library_revision: ULong,
    val library_entries: List<LibraryEntryRecord>,
    val memory_revision: ULong,
    val memory_records: List<MemoryRecord>
)

data class TaskBudget(
    val kind: TaskBudgetKind,
    val limit: ULong
)

data class TaskConsentSource(
    val source_id: String,
    val tab_id: String,
    val normalized_origin: String,
    val canonical_locator: String?
)

data class LibraryRefreshSource(
    val source_id: String,
    val title: String,
    val host: String,
    val canonical_locator: String,
    val original_content_digest: ByteArray
)

data class LibraryRefreshRequest(
    val preview_id: String,
    val library_revision: ULong,
    val collection_id: String,
    val source_workspace_revision: ULong,
    val sources: List<LibraryRefreshSource>
)

data class TaskConsentPreview(
    val sources: List<TaskConsentSource>,
    val source_discovery_enabled: Boolean,
    val new_source_cap: UInt,
    val provider_route: TaskProviderRoute
)

data class TaskSuppliedValuePosition(
    val index: UInt,
    val request_id: String
)

data class TaskToggleState(
    val checked: Boolean
)

data class TaskActionInput(
    val kind: TaskActionInputKind,
    val supplied_value: TaskSuppliedValuePosition?,
    val toggle_state: TaskToggleState?
)

data class TaskPolicyEffect(
    val operation: OperationEnvelope,
    val effect_id: String,
    val task_id: String,
    val action_id: String,
    val action_class: PolicyActionClass,
    val proposal_digest: String,
    val idempotency_key: String,
    val tab_id: String,
    val node_id: String?,
    val principal: PolicyPrincipal,
    val data_classes: List<BipSensitivity>,
    val context_risk: PolicyRiskClass,
    val approval: PolicyApprovalFact?,
    val control_mode: TaskControlMode,
    val policy_version: UInt,
    val operation_kind: TaskActionOperationKind,
    val tool_name: String,
    val destination_address: String?,
    val canonical_intent: ByteArray,
    val transient_search_query: String?,
    val input: TaskActionInput,
    val discovery: TaskDiscoveryAuthorityFact?
)

data class TaskFrozenDocument(
    val frame_id: String,
    val page_epoch: String,
    val graph_revision: ULong,
    val normalized_origin: String,
    val opaque_origin_id: String?
)

data class TaskTabDocumentTarget(
    val tab_id: String,
    val frame_id: String,
    val page_epoch: String,
    val graph_revision: ULong
)

data class TaskTabActionBinding(
    val browser_session_id: String,
    val target: TaskTabDocumentTarget?
)

data class TaskDownloadActionBinding(
    val browser_session_id: String,
    val download_id: String?
)

data class TaskStoreActionBinding(
    val query: String?,
    val limit: UInt
)

data class TaskExecutableAction(
    val action_class: PolicyActionClass,
    val tool_name: String,
    val tab_id: String,
    val node_id: String?,
    val destination_origin: String?,
    val operand_handle: String?,
    val destination_address: String?,
    val operation_kind: TaskActionOperationKind,
    val canonical_intent: ByteArray,
    val transient_search_query: String?,
    val input: TaskActionInput,
    val task_tab: TaskTabActionBinding?,
    val task_download: TaskDownloadActionBinding?,
    val task_store: TaskStoreActionBinding?
)

data class TaskObservationBounds(
    val scope: ObservationScope,
    val max_bytes: UInt,
    val max_nodes: UInt,
    val max_text_bytes: UInt,
    val max_frames: UInt,
    val deadline_ms: UInt
)

data class TaskActionEffect(
    val action_id: String,
    val proposal_digest: String,
    val idempotency_key: String,
    val capability_id: String,
    val dispatch_id: String,
    val document: TaskFrozenDocument,
    val executable: TaskExecutableAction,
    val preconditions: List<TaskActionPrecondition>,
    val postcondition: TaskActionPostcondition,
    val observation: TaskObservationBounds?
)

data class TaskRevocationEffect(
    val reason: TaskRevocationReason
)

data class TaskApprovalEffect(
    val action_id: String,
    val proposal_digest: String,
    val form_action: TaskExecutableAction?
)

data class TaskPermissionEffect(
    val request_id: String,
    val permission: PlatformPermission,
    val deadline_monotonic_ms: ULong,
    val deadline_utc_ms: ULong,
    val browser_session_id: String
)

data class TaskSettlementEffect(
    val kind: TaskSettlementKind
)

data class TaskReconcileEffect(
    val action_id: String,
    val rule: TaskRecoveryRule,
    val dispatch_id: String,
    val operation: TaskActionOperationKind
)

data class TaskReleaseTabsEffect(
    val terminal_revision: ULong
)

data class TaskArtifactEffect(
    val kind: TaskArtifactKind,
    val artifact_id: String,
    val workspace_revision: ULong,
    val content: ByteArray
)

data class TaskModelEffect(
    val call_id: String,
    val request: ModelRequestEffect
)

data class TaskHandoverEffect(
    val handover_id: String,
    val window_ms: UInt
)

data class TaskToolJobEffect(
    val action_id: String,
    val job_id: String,
    val runtime: ToolRuntimeKind,
    val job: ToolJobEffect?
)

data class TaskFieldValuesEffect(
    val request_id: String,
    val tab_id: String,
    val node_id: String,
    val companion_node_ids: List<String>
)

data class TaskDiscoveryBootstrapEffect(
    val browser_session_id: String,
    val remaining_new_source_cap: UInt
)

data class TaskLibraryToolEffect(
    val action_id: String,
    val operation_kind: TaskActionOperationKind
)

data class TaskMemoryToolEffect(
    val action_id: String,
    val operation_kind: TaskActionOperationKind
)

data class TaskEffectBinding(
    val operation: OperationEnvelope,
    val effect_id: String,
    val task_id: String,
    val ordinal: UInt,
    val kind: TaskReducerEffectKind,
    val revocation: TaskRevocationEffect?,
    val policy: TaskPolicyEffect?,
    val approval: TaskApprovalEffect?,
    val permission: TaskPermissionEffect?,
    val action: TaskActionEffect?,
    val settlement: TaskSettlementEffect?,
    val reconcile: TaskReconcileEffect?,
    val release_tabs: TaskReleaseTabsEffect?,
    val generate_artifact: TaskArtifactEffect?,
    val export_artifact: TaskArtifactEffect?,
    val model: TaskModelEffect?,
    val handover: TaskHandoverEffect?,
    val tool_job: TaskToolJobEffect?,
    val field_values: TaskFieldValuesEffect?,
    val discovery_bootstrap: TaskDiscoveryBootstrapEffect?,
    val library_tool: TaskLibraryToolEffect?,
    val memory_tool: TaskMemoryToolEffect?
) {
    fun hasValidBody(): Boolean {
        val bodyCount = listOf(revocation, policy, approval, permission, action, settlement, reconcile, release_tabs, generate_artifact, export_artifact, model, handover, tool_job, field_values, discovery_bootstrap, library_tool, memory_tool).count { it != null }
        if (bodyCount != 1) return false
        return when (kind) {
            TaskReducerEffectKind.REVOKE_AUTHORITY -> revocation != null
            TaskReducerEffectKind.ASK_POLICY -> policy != null
            TaskReducerEffectKind.REQUEST_APPROVAL -> approval != null
            TaskReducerEffectKind.REQUEST_PERMISSION -> permission != null
            TaskReducerEffectKind.DISPATCH_ACTION -> action != null
            TaskReducerEffectKind.AWAIT_IN_FLIGHT_WORK -> settlement != null
            TaskReducerEffectKind.RECONCILE_ACTION -> reconcile != null
            TaskReducerEffectKind.RELEASE_TASK_TABS -> release_tabs != null
            TaskReducerEffectKind.GENERATE_ARTIFACT -> generate_artifact != null
            TaskReducerEffectKind.EXPORT_ARTIFACT -> export_artifact != null
            TaskReducerEffectKind.CALL_MODEL -> model != null
            TaskReducerEffectKind.AWAIT_HANDOVER -> handover != null
            TaskReducerEffectKind.RUN_TOOL_JOB -> tool_job != null
            TaskReducerEffectKind.REQUEST_FIELD_VALUES -> field_values != null
            TaskReducerEffectKind.PREPARE_DISCOVERY_TAB -> discovery_bootstrap != null
            TaskReducerEffectKind.RUN_LIBRARY_TOOL -> library_tool != null
            TaskReducerEffectKind.RUN_MEMORY_TOOL -> memory_tool != null
        }
    }

    companion object {
        fun revocation(operation: OperationEnvelope, effect_id: String, task_id: String, ordinal: UInt, body: TaskRevocationEffect): TaskEffectBinding =
            TaskEffectBinding(
                operation = operation,
                effect_id = effect_id,
                task_id = task_id,
                ordinal = ordinal,
                kind = TaskReducerEffectKind.REVOKE_AUTHORITY,
                revocation = body,
                policy = null,
                approval = null,
                permission = null,
                action = null,
                settlement = null,
                reconcile = null,
                release_tabs = null,
                generate_artifact = null,
                export_artifact = null,
                model = null,
                handover = null,
                tool_job = null,
                field_values = null,
                discovery_bootstrap = null,
                library_tool = null,
                memory_tool = null,
            )

        fun policy(operation: OperationEnvelope, effect_id: String, task_id: String, ordinal: UInt, body: TaskPolicyEffect): TaskEffectBinding =
            TaskEffectBinding(
                operation = operation,
                effect_id = effect_id,
                task_id = task_id,
                ordinal = ordinal,
                kind = TaskReducerEffectKind.ASK_POLICY,
                revocation = null,
                policy = body,
                approval = null,
                permission = null,
                action = null,
                settlement = null,
                reconcile = null,
                release_tabs = null,
                generate_artifact = null,
                export_artifact = null,
                model = null,
                handover = null,
                tool_job = null,
                field_values = null,
                discovery_bootstrap = null,
                library_tool = null,
                memory_tool = null,
            )

        fun approval(operation: OperationEnvelope, effect_id: String, task_id: String, ordinal: UInt, body: TaskApprovalEffect): TaskEffectBinding =
            TaskEffectBinding(
                operation = operation,
                effect_id = effect_id,
                task_id = task_id,
                ordinal = ordinal,
                kind = TaskReducerEffectKind.REQUEST_APPROVAL,
                revocation = null,
                policy = null,
                approval = body,
                permission = null,
                action = null,
                settlement = null,
                reconcile = null,
                release_tabs = null,
                generate_artifact = null,
                export_artifact = null,
                model = null,
                handover = null,
                tool_job = null,
                field_values = null,
                discovery_bootstrap = null,
                library_tool = null,
                memory_tool = null,
            )

        fun permission(operation: OperationEnvelope, effect_id: String, task_id: String, ordinal: UInt, body: TaskPermissionEffect): TaskEffectBinding =
            TaskEffectBinding(
                operation = operation,
                effect_id = effect_id,
                task_id = task_id,
                ordinal = ordinal,
                kind = TaskReducerEffectKind.REQUEST_PERMISSION,
                revocation = null,
                policy = null,
                approval = null,
                permission = body,
                action = null,
                settlement = null,
                reconcile = null,
                release_tabs = null,
                generate_artifact = null,
                export_artifact = null,
                model = null,
                handover = null,
                tool_job = null,
                field_values = null,
                discovery_bootstrap = null,
                library_tool = null,
                memory_tool = null,
            )

        fun action(operation: OperationEnvelope, effect_id: String, task_id: String, ordinal: UInt, body: TaskActionEffect): TaskEffectBinding =
            TaskEffectBinding(
                operation = operation,
                effect_id = effect_id,
                task_id = task_id,
                ordinal = ordinal,
                kind = TaskReducerEffectKind.DISPATCH_ACTION,
                revocation = null,
                policy = null,
                approval = null,
                permission = null,
                action = body,
                settlement = null,
                reconcile = null,
                release_tabs = null,
                generate_artifact = null,
                export_artifact = null,
                model = null,
                handover = null,
                tool_job = null,
                field_values = null,
                discovery_bootstrap = null,
                library_tool = null,
                memory_tool = null,
            )

        fun settlement(operation: OperationEnvelope, effect_id: String, task_id: String, ordinal: UInt, body: TaskSettlementEffect): TaskEffectBinding =
            TaskEffectBinding(
                operation = operation,
                effect_id = effect_id,
                task_id = task_id,
                ordinal = ordinal,
                kind = TaskReducerEffectKind.AWAIT_IN_FLIGHT_WORK,
                revocation = null,
                policy = null,
                approval = null,
                permission = null,
                action = null,
                settlement = body,
                reconcile = null,
                release_tabs = null,
                generate_artifact = null,
                export_artifact = null,
                model = null,
                handover = null,
                tool_job = null,
                field_values = null,
                discovery_bootstrap = null,
                library_tool = null,
                memory_tool = null,
            )

        fun reconcile(operation: OperationEnvelope, effect_id: String, task_id: String, ordinal: UInt, body: TaskReconcileEffect): TaskEffectBinding =
            TaskEffectBinding(
                operation = operation,
                effect_id = effect_id,
                task_id = task_id,
                ordinal = ordinal,
                kind = TaskReducerEffectKind.RECONCILE_ACTION,
                revocation = null,
                policy = null,
                approval = null,
                permission = null,
                action = null,
                settlement = null,
                reconcile = body,
                release_tabs = null,
                generate_artifact = null,
                export_artifact = null,
                model = null,
                handover = null,
                tool_job = null,
                field_values = null,
                discovery_bootstrap = null,
                library_tool = null,
                memory_tool = null,
            )

        fun releaseTabs(operation: OperationEnvelope, effect_id: String, task_id: String, ordinal: UInt, body: TaskReleaseTabsEffect): TaskEffectBinding =
            TaskEffectBinding(
                operation = operation,
                effect_id = effect_id,
                task_id = task_id,
                ordinal = ordinal,
                kind = TaskReducerEffectKind.RELEASE_TASK_TABS,
                revocation = null,
                policy = null,
                approval = null,
                permission = null,
                action = null,
                settlement = null,
                reconcile = null,
                release_tabs = body,
                generate_artifact = null,
                export_artifact = null,
                model = null,
                handover = null,
                tool_job = null,
                field_values = null,
                discovery_bootstrap = null,
                library_tool = null,
                memory_tool = null,
            )

        fun generateArtifact(operation: OperationEnvelope, effect_id: String, task_id: String, ordinal: UInt, body: TaskArtifactEffect): TaskEffectBinding =
            TaskEffectBinding(
                operation = operation,
                effect_id = effect_id,
                task_id = task_id,
                ordinal = ordinal,
                kind = TaskReducerEffectKind.GENERATE_ARTIFACT,
                revocation = null,
                policy = null,
                approval = null,
                permission = null,
                action = null,
                settlement = null,
                reconcile = null,
                release_tabs = null,
                generate_artifact = body,
                export_artifact = null,
                model = null,
                handover = null,
                tool_job = null,
                field_values = null,
                discovery_bootstrap = null,
                library_tool = null,
                memory_tool = null,
            )

        fun exportArtifact(operation: OperationEnvelope, effect_id: String, task_id: String, ordinal: UInt, body: TaskArtifactEffect): TaskEffectBinding =
            TaskEffectBinding(
                operation = operation,
                effect_id = effect_id,
                task_id = task_id,
                ordinal = ordinal,
                kind = TaskReducerEffectKind.EXPORT_ARTIFACT,
                revocation = null,
                policy = null,
                approval = null,
                permission = null,
                action = null,
                settlement = null,
                reconcile = null,
                release_tabs = null,
                generate_artifact = null,
                export_artifact = body,
                model = null,
                handover = null,
                tool_job = null,
                field_values = null,
                discovery_bootstrap = null,
                library_tool = null,
                memory_tool = null,
            )

        fun model(operation: OperationEnvelope, effect_id: String, task_id: String, ordinal: UInt, body: TaskModelEffect): TaskEffectBinding =
            TaskEffectBinding(
                operation = operation,
                effect_id = effect_id,
                task_id = task_id,
                ordinal = ordinal,
                kind = TaskReducerEffectKind.CALL_MODEL,
                revocation = null,
                policy = null,
                approval = null,
                permission = null,
                action = null,
                settlement = null,
                reconcile = null,
                release_tabs = null,
                generate_artifact = null,
                export_artifact = null,
                model = body,
                handover = null,
                tool_job = null,
                field_values = null,
                discovery_bootstrap = null,
                library_tool = null,
                memory_tool = null,
            )

        fun handover(operation: OperationEnvelope, effect_id: String, task_id: String, ordinal: UInt, body: TaskHandoverEffect): TaskEffectBinding =
            TaskEffectBinding(
                operation = operation,
                effect_id = effect_id,
                task_id = task_id,
                ordinal = ordinal,
                kind = TaskReducerEffectKind.AWAIT_HANDOVER,
                revocation = null,
                policy = null,
                approval = null,
                permission = null,
                action = null,
                settlement = null,
                reconcile = null,
                release_tabs = null,
                generate_artifact = null,
                export_artifact = null,
                model = null,
                handover = body,
                tool_job = null,
                field_values = null,
                discovery_bootstrap = null,
                library_tool = null,
                memory_tool = null,
            )

        fun toolJob(operation: OperationEnvelope, effect_id: String, task_id: String, ordinal: UInt, body: TaskToolJobEffect): TaskEffectBinding =
            TaskEffectBinding(
                operation = operation,
                effect_id = effect_id,
                task_id = task_id,
                ordinal = ordinal,
                kind = TaskReducerEffectKind.RUN_TOOL_JOB,
                revocation = null,
                policy = null,
                approval = null,
                permission = null,
                action = null,
                settlement = null,
                reconcile = null,
                release_tabs = null,
                generate_artifact = null,
                export_artifact = null,
                model = null,
                handover = null,
                tool_job = body,
                field_values = null,
                discovery_bootstrap = null,
                library_tool = null,
                memory_tool = null,
            )

        fun fieldValues(operation: OperationEnvelope, effect_id: String, task_id: String, ordinal: UInt, body: TaskFieldValuesEffect): TaskEffectBinding =
            TaskEffectBinding(
                operation = operation,
                effect_id = effect_id,
                task_id = task_id,
                ordinal = ordinal,
                kind = TaskReducerEffectKind.REQUEST_FIELD_VALUES,
                revocation = null,
                policy = null,
                approval = null,
                permission = null,
                action = null,
                settlement = null,
                reconcile = null,
                release_tabs = null,
                generate_artifact = null,
                export_artifact = null,
                model = null,
                handover = null,
                tool_job = null,
                field_values = body,
                discovery_bootstrap = null,
                library_tool = null,
                memory_tool = null,
            )

        fun discoveryBootstrap(operation: OperationEnvelope, effect_id: String, task_id: String, ordinal: UInt, body: TaskDiscoveryBootstrapEffect): TaskEffectBinding =
            TaskEffectBinding(
                operation = operation,
                effect_id = effect_id,
                task_id = task_id,
                ordinal = ordinal,
                kind = TaskReducerEffectKind.PREPARE_DISCOVERY_TAB,
                revocation = null,
                policy = null,
                approval = null,
                permission = null,
                action = null,
                settlement = null,
                reconcile = null,
                release_tabs = null,
                generate_artifact = null,
                export_artifact = null,
                model = null,
                handover = null,
                tool_job = null,
                field_values = null,
                discovery_bootstrap = body,
                library_tool = null,
                memory_tool = null,
            )

        fun libraryTool(operation: OperationEnvelope, effect_id: String, task_id: String, ordinal: UInt, body: TaskLibraryToolEffect): TaskEffectBinding =
            TaskEffectBinding(
                operation = operation,
                effect_id = effect_id,
                task_id = task_id,
                ordinal = ordinal,
                kind = TaskReducerEffectKind.RUN_LIBRARY_TOOL,
                revocation = null,
                policy = null,
                approval = null,
                permission = null,
                action = null,
                settlement = null,
                reconcile = null,
                release_tabs = null,
                generate_artifact = null,
                export_artifact = null,
                model = null,
                handover = null,
                tool_job = null,
                field_values = null,
                discovery_bootstrap = null,
                library_tool = body,
                memory_tool = null,
            )

        fun memoryTool(operation: OperationEnvelope, effect_id: String, task_id: String, ordinal: UInt, body: TaskMemoryToolEffect): TaskEffectBinding =
            TaskEffectBinding(
                operation = operation,
                effect_id = effect_id,
                task_id = task_id,
                ordinal = ordinal,
                kind = TaskReducerEffectKind.RUN_MEMORY_TOOL,
                revocation = null,
                policy = null,
                approval = null,
                permission = null,
                action = null,
                settlement = null,
                reconcile = null,
                release_tabs = null,
                generate_artifact = null,
                export_artifact = null,
                model = null,
                handover = null,
                tool_job = null,
                field_values = null,
                discovery_bootstrap = null,
                library_tool = null,
                memory_tool = body,
            )

    }
}

data class TaskProducedArtifactReceipt(
    val artifact_id: String,
    val kind: TaskArtifactKind
)

data class TaskToolOutputReceipt(
    val digest: ByteArray,
    val byte_count: ULong,
    val chunk_count: UInt,
    val artifact: TaskProducedArtifactReceipt?
)

data class TaskReconciledActionResult(
    val result_code: UInt
)

data class TaskEffectCompletion(
    val operation: OperationEnvelope,
    val effect_id: String,
    val task_id: String,
    val kind: TaskReducerEffectKind,
    val status: TaskEffectCompletionStatus,
    val effect_result: EffectResult?,
    val tool_output: TaskToolOutputReceipt?,
    val reconciled_action_result: TaskReconciledActionResult?
)

data class BuiltinSkillReference(
    val skill_id: BuiltinSkillId,
    val version: UInt
)

data class StartTaskCommand(
    val task_id: String,
    val workspace_id: String?,
    val browser_profile_id: String,
    val kind: TaskKind,
    val goal: String,
    val control_mode: TaskControlMode,
    val provider_route_id: String?,
    val assistant_config_version: UInt,
    val policy_version: UInt,
    val skill_version_id: String?,
    val tool_allowlist: List<String>,
    val milestone: TaskMilestone,
    val budgets: List<TaskBudget>,
    val has_task_deadline: Boolean,
    val task_deadline_monotonic_ms: ULong,
    val predecessor_task_id: String?,
    val trace_id: String,
    val task_id_seed: ByteArray,
    val template_id: TaskTemplateId,
    val consent_preview: TaskConsentPreview,
    val initial_consent_receipt_id: String,
    val browser_session_id: String,
    val task_deadline_utc_ms: ULong,
    val library_refresh: LibraryRefreshRequest?,
    val builtin_skill: BuiltinSkillReference?
)

data class CancelTaskCommand(
    val task_id: String,
    val reason: CancelReason,
    val trace_id: String
)

data class PauseTaskCommand(
    val task_id: String,
    val trace_id: String
)

data class ResumeTaskCommand(
    val task_id: String,
    val trace_id: String
)

data class TakeOverCommand(
    val task_id: String,
    val trace_id: String
)

data class UserDecisionCommand(
    val task_id: String,
    val action_id: String,
    val decision: UserDecisionKind,
    val approval_digest: String,
    val trace_id: String,
    val approval_receipt_id: String,
    val approval_expires_at_monotonic_ms: ULong,
    val approval_expires_at_utc_ms: ULong,
    val browser_session_id: String
)

data class AuthCallbackCommand(
    val flow_id: String,
    val redirect_binding_id: String,
    val returned_state: String,
    val status: AuthCallbackStatus,
    val authorization_code_handle: String?
)

fun AuthCallbackCommand.hasValidPresence(): Boolean =
    (if (status == AuthCallbackStatus.AUTHORIZATION_CODE) authorization_code_handle != null else authorization_code_handle == null)

data class PermissionResultCommand(
    val task_id: String,
    val request_id: String,
    val permission: PlatformPermission,
    val decision: PermissionDecision,
    val trace_id: String
)

data class CompleteHandoverCommand(
    val task_id: String,
    val handover_id: String,
    val lease_before: String,
    val resumed_with: String,
    val person_input: UInt,
    val trace_id: String
)

data class ExpireHandoverCommand(
    val task_id: String,
    val handover_id: String,
    val trace_id: String
)

data class SupplyUserInputCommand(
    val task_id: String,
    val answer: String,
    val trace_id: String
)

data class FollowUpCommand(
    val task_id: String,
    val question: String,
    val trace_id: String
)

data class SupplyFieldValuesCommand(
    val task_id: String,
    val request_id: String,
    val supplied: UInt,
    val trace_id: String,
    val outcome: FieldValueAskOutcome,
    val field_node_ids: List<String>
)

data class StartAuthCommand(
    val flow_id: String,
    val method: AccountAuthMethod,
    val redirect_binding_id: String,
    val scopes: List<AccountScope>,
    val issued_at_monotonic_ms: ULong
)

data class RequestEmailLinkCommand(
    val flow_id: String,
    val email: String,
    val redirect_binding_id: String,
    val scopes: List<AccountScope>,
    val issued_at_monotonic_ms: ULong
)

data class SignOutCommand(
    val account_subject: String?
)

data class AuthCredentialResultCommand(
    val flow_id: String,
    val method: AccountAuthMethod,
    val credential_handle: String?,
    val status: AuthCredentialStatus
)

data class CorrectWorkspaceFactCommand(
    val workspace_id: String,
    val expected_revision: ULong,
    val fact_id: String,
    val value: String
)

data class ExcludeWorkspaceSourceCommand(
    val workspace_id: String,
    val expected_revision: ULong,
    val source_id: String
)

data class RequestWorkspaceExportCommand(
    val request_id: String,
    val workspace_id: String,
    val expected_revision: ULong,
    val format: WorkspaceExportFormat
)

data class SaveWorkspaceCommand(
    val workspace_id: String,
    val expected_revision: ULong
)

data class RenameWorkspaceCommand(
    val workspace_id: String,
    val expected_revision: ULong,
    val display_name: String
)

data class DeleteWorkspaceCommand(
    val workspace_id: String,
    val expected_revision: ULong,
    val confirmation_token: String
)

data class DiscardWorkspaceCommand(
    val workspace_id: String,
    val expected_revision: ULong
)

data class SearchLibraryCommand(
    val request_id: String,
    val query: String,
    val limit: UInt,
    val requested_at_epoch_ms: ULong
)

data class SaveLibraryFactCommand(
    val workspace_id: String,
    val expected_workspace_revision: ULong,
    val fact_id: String,
    val expected_library_revision: ULong,
    val expected_entry_revision: ULong,
    val approved_at_epoch_ms: ULong
)

data class RemoveLibraryEntryCommand(
    val entry_id: String,
    val expected_library_revision: ULong,
    val expected_entry_revision: ULong,
    val removed_at_epoch_ms: ULong
)

data class RequestLibraryExportCommand(
    val request_id: String,
    val expected_library_revision: ULong,
    val collection_id: String?,
    val format: WorkspaceExportFormat
)

data class SearchMemoryCommand(
    val request_id: String,
    val query: String,
    val limit: UInt,
    val requested_at_epoch_ms: ULong
)

data class UpsertMemoryCommand(
    val memory_id: String?,
    val statement: String,
    val scope_kind: MemoryScopeKind,
    val scope_workspace: MemoryWorkspaceRecord?,
    val sensitivity: MemorySensitivity,
    val expected_memory_revision: ULong,
    val expected_record_revision: ULong,
    val expires_at_epoch_ms: ULong,
    val approved_at_epoch_ms: ULong
)

data class DeleteMemoryCommand(
    val memory_id: String,
    val expected_memory_revision: ULong,
    val expected_record_revision: ULong,
    val deleted_at_epoch_ms: ULong
)

data class SaveProviderCredentialCommand(
    val provider_id: String,
    val auth_method: ProviderAuthMethod,
    val credential_handle: String
)

data class SetProviderCredentialStateCommand(
    val provider_id: String,
    val state: ProviderCredentialState,
    val available_model_ids: List<String>
)

data class ThinkingPreference(
    val level: ThinkingLevel
)

data class SetProviderModelPreferenceCommand(
    val provider_id: String,
    val model_id: String?,
    val thinking: ThinkingPreference?
)

data class ProbeProviderCredentialCommand(
    val provider_id: String,
    val credential_handle: String
)

data class ForgetProviderCredentialCommand(
    val provider_id: String
)

data class StartProviderAuthCommand(
    val flow_id: String,
    val provider_id: String,
    val redirect_binding_id: String,
    val issued_at_monotonic_ms: ULong
)

data class CancelProviderAuthCommand(
    val flow_id: String
)

data class ProviderAuthCallbackCommand(
    val flow_id: String,
    val redirect_binding_id: String,
    val returned_state: String,
    val status: AuthCallbackStatus,
    val authorization_code_handle: String?
)

fun ProviderAuthCallbackCommand.hasValidPresence(): Boolean =
    (if (status == AuthCallbackStatus.AUTHORIZATION_CODE) authorization_code_handle != null else authorization_code_handle == null)

data class CustomModelSpec(
    val model_id: String,
    val display_name: String,
    val context_window: UInt,
    val max_output_tokens: UInt,
    val reasoning: Boolean,
    val tool_calling: Boolean
)

data class DetectedServer(
    val server_kind: ServerKind
)

data class SaveCustomProviderCommand(
    val provider_id: String,
    val display_name: String,
    val endpoint: String,
    val wire_api: ProviderWireApi,
    val credential_handle: String?,
    val models: List<CustomModelSpec>,
    val detected_server: DetectedServer?
)

data class RemoveCustomProviderCommand(
    val provider_id: String
)

data class ProbeCustomEndpointCommand(
    val endpoint: String,
    val wire_api: ProviderWireApi,
    val credential_handle: String?,
    val provider_id: String
)

data class RequestComposerCompletionCommand(
    val request_id: String,
    val prefix: String,
    val suffix: String?
)

data class CancelComposerCompletionCommand(
    val request_id: String
)

data class SetAssistantConfigurationCommand(
    val expected_revision: ULong,
    val disabled_abilities: List<AssistantAbility>,
    val preset: PersonalityPreset,
    val pace: UInt,
    val length: UInt,
    val check_in: UInt
)

data class SavedSignInMetadata(
    val id: String,
    val site: String,
    val username: String,
    val last_used_epoch_ms: ULong
)

data class SavedDetailRecord(
    val id: String,
    val given_name: String,
    val family_name: String,
    val email: String,
    val phone: String,
    val address: String,
    val postcode: String,
    val country: String
)

data class ReplaceSavedDataSnapshotCommand(
    val sign_ins_availability: SavedDataAvailability,
    val sign_ins_revision: ULong,
    val sign_ins: List<SavedSignInMetadata>,
    val details_availability: SavedDataAvailability,
    val details_revision: ULong,
    val details: List<SavedDetailRecord>
)

data class AcceptTaskArtifactCommand(
    val task_id: String,
    val artifact_id: String
)

data class ExportTaskArtifactCommand(
    val task_id: String,
    val artifact_id: String,
    val kind: TaskArtifactKind
)

data class SkillObservedClause(
    val kind: SkillClauseKind,
    val role: UInt,
    val detail: UInt
)

data class SkillSemanticTarget(
    val role: UInt,
    val phrase: UInt
)

data class SkillObservedArgument(
    val parameter: UInt,
    val kind: SkillArgumentKind,
    val value: ULong,
    val purpose: UInt,
    val public_address: String?,
    val semantic_target: SkillSemanticTarget?
)

data class SkillObservedStep(
    val verb: String,
    val arguments: List<SkillObservedArgument>,
    val postcondition: UInt,
    val has_fill: Boolean,
    val fill_purpose: UInt
)

data class MutateSkillCommand(
    val kind: SkillMutationKind,
    val skill_id: String,
    val expected_version: UInt,
    val origin: String,
    val clauses: List<SkillObservedClause>,
    val steps: List<SkillObservedStep>,
    val admitted: UInt,
    val enabled: Boolean,
    val recorded_at_epoch_ms: ULong
)

data class CoreServiceCommand(
    val operation: OperationEnvelope,
    val kind: CoreServiceCommandKind,
    val start_task: StartTaskCommand?,
    val cancel_task: CancelTaskCommand?,
    val user_decision: UserDecisionCommand?,
    val auth_callback: AuthCallbackCommand?,
    val permission_result: PermissionResultCommand?,
    val start_auth: StartAuthCommand?,
    val request_email_link: RequestEmailLinkCommand?,
    val sign_out: SignOutCommand?,
    val auth_credential_result: AuthCredentialResultCommand?,
    val correct_workspace_fact: CorrectWorkspaceFactCommand?,
    val exclude_workspace_source: ExcludeWorkspaceSourceCommand?,
    val request_workspace_export: RequestWorkspaceExportCommand?,
    val set_asset_delivery_policy: SetAssetDeliveryPolicyCommand?,
    val request_asset: RequestAssetCommand?,
    val remove_asset: RemoveAssetCommand?,
    val save_provider_credential: SaveProviderCredentialCommand?,
    val forget_provider_credential: ForgetProviderCredentialCommand?,
    val start_provider_auth: StartProviderAuthCommand?,
    val provider_auth_callback: ProviderAuthCallbackCommand?,
    val save_custom_provider: SaveCustomProviderCommand?,
    val remove_custom_provider: RemoveCustomProviderCommand?,
    val complete_handover: CompleteHandoverCommand?,
    val expire_handover: ExpireHandoverCommand?,
    val supply_user_input: SupplyUserInputCommand?,
    val set_provider_credential_state: SetProviderCredentialStateCommand?,
    val probe_provider_credential: ProbeProviderCredentialCommand?,
    val supply_field_values: SupplyFieldValuesCommand?,
    val set_provider_model_preference: SetProviderModelPreferenceCommand?,
    val probe_custom_endpoint: ProbeCustomEndpointCommand?,
    val request_composer_completion: RequestComposerCompletionCommand?,
    val cancel_composer_completion: CancelComposerCompletionCommand?,
    val pause_task: PauseTaskCommand?,
    val resume_task: ResumeTaskCommand?,
    val take_over: TakeOverCommand?,
    val set_assistant_configuration: SetAssistantConfigurationCommand?,
    val save_workspace: SaveWorkspaceCommand?,
    val rename_workspace: RenameWorkspaceCommand?,
    val delete_workspace: DeleteWorkspaceCommand?,
    val discard_workspace: DiscardWorkspaceCommand?,
    val search_library: SearchLibraryCommand?,
    val save_library_fact: SaveLibraryFactCommand?,
    val remove_library_entry: RemoveLibraryEntryCommand?,
    val request_library_export: RequestLibraryExportCommand?,
    val search_memory: SearchMemoryCommand?,
    val upsert_memory: UpsertMemoryCommand?,
    val delete_memory: DeleteMemoryCommand?,
    val accept_task_artifact: AcceptTaskArtifactCommand?,
    val export_task_artifact: ExportTaskArtifactCommand?,
    val replace_saved_data_snapshot: ReplaceSavedDataSnapshotCommand?,
    val mutate_skill: MutateSkillCommand?,
    val cancel_provider_auth: CancelProviderAuthCommand?,
    val follow_up: FollowUpCommand?
) {
    fun hasValidBody(): Boolean {
        val bodyCount = listOf(start_task, cancel_task, user_decision, auth_callback, permission_result, start_auth, request_email_link, sign_out, auth_credential_result, correct_workspace_fact, exclude_workspace_source, request_workspace_export, set_asset_delivery_policy, request_asset, remove_asset, save_provider_credential, forget_provider_credential, start_provider_auth, provider_auth_callback, save_custom_provider, remove_custom_provider, complete_handover, expire_handover, supply_user_input, set_provider_credential_state, probe_provider_credential, supply_field_values, set_provider_model_preference, probe_custom_endpoint, request_composer_completion, cancel_composer_completion, pause_task, resume_task, take_over, set_assistant_configuration, save_workspace, rename_workspace, delete_workspace, discard_workspace, search_library, save_library_fact, remove_library_entry, request_library_export, search_memory, upsert_memory, delete_memory, accept_task_artifact, export_task_artifact, replace_saved_data_snapshot, mutate_skill, cancel_provider_auth, follow_up).count { it != null }
        if (bodyCount != 1) return false
        return when (kind) {
            CoreServiceCommandKind.START_TASK -> start_task != null
            CoreServiceCommandKind.CANCEL_TASK -> cancel_task != null
            CoreServiceCommandKind.USER_DECISION -> user_decision != null
            CoreServiceCommandKind.AUTH_CALLBACK -> auth_callback != null
            CoreServiceCommandKind.PERMISSION_RESULT -> permission_result != null
            CoreServiceCommandKind.START_AUTH -> start_auth != null
            CoreServiceCommandKind.REQUEST_EMAIL_LINK -> request_email_link != null
            CoreServiceCommandKind.SIGN_OUT -> sign_out != null
            CoreServiceCommandKind.AUTH_CREDENTIAL_RESULT -> auth_credential_result != null
            CoreServiceCommandKind.CORRECT_WORKSPACE_FACT -> correct_workspace_fact != null
            CoreServiceCommandKind.EXCLUDE_WORKSPACE_SOURCE -> exclude_workspace_source != null
            CoreServiceCommandKind.REQUEST_WORKSPACE_EXPORT -> request_workspace_export != null
            CoreServiceCommandKind.SET_ASSET_DELIVERY_POLICY -> set_asset_delivery_policy != null
            CoreServiceCommandKind.REQUEST_ASSET -> request_asset != null
            CoreServiceCommandKind.REMOVE_ASSET -> remove_asset != null
            CoreServiceCommandKind.SAVE_PROVIDER_CREDENTIAL -> save_provider_credential != null
            CoreServiceCommandKind.FORGET_PROVIDER_CREDENTIAL -> forget_provider_credential != null
            CoreServiceCommandKind.START_PROVIDER_AUTH -> start_provider_auth != null
            CoreServiceCommandKind.PROVIDER_AUTH_CALLBACK -> provider_auth_callback != null
            CoreServiceCommandKind.SAVE_CUSTOM_PROVIDER -> save_custom_provider != null
            CoreServiceCommandKind.REMOVE_CUSTOM_PROVIDER -> remove_custom_provider != null
            CoreServiceCommandKind.COMPLETE_HANDOVER -> complete_handover != null
            CoreServiceCommandKind.EXPIRE_HANDOVER -> expire_handover != null
            CoreServiceCommandKind.SUPPLY_USER_INPUT -> supply_user_input != null
            CoreServiceCommandKind.SET_PROVIDER_CREDENTIAL_STATE -> set_provider_credential_state != null
            CoreServiceCommandKind.PROBE_PROVIDER_CREDENTIAL -> probe_provider_credential != null
            CoreServiceCommandKind.SUPPLY_FIELD_VALUES -> supply_field_values != null
            CoreServiceCommandKind.SET_PROVIDER_MODEL_PREFERENCE -> set_provider_model_preference != null
            CoreServiceCommandKind.PROBE_CUSTOM_ENDPOINT -> probe_custom_endpoint != null
            CoreServiceCommandKind.REQUEST_COMPOSER_COMPLETION -> request_composer_completion != null
            CoreServiceCommandKind.CANCEL_COMPOSER_COMPLETION -> cancel_composer_completion != null
            CoreServiceCommandKind.PAUSE_TASK -> pause_task != null
            CoreServiceCommandKind.RESUME_TASK -> resume_task != null
            CoreServiceCommandKind.TAKE_OVER -> take_over != null
            CoreServiceCommandKind.SET_ASSISTANT_CONFIGURATION -> set_assistant_configuration != null
            CoreServiceCommandKind.SAVE_WORKSPACE -> save_workspace != null
            CoreServiceCommandKind.RENAME_WORKSPACE -> rename_workspace != null
            CoreServiceCommandKind.DELETE_WORKSPACE -> delete_workspace != null
            CoreServiceCommandKind.DISCARD_WORKSPACE -> discard_workspace != null
            CoreServiceCommandKind.SEARCH_LIBRARY -> search_library != null
            CoreServiceCommandKind.SAVE_LIBRARY_FACT -> save_library_fact != null
            CoreServiceCommandKind.REMOVE_LIBRARY_ENTRY -> remove_library_entry != null
            CoreServiceCommandKind.REQUEST_LIBRARY_EXPORT -> request_library_export != null
            CoreServiceCommandKind.SEARCH_MEMORY -> search_memory != null
            CoreServiceCommandKind.UPSERT_MEMORY -> upsert_memory != null
            CoreServiceCommandKind.DELETE_MEMORY -> delete_memory != null
            CoreServiceCommandKind.ACCEPT_TASK_ARTIFACT -> accept_task_artifact != null
            CoreServiceCommandKind.EXPORT_TASK_ARTIFACT -> export_task_artifact != null
            CoreServiceCommandKind.REPLACE_SAVED_DATA_SNAPSHOT -> replace_saved_data_snapshot != null
            CoreServiceCommandKind.MUTATE_SKILL -> mutate_skill != null
            CoreServiceCommandKind.CANCEL_PROVIDER_AUTH -> cancel_provider_auth != null
            CoreServiceCommandKind.FOLLOW_UP -> follow_up != null
        }
    }

    companion object {
        fun startTask(operation: OperationEnvelope, body: StartTaskCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.START_TASK,
                start_task = body,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun cancelTask(operation: OperationEnvelope, body: CancelTaskCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.CANCEL_TASK,
                start_task = null,
                cancel_task = body,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun userDecision(operation: OperationEnvelope, body: UserDecisionCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.USER_DECISION,
                start_task = null,
                cancel_task = null,
                user_decision = body,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun authCallback(operation: OperationEnvelope, body: AuthCallbackCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.AUTH_CALLBACK,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = body,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun permissionResult(operation: OperationEnvelope, body: PermissionResultCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.PERMISSION_RESULT,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = body,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun startAuth(operation: OperationEnvelope, body: StartAuthCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.START_AUTH,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = body,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun requestEmailLink(operation: OperationEnvelope, body: RequestEmailLinkCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.REQUEST_EMAIL_LINK,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = body,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun signOut(operation: OperationEnvelope, body: SignOutCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.SIGN_OUT,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = body,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun authCredentialResult(operation: OperationEnvelope, body: AuthCredentialResultCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.AUTH_CREDENTIAL_RESULT,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = body,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun correctWorkspaceFact(operation: OperationEnvelope, body: CorrectWorkspaceFactCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.CORRECT_WORKSPACE_FACT,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = body,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun excludeWorkspaceSource(operation: OperationEnvelope, body: ExcludeWorkspaceSourceCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.EXCLUDE_WORKSPACE_SOURCE,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = body,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun requestWorkspaceExport(operation: OperationEnvelope, body: RequestWorkspaceExportCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.REQUEST_WORKSPACE_EXPORT,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = body,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun setAssetDeliveryPolicy(operation: OperationEnvelope, body: SetAssetDeliveryPolicyCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.SET_ASSET_DELIVERY_POLICY,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = body,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun requestAsset(operation: OperationEnvelope, body: RequestAssetCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.REQUEST_ASSET,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = body,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun removeAsset(operation: OperationEnvelope, body: RemoveAssetCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.REMOVE_ASSET,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = body,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun saveProviderCredential(operation: OperationEnvelope, body: SaveProviderCredentialCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.SAVE_PROVIDER_CREDENTIAL,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = body,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun forgetProviderCredential(operation: OperationEnvelope, body: ForgetProviderCredentialCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.FORGET_PROVIDER_CREDENTIAL,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = body,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun startProviderAuth(operation: OperationEnvelope, body: StartProviderAuthCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.START_PROVIDER_AUTH,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = body,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun providerAuthCallback(operation: OperationEnvelope, body: ProviderAuthCallbackCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.PROVIDER_AUTH_CALLBACK,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = body,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun saveCustomProvider(operation: OperationEnvelope, body: SaveCustomProviderCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.SAVE_CUSTOM_PROVIDER,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = body,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun removeCustomProvider(operation: OperationEnvelope, body: RemoveCustomProviderCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.REMOVE_CUSTOM_PROVIDER,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = body,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun completeHandover(operation: OperationEnvelope, body: CompleteHandoverCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.COMPLETE_HANDOVER,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = body,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun expireHandover(operation: OperationEnvelope, body: ExpireHandoverCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.EXPIRE_HANDOVER,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = body,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun supplyUserInput(operation: OperationEnvelope, body: SupplyUserInputCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.SUPPLY_USER_INPUT,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = body,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun setProviderCredentialState(operation: OperationEnvelope, body: SetProviderCredentialStateCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.SET_PROVIDER_CREDENTIAL_STATE,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = body,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun probeProviderCredential(operation: OperationEnvelope, body: ProbeProviderCredentialCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.PROBE_PROVIDER_CREDENTIAL,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = body,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun supplyFieldValues(operation: OperationEnvelope, body: SupplyFieldValuesCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.SUPPLY_FIELD_VALUES,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = body,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun setProviderModelPreference(operation: OperationEnvelope, body: SetProviderModelPreferenceCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.SET_PROVIDER_MODEL_PREFERENCE,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun probeCustomEndpoint(operation: OperationEnvelope, body: ProbeCustomEndpointCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.PROBE_CUSTOM_ENDPOINT,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun requestComposerCompletion(operation: OperationEnvelope, body: RequestComposerCompletionCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.REQUEST_COMPOSER_COMPLETION,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun cancelComposerCompletion(operation: OperationEnvelope, body: CancelComposerCompletionCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.CANCEL_COMPOSER_COMPLETION,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun pauseTask(operation: OperationEnvelope, body: PauseTaskCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.PAUSE_TASK,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun resumeTask(operation: OperationEnvelope, body: ResumeTaskCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.RESUME_TASK,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun takeOver(operation: OperationEnvelope, body: TakeOverCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.TAKE_OVER,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun setAssistantConfiguration(operation: OperationEnvelope, body: SetAssistantConfigurationCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.SET_ASSISTANT_CONFIGURATION,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun saveWorkspace(operation: OperationEnvelope, body: SaveWorkspaceCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.SAVE_WORKSPACE,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun renameWorkspace(operation: OperationEnvelope, body: RenameWorkspaceCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.RENAME_WORKSPACE,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun deleteWorkspace(operation: OperationEnvelope, body: DeleteWorkspaceCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.DELETE_WORKSPACE,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun discardWorkspace(operation: OperationEnvelope, body: DiscardWorkspaceCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.DISCARD_WORKSPACE,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun searchLibrary(operation: OperationEnvelope, body: SearchLibraryCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.SEARCH_LIBRARY,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun saveLibraryFact(operation: OperationEnvelope, body: SaveLibraryFactCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.SAVE_LIBRARY_FACT,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun removeLibraryEntry(operation: OperationEnvelope, body: RemoveLibraryEntryCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.REMOVE_LIBRARY_ENTRY,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun requestLibraryExport(operation: OperationEnvelope, body: RequestLibraryExportCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.REQUEST_LIBRARY_EXPORT,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun searchMemory(operation: OperationEnvelope, body: SearchMemoryCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.SEARCH_MEMORY,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun upsertMemory(operation: OperationEnvelope, body: UpsertMemoryCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.UPSERT_MEMORY,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun deleteMemory(operation: OperationEnvelope, body: DeleteMemoryCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.DELETE_MEMORY,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun acceptTaskArtifact(operation: OperationEnvelope, body: AcceptTaskArtifactCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.ACCEPT_TASK_ARTIFACT,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun exportTaskArtifact(operation: OperationEnvelope, body: ExportTaskArtifactCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.EXPORT_TASK_ARTIFACT,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = body,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun replaceSavedDataSnapshot(operation: OperationEnvelope, body: ReplaceSavedDataSnapshotCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.REPLACE_SAVED_DATA_SNAPSHOT,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = body,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun mutateSkill(operation: OperationEnvelope, body: MutateSkillCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.MUTATE_SKILL,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = body,
                cancel_provider_auth = null,
                follow_up = null,
            )

        fun cancelProviderAuth(operation: OperationEnvelope, body: CancelProviderAuthCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.CANCEL_PROVIDER_AUTH,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = body,
                follow_up = null,
            )

        fun followUp(operation: OperationEnvelope, body: FollowUpCommand): CoreServiceCommand =
            CoreServiceCommand(
                operation = operation,
                kind = CoreServiceCommandKind.FOLLOW_UP,
                start_task = null,
                cancel_task = null,
                user_decision = null,
                auth_callback = null,
                permission_result = null,
                start_auth = null,
                request_email_link = null,
                sign_out = null,
                auth_credential_result = null,
                correct_workspace_fact = null,
                exclude_workspace_source = null,
                request_workspace_export = null,
                set_asset_delivery_policy = null,
                request_asset = null,
                remove_asset = null,
                save_provider_credential = null,
                forget_provider_credential = null,
                start_provider_auth = null,
                provider_auth_callback = null,
                save_custom_provider = null,
                remove_custom_provider = null,
                complete_handover = null,
                expire_handover = null,
                supply_user_input = null,
                set_provider_credential_state = null,
                probe_provider_credential = null,
                supply_field_values = null,
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
                export_task_artifact = null,
                replace_saved_data_snapshot = null,
                mutate_skill = null,
                cancel_provider_auth = null,
                follow_up = body,
            )

    }
}

data class CoreBootstrapResult(
    val status: InitializationStatus,
    val accepted_generation: ULong
)

data class Admission(
    val operation_id: String,
    val status: AdmissionStatus
)

data class ModelArtifactRegistration(
    val asset_id: String,
    val asset_revision: String,
    val asset_kind: AssetKind,
    val format: ToolModelArtifactKind,
    val adapter: Boolean,
    val byte_length: ULong,
    val digest: ByteArray
)

data class CoreStateUpdate(
    val service_generation: ULong,
    val sequence: ULong,
    val core_status_schema_version: UInt,
    val payload: ByteArray,
    val model_artifacts: List<ModelArtifactRegistration>
)

data class PendingApprovalBinding(
    val task_id: String,
    val action_id: String,
    val proposal_digest: String,
    val service_generation: ULong,
    val task_revision: ULong
)

data class TaskRevisionBinding(
    val task_id: String,
    val service_generation: ULong,
    val task_revision: ULong,
    val allowed_controls: List<TaskControlKind>
)

data class TaskSettlementBinding(
    val task_id: String,
    val service_generation: ULong,
    val task_revision: ULong,
    val kind: TaskSettlementKind
)

data class TerminalTaskBinding(
    val task_id: String,
    val service_generation: ULong,
    val task_revision: ULong,
    val kind: TerminalTaskKind
)

data class PendingPermissionBinding(
    val task_id: String,
    val request_id: String,
    val permission: PlatformPermission,
    val service_generation: ULong,
    val task_revision: ULong,
    val deadline_monotonic_ms: ULong,
    val deadline_utc_ms: ULong,
    val browser_session_id: String
)

data class AcceptedTaskConsentBinding(
    val task_id: String,
    val service_generation: ULong,
    val current_task_revision: ULong,
    val accepted_revision: ULong,
    val browser_session_id: String,
    val receipt_id: String,
    val consent_preview: TaskConsentPreview
)

data class CommittedActionApprovalBinding(
    val task_id: String,
    val action_id: String,
    val service_generation: ULong,
    val committed_revision: ULong,
    val receipt_id: String,
    val proposal_digest: String,
    val expires_at_monotonic_ms: ULong,
    val expires_at_utc_ms: ULong,
    val browser_session_id: String
)

data class CoreStateBrowserBindings(
    val service_generation: ULong,
    val state_sequence: ULong,
    val task_revisions: List<TaskRevisionBinding>,
    val pending_approvals: List<PendingApprovalBinding>,
    val task_settlements: List<TaskSettlementBinding>,
    val pending_permissions: List<PendingPermissionBinding>,
    val terminal_tasks: List<TerminalTaskBinding>,
    val accepted_task_consents: List<AcceptedTaskConsentBinding>,
    val committed_action_approvals: List<CommittedActionApprovalBinding>
)

data class WorkspacePersistEffect(
    val workspace_id: String,
    val snapshot: ByteArray,
    val expected_revision: ULong,
    val resulting_revision: ULong
)

data class WorkspaceDeletionEffect(
    val workspace_id: String,
    val expected_revision: ULong,
    val resulting_revision: ULong,
    val sources: UInt,
    val facts: UInt,
    val artifact_metadata: UInt,
    val derived_indexes: UInt,
    val confirmation_token: String
)

data class SkillInstallEffect(
    val skill_id: String,
    val origin: String,
    val provenance: SkillProvenance,
    val version: UInt,
    val definition: ByteArray,
    val step_count: UInt,
    val recorded_at_utc_ms: ULong
)

data class SkillStatusEffect(
    val skill_id: String,
    val status: SkillStatus,
    val changed_at_utc_ms: ULong,
    val version: UInt
)

data class SkillRunEffect(
    val skill_id: String,
    val version: UInt,
    val task_id: String,
    val outcome: SkillRunOutcome,
    val ran_at_utc_ms: ULong
)

data class SkillForgetEffect(
    val skill_id: String
)

data class SourceDeletionEffect(
    val source_id: String,
    val origin: String
)

data class AssistantConfigurationPersistEffect(
    val disabled_abilities: List<AssistantAbility>,
    val preset: PersonalityPreset,
    val pace: UInt,
    val length: UInt,
    val check_in: UInt
)

data class LibraryPersistEffect(
    val entry: LibraryEntryRecord,
    val expected_entry_revision: ULong
)

data class LibraryDeletionEffect(
    val entry_id: String,
    val expected_entry_revision: ULong,
    val resulting_entry_revision: ULong,
    val removed_at_epoch_ms: ULong
)

data class MemoryPersistEffect(
    val record: MemoryRecord,
    val expected_record_revision: ULong
)

data class MemoryDeletionEffect(
    val memory_id: String,
    val expected_record_revision: ULong,
    val resulting_record_revision: ULong,
    val deleted_at_epoch_ms: ULong
)

data class StorageCommitEffect(
    val operation_kind: StorageOperation,
    val task_id: String,
    val expected_revision: ULong,
    val resulting_revision: ULong,
    val transaction_batch: ByteArray,
    val task_id_seed: ByteArray,
    val workspace: WorkspacePersistEffect?,
    val install_skill: SkillInstallEffect?,
    val skill_status: SkillStatusEffect?,
    val skill_run: SkillRunEffect?,
    val forget_skill: SkillForgetEffect?,
    val source_deletion: SourceDeletionEffect?,
    val assistant_configuration: AssistantConfigurationPersistEffect?,
    val workspace_deletion: WorkspaceDeletionEffect?,
    val library_entry: LibraryPersistEffect?,
    val library_deletion: LibraryDeletionEffect?,
    val memory_record: MemoryPersistEffect?,
    val memory_deletion: MemoryDeletionEffect?
)

data class PageObservationEffect(
    val tab_id: String,
    val frame_id: String,
    val page_epoch: String,
    val scope: ObservationScope,
    val max_bytes: UInt,
    val task_id: String,
    val action_id: String,
    val capability_id: String,
    val proposal_digest: String,
    val idempotency_key: String,
    val authority_subject: AuthoritySubject,
    val max_nodes: UInt,
    val max_text_bytes: UInt,
    val max_frames: UInt,
    val deadline_ms: UInt,
    val expected_graph_revision: ULong
)

data class ModelStaticHeader(
    val name: String,
    val value: String
)

data class ModelRequestEffect(
    val route_id: String,
    val model_id: String,
    val disclosure: DisclosureClass,
    val request_body: ByteArray,
    val max_output_bytes: UInt,
    val task_id: String,
    val provider_id: String,
    val wire_api: ProviderWireApi,
    val endpoint: String,
    val credential_handle: String?,
    val static_headers: List<ModelStaticHeader>,
    val probe: Boolean,
    val endpoint_kind: ModelEndpointKind,
    val media_attachment_handle: String?,
    val media_attachment_mime_type: String?,
    val not_before_monotonic_ms: ULong
)

data class ExchangeAuthorizationCodeRequest(
    val flow_id: String,
    val auth_method: AccountAuthMethod,
    val authorization_code_handle: String,
    val pkce_verifier_handle: String,
    val redirect_binding_id: String
)

data class ExchangeNativeCredentialRequest(
    val flow_id: String,
    val auth_method: AccountAuthMethod,
    val credential_handle: String,
    val raw_nonce_handle: String
)

data class EmailLinkNetworkRequest(
    val flow_id: String,
    val email: String,
    val pkce_verifier_handle: String,
    val redirect_binding_id: String,
    val pkce_challenge: String,
    val state: String
)

data class RefreshSessionRequest(
    val session_handle: String,
    val expected_rotation: ULong,
    val expected_account_subject: String,
    val expected_auth_method: AccountAuthMethod
)

data class RevokeSessionRequest(
    val session_handle: String
)

data class FetchEntitlementRequest(
    val reason: EntitlementFetchReason
)

data class NetworkRequestEffect(
    val operation_kind: AccountNetworkOperation,
    val exchange_authorization_code: ExchangeAuthorizationCodeRequest?,
    val exchange_native_credential: ExchangeNativeCredentialRequest?,
    val request_email_link: EmailLinkNetworkRequest?,
    val refresh_session: RefreshSessionRequest?,
    val revoke_session: RevokeSessionRequest?,
    val max_response_bytes: UInt,
    val fetch_entitlement: FetchEntitlementRequest?
) {
    fun hasValidBody(): Boolean {
        val bodyCount = listOf(exchange_authorization_code, exchange_native_credential, request_email_link, refresh_session, revoke_session, fetch_entitlement).count { it != null }
        if (bodyCount != 1) return false
        return when (operation_kind) {
            AccountNetworkOperation.EXCHANGE_AUTHORIZATION_CODE -> exchange_authorization_code != null
            AccountNetworkOperation.EXCHANGE_NATIVE_CREDENTIAL -> exchange_native_credential != null
            AccountNetworkOperation.REQUEST_EMAIL_LINK -> request_email_link != null
            AccountNetworkOperation.REFRESH_SESSION -> refresh_session != null
            AccountNetworkOperation.REVOKE_SESSION -> revoke_session != null
            AccountNetworkOperation.FETCH_ENTITLEMENT -> fetch_entitlement != null
        }
    }

    companion object {
        fun exchangeAuthorizationCode(max_response_bytes: UInt, body: ExchangeAuthorizationCodeRequest): NetworkRequestEffect =
            NetworkRequestEffect(
                operation_kind = AccountNetworkOperation.EXCHANGE_AUTHORIZATION_CODE,
                exchange_authorization_code = body,
                exchange_native_credential = null,
                request_email_link = null,
                refresh_session = null,
                revoke_session = null,
                max_response_bytes = max_response_bytes,
                fetch_entitlement = null,
            )

        fun exchangeNativeCredential(max_response_bytes: UInt, body: ExchangeNativeCredentialRequest): NetworkRequestEffect =
            NetworkRequestEffect(
                operation_kind = AccountNetworkOperation.EXCHANGE_NATIVE_CREDENTIAL,
                exchange_authorization_code = null,
                exchange_native_credential = body,
                request_email_link = null,
                refresh_session = null,
                revoke_session = null,
                max_response_bytes = max_response_bytes,
                fetch_entitlement = null,
            )

        fun requestEmailLink(max_response_bytes: UInt, body: EmailLinkNetworkRequest): NetworkRequestEffect =
            NetworkRequestEffect(
                operation_kind = AccountNetworkOperation.REQUEST_EMAIL_LINK,
                exchange_authorization_code = null,
                exchange_native_credential = null,
                request_email_link = body,
                refresh_session = null,
                revoke_session = null,
                max_response_bytes = max_response_bytes,
                fetch_entitlement = null,
            )

        fun refreshSession(max_response_bytes: UInt, body: RefreshSessionRequest): NetworkRequestEffect =
            NetworkRequestEffect(
                operation_kind = AccountNetworkOperation.REFRESH_SESSION,
                exchange_authorization_code = null,
                exchange_native_credential = null,
                request_email_link = null,
                refresh_session = body,
                revoke_session = null,
                max_response_bytes = max_response_bytes,
                fetch_entitlement = null,
            )

        fun revokeSession(max_response_bytes: UInt, body: RevokeSessionRequest): NetworkRequestEffect =
            NetworkRequestEffect(
                operation_kind = AccountNetworkOperation.REVOKE_SESSION,
                exchange_authorization_code = null,
                exchange_native_credential = null,
                request_email_link = null,
                refresh_session = null,
                revoke_session = body,
                max_response_bytes = max_response_bytes,
                fetch_entitlement = null,
            )

        fun fetchEntitlement(max_response_bytes: UInt, body: FetchEntitlementRequest): NetworkRequestEffect =
            NetworkRequestEffect(
                operation_kind = AccountNetworkOperation.FETCH_ENTITLEMENT,
                exchange_authorization_code = null,
                exchange_native_credential = null,
                request_email_link = null,
                refresh_session = null,
                revoke_session = null,
                max_response_bytes = max_response_bytes,
                fetch_entitlement = body,
            )

    }
}

data class BrowserActionEffect(
    val operation_kind: BrowserActionOperation,
    val action_id: String,
    val grant_reference: String,
    val approval_digest: String,
    val origin_scope_digest: String
)

data class ToolModelArtifact(
    val model_id: String,
    val model_revision: String,
    val kind: ToolModelArtifactKind,
    val artifact_bytes: ULong,
    val artifact_digest: ByteArray,
    val adapter_id: String,
    val adapter_bytes: ULong,
    val adapter_digest: ByteArray
)

data class ToolResourceBudget(
    val max_input_bytes: ULong,
    val max_output_bytes: ULong,
    val max_memory_bytes: ULong,
    val max_cpu_ms: ULong,
    val max_temporary_bytes: ULong,
    val max_output_chunks: UInt
)

data class BundledPythonArguments(
    val entrypoint_id: String,
    val input: ByteArray
)

data class LocalModelArguments(
    val conversation_id: String,
    val prompt_utf8: ByteArray,
    val max_output_tokens: UInt,
    val model: ToolModelArtifact
)

data class LocalEmbeddingArguments(
    val model: ToolModelArtifact,
    val input_utf8: ByteArray,
    val max_input_tokens: UInt,
    val normalize: Boolean
)

data class MediaProbeArguments(
    val input_handle: String
)

data class AudioExtractArguments(
    val input_handle: String,
    val output_handle: String,
    val preset_id: String
)

data class FrameSampleArguments(
    val input_handle: String,
    val output_handle: String,
    val preset_id: String,
    val max_frames: UInt
)

data class TranscodeArguments(
    val input_handle: String,
    val output_handle: String,
    val preset_id: String
)

data class SignedWasmArguments(
    val entrypoint_id: String,
    val input: ByteArray
)

data class ToolJobEffect(
    val job_id: String,
    val runtime: ToolRuntimeKind,
    val tool_id: String,
    val tool_version: String,
    val operation_kind: ToolOperation,
    val budget: ToolResourceBudget,
    val bundled_python: BundledPythonArguments?,
    val local_model: LocalModelArguments?,
    val media_probe: MediaProbeArguments?,
    val audio_extract: AudioExtractArguments?,
    val frame_sample: FrameSampleArguments?,
    val transcode: TranscodeArguments?,
    val signed_wasm: SignedWasmArguments?,
    val local_embedding: LocalEmbeddingArguments?,
    val task_id: String
) {
    fun hasValidBody(): Boolean {
        val bodyCount = listOf(bundled_python, local_model, media_probe, audio_extract, frame_sample, transcode, signed_wasm, local_embedding).count { it != null }
        if (bodyCount != 1) return false
        val bodyMatches = when (operation_kind) {
            ToolOperation.RUN_BUNDLED_PYTHON_MODULE -> bundled_python != null
            ToolOperation.GENERATE_LOCAL_MODEL -> local_model != null
            ToolOperation.PROBE_MEDIA -> media_probe != null
            ToolOperation.EXTRACT_AUDIO -> audio_extract != null
            ToolOperation.SAMPLE_FRAMES -> frame_sample != null
            ToolOperation.TRANSCODE_PRESET -> transcode != null
            ToolOperation.RUN_SIGNED_WASM_TRANSFORM -> signed_wasm != null
            ToolOperation.EMBED_LOCAL_MODEL -> local_embedding != null
        }
        if (!bodyMatches) return false
        return when (runtime) {
            ToolRuntimeKind.PYTHON -> operation_kind == ToolOperation.RUN_BUNDLED_PYTHON_MODULE
            ToolRuntimeKind.LOCAL_MODEL -> operation_kind == ToolOperation.GENERATE_LOCAL_MODEL || operation_kind == ToolOperation.EMBED_LOCAL_MODEL
            ToolRuntimeKind.MEDIA -> operation_kind == ToolOperation.PROBE_MEDIA || operation_kind == ToolOperation.EXTRACT_AUDIO || operation_kind == ToolOperation.SAMPLE_FRAMES || operation_kind == ToolOperation.TRANSCODE_PRESET
            ToolRuntimeKind.WASM -> operation_kind == ToolOperation.RUN_SIGNED_WASM_TRANSFORM
        }
    }

    companion object {
        fun bundledPython(job_id: String, runtime: ToolRuntimeKind, tool_id: String, tool_version: String, budget: ToolResourceBudget, task_id: String, body: BundledPythonArguments): ToolJobEffect =
            ToolJobEffect(
                job_id = job_id,
                runtime = runtime,
                tool_id = tool_id,
                tool_version = tool_version,
                operation_kind = ToolOperation.RUN_BUNDLED_PYTHON_MODULE,
                budget = budget,
                bundled_python = body,
                local_model = null,
                media_probe = null,
                audio_extract = null,
                frame_sample = null,
                transcode = null,
                signed_wasm = null,
                local_embedding = null,
                task_id = task_id,
            )

        fun localModel(job_id: String, runtime: ToolRuntimeKind, tool_id: String, tool_version: String, budget: ToolResourceBudget, task_id: String, body: LocalModelArguments): ToolJobEffect =
            ToolJobEffect(
                job_id = job_id,
                runtime = runtime,
                tool_id = tool_id,
                tool_version = tool_version,
                operation_kind = ToolOperation.GENERATE_LOCAL_MODEL,
                budget = budget,
                bundled_python = null,
                local_model = body,
                media_probe = null,
                audio_extract = null,
                frame_sample = null,
                transcode = null,
                signed_wasm = null,
                local_embedding = null,
                task_id = task_id,
            )

        fun mediaProbe(job_id: String, runtime: ToolRuntimeKind, tool_id: String, tool_version: String, budget: ToolResourceBudget, task_id: String, body: MediaProbeArguments): ToolJobEffect =
            ToolJobEffect(
                job_id = job_id,
                runtime = runtime,
                tool_id = tool_id,
                tool_version = tool_version,
                operation_kind = ToolOperation.PROBE_MEDIA,
                budget = budget,
                bundled_python = null,
                local_model = null,
                media_probe = body,
                audio_extract = null,
                frame_sample = null,
                transcode = null,
                signed_wasm = null,
                local_embedding = null,
                task_id = task_id,
            )

        fun audioExtract(job_id: String, runtime: ToolRuntimeKind, tool_id: String, tool_version: String, budget: ToolResourceBudget, task_id: String, body: AudioExtractArguments): ToolJobEffect =
            ToolJobEffect(
                job_id = job_id,
                runtime = runtime,
                tool_id = tool_id,
                tool_version = tool_version,
                operation_kind = ToolOperation.EXTRACT_AUDIO,
                budget = budget,
                bundled_python = null,
                local_model = null,
                media_probe = null,
                audio_extract = body,
                frame_sample = null,
                transcode = null,
                signed_wasm = null,
                local_embedding = null,
                task_id = task_id,
            )

        fun frameSample(job_id: String, runtime: ToolRuntimeKind, tool_id: String, tool_version: String, budget: ToolResourceBudget, task_id: String, body: FrameSampleArguments): ToolJobEffect =
            ToolJobEffect(
                job_id = job_id,
                runtime = runtime,
                tool_id = tool_id,
                tool_version = tool_version,
                operation_kind = ToolOperation.SAMPLE_FRAMES,
                budget = budget,
                bundled_python = null,
                local_model = null,
                media_probe = null,
                audio_extract = null,
                frame_sample = body,
                transcode = null,
                signed_wasm = null,
                local_embedding = null,
                task_id = task_id,
            )

        fun transcode(job_id: String, runtime: ToolRuntimeKind, tool_id: String, tool_version: String, budget: ToolResourceBudget, task_id: String, body: TranscodeArguments): ToolJobEffect =
            ToolJobEffect(
                job_id = job_id,
                runtime = runtime,
                tool_id = tool_id,
                tool_version = tool_version,
                operation_kind = ToolOperation.TRANSCODE_PRESET,
                budget = budget,
                bundled_python = null,
                local_model = null,
                media_probe = null,
                audio_extract = null,
                frame_sample = null,
                transcode = body,
                signed_wasm = null,
                local_embedding = null,
                task_id = task_id,
            )

        fun signedWasm(job_id: String, runtime: ToolRuntimeKind, tool_id: String, tool_version: String, budget: ToolResourceBudget, task_id: String, body: SignedWasmArguments): ToolJobEffect =
            ToolJobEffect(
                job_id = job_id,
                runtime = runtime,
                tool_id = tool_id,
                tool_version = tool_version,
                operation_kind = ToolOperation.RUN_SIGNED_WASM_TRANSFORM,
                budget = budget,
                bundled_python = null,
                local_model = null,
                media_probe = null,
                audio_extract = null,
                frame_sample = null,
                transcode = null,
                signed_wasm = body,
                local_embedding = null,
                task_id = task_id,
            )

        fun localEmbedding(job_id: String, runtime: ToolRuntimeKind, tool_id: String, tool_version: String, budget: ToolResourceBudget, task_id: String, body: LocalEmbeddingArguments): ToolJobEffect =
            ToolJobEffect(
                job_id = job_id,
                runtime = runtime,
                tool_id = tool_id,
                tool_version = tool_version,
                operation_kind = ToolOperation.EMBED_LOCAL_MODEL,
                budget = budget,
                bundled_python = null,
                local_model = null,
                media_probe = null,
                audio_extract = null,
                frame_sample = null,
                transcode = null,
                signed_wasm = null,
                local_embedding = body,
                task_id = task_id,
            )

    }
}

data class GenerateEntropyRequest(
    val flow_id: String,
    val byte_count: UInt
)

data class WriteTransientSecretRequest(
    val flow_id: String,
    val purpose: SecretMaterialPurpose,
    val material: ByteArray
)

data class DeleteSecretHandleRequest(
    val secret_handle: String
)

data class SecureStoreEffect(
    val operation_kind: SecureStoreOperation,
    val generate_entropy: GenerateEntropyRequest?,
    val write_transient: WriteTransientSecretRequest?,
    val delete_handle: DeleteSecretHandleRequest?
) {
    fun hasValidBody(): Boolean {
        val bodyCount = listOf(generate_entropy, write_transient, delete_handle).count { it != null }
        if (bodyCount != 1) return false
        return when (operation_kind) {
            SecureStoreOperation.GENERATE_ENTROPY -> generate_entropy != null
            SecureStoreOperation.WRITE_TRANSIENT -> write_transient != null
            SecureStoreOperation.DELETE_HANDLE -> delete_handle != null
        }
    }

    companion object {
        fun generateEntropy(body: GenerateEntropyRequest): SecureStoreEffect =
            SecureStoreEffect(
                operation_kind = SecureStoreOperation.GENERATE_ENTROPY,
                generate_entropy = body,
                write_transient = null,
                delete_handle = null,
            )

        fun writeTransient(body: WriteTransientSecretRequest): SecureStoreEffect =
            SecureStoreEffect(
                operation_kind = SecureStoreOperation.WRITE_TRANSIENT,
                generate_entropy = null,
                write_transient = body,
                delete_handle = null,
            )

        fun deleteHandle(body: DeleteSecretHandleRequest): SecureStoreEffect =
            SecureStoreEffect(
                operation_kind = SecureStoreOperation.DELETE_HANDLE,
                generate_entropy = null,
                write_transient = null,
                delete_handle = body,
            )

    }
}

data class OAuthSurfaceRequest(
    val flow_id: String,
    val auth_method: AccountAuthMethod,
    val redirect_binding_id: String,
    val pkce_challenge: String,
    val state: String,
    val scopes: List<AccountScope>,
    val pkce_verifier_handle: String
)

data class NativeCredentialSurfaceRequest(
    val flow_id: String,
    val auth_method: AccountAuthMethod,
    val raw_nonce_handle: String,
    val hashed_nonce: String
)

data class AuthSurfaceEffect(
    val operation_kind: AuthSurfaceOperation,
    val oauth: OAuthSurfaceRequest?,
    val native_credential: NativeCredentialSurfaceRequest?
) {
    fun hasValidBody(): Boolean {
        val bodyCount = listOf(oauth, native_credential).count { it != null }
        if (bodyCount != 1) return false
        return when (operation_kind) {
            AuthSurfaceOperation.OPEN_OAUTH -> oauth != null
            AuthSurfaceOperation.REQUEST_NATIVE_CREDENTIAL -> native_credential != null
        }
    }

    companion object {
        fun oauth(body: OAuthSurfaceRequest): AuthSurfaceEffect =
            AuthSurfaceEffect(
                operation_kind = AuthSurfaceOperation.OPEN_OAUTH,
                oauth = body,
                native_credential = null,
            )

        fun nativeCredential(body: NativeCredentialSurfaceRequest): AuthSurfaceEffect =
            AuthSurfaceEffect(
                operation_kind = AuthSurfaceOperation.REQUEST_NATIVE_CREDENTIAL,
                oauth = null,
                native_credential = body,
            )

    }
}

data class PermissionRequestEffect(
    val request_id: String,
    val permission: PlatformPermission,
    val task_id: String,
    val task_revision: ULong,
    val deadline_monotonic_ms: ULong,
    val deadline_utc_ms: ULong,
    val browser_session_id: String
)

data class ProviderListingFetchEffect(
    val provider_id: String,
    val endpoint: String,
    val wire_api: ProviderWireApi,
    val credential_handle: String?,
    val max_response_bytes: UInt
)

data class ProviderListingFetchResult(
    val provider_id: String,
    val disposition: CatalogFetchDisposition,
    val body: ByteArray
)

data class CustomEndpointProbeEffect(
    val provider_id: String,
    val endpoint: String,
    val wire_api: ProviderWireApi,
    val credential_handle: String?,
    val max_response_bytes: UInt
)

data class CustomEndpointProbeResult(
    val provider_id: String,
    val reached: Boolean,
    val detected_server: DetectedServer?,
    val model_count: UInt,
    val models: List<CustomModelSpec>,
    val proved_base: String?
)

data class ComposerCompletionEffect(
    val request_id: String,
    val text: String?
)

data class ComposerCompletionEffectResult(
    val request_id: String,
    val delivered: Boolean
)

data class EffectEnvelope(
    val operation: OperationEnvelope,
    val effect_id: String,
    val kind: EffectKind,
    val retry_class: RetryClass,
    val storage_commit: StorageCommitEffect?,
    val page_observation: PageObservationEffect?,
    val model_request: ModelRequestEffect?,
    val network_request: NetworkRequestEffect?,
    val browser_action: BrowserActionEffect?,
    val tool_job: ToolJobEffect?,
    val secure_store: SecureStoreEffect?,
    val auth_surface: AuthSurfaceEffect?,
    val permission_request: PermissionRequestEffect?,
    val asset_delivery: AssetDeliveryEffect?,
    val catalog_fetch: CatalogFetchEffect?,
    val provider_listing_fetch: ProviderListingFetchEffect?,
    val composer_completion: ComposerCompletionEffect?,
    val custom_endpoint_probe: CustomEndpointProbeEffect?
) {
    fun hasValidBody(): Boolean {
        val bodyCount = listOf(storage_commit, page_observation, model_request, network_request, browser_action, tool_job, secure_store, auth_surface, permission_request, asset_delivery, catalog_fetch, provider_listing_fetch, composer_completion, custom_endpoint_probe).count { it != null }
        if (bodyCount != 1) return false
        return when (kind) {
            EffectKind.STORAGE_COMMIT -> storage_commit != null
            EffectKind.PAGE_OBSERVATION -> page_observation != null
            EffectKind.MODEL_REQUEST -> model_request != null
            EffectKind.NETWORK_REQUEST -> network_request != null
            EffectKind.BROWSER_ACTION -> browser_action != null
            EffectKind.TOOL_JOB -> tool_job != null
            EffectKind.SECURE_STORE -> secure_store != null
            EffectKind.OPEN_AUTH_SURFACE -> auth_surface != null
            EffectKind.REQUEST_PERMISSION -> permission_request != null
            EffectKind.DELIVER_ASSET -> asset_delivery != null
            EffectKind.FETCH_CATALOG -> catalog_fetch != null
            EffectKind.FETCH_PROVIDER_LISTING -> provider_listing_fetch != null
            EffectKind.DELIVER_COMPOSER_COMPLETION -> composer_completion != null
            EffectKind.PROBE_CUSTOM_ENDPOINT -> custom_endpoint_probe != null
        }
    }

    companion object {
        fun storageCommit(operation: OperationEnvelope, effect_id: String, retry_class: RetryClass, body: StorageCommitEffect): EffectEnvelope =
            EffectEnvelope(
                operation = operation,
                effect_id = effect_id,
                kind = EffectKind.STORAGE_COMMIT,
                retry_class = retry_class,
                storage_commit = body,
                page_observation = null,
                model_request = null,
                network_request = null,
                browser_action = null,
                tool_job = null,
                secure_store = null,
                auth_surface = null,
                permission_request = null,
                asset_delivery = null,
                catalog_fetch = null,
                provider_listing_fetch = null,
                composer_completion = null,
                custom_endpoint_probe = null,
            )

        fun pageObservation(operation: OperationEnvelope, effect_id: String, retry_class: RetryClass, body: PageObservationEffect): EffectEnvelope =
            EffectEnvelope(
                operation = operation,
                effect_id = effect_id,
                kind = EffectKind.PAGE_OBSERVATION,
                retry_class = retry_class,
                storage_commit = null,
                page_observation = body,
                model_request = null,
                network_request = null,
                browser_action = null,
                tool_job = null,
                secure_store = null,
                auth_surface = null,
                permission_request = null,
                asset_delivery = null,
                catalog_fetch = null,
                provider_listing_fetch = null,
                composer_completion = null,
                custom_endpoint_probe = null,
            )

        fun modelRequest(operation: OperationEnvelope, effect_id: String, retry_class: RetryClass, body: ModelRequestEffect): EffectEnvelope =
            EffectEnvelope(
                operation = operation,
                effect_id = effect_id,
                kind = EffectKind.MODEL_REQUEST,
                retry_class = retry_class,
                storage_commit = null,
                page_observation = null,
                model_request = body,
                network_request = null,
                browser_action = null,
                tool_job = null,
                secure_store = null,
                auth_surface = null,
                permission_request = null,
                asset_delivery = null,
                catalog_fetch = null,
                provider_listing_fetch = null,
                composer_completion = null,
                custom_endpoint_probe = null,
            )

        fun networkRequest(operation: OperationEnvelope, effect_id: String, retry_class: RetryClass, body: NetworkRequestEffect): EffectEnvelope =
            EffectEnvelope(
                operation = operation,
                effect_id = effect_id,
                kind = EffectKind.NETWORK_REQUEST,
                retry_class = retry_class,
                storage_commit = null,
                page_observation = null,
                model_request = null,
                network_request = body,
                browser_action = null,
                tool_job = null,
                secure_store = null,
                auth_surface = null,
                permission_request = null,
                asset_delivery = null,
                catalog_fetch = null,
                provider_listing_fetch = null,
                composer_completion = null,
                custom_endpoint_probe = null,
            )

        fun browserAction(operation: OperationEnvelope, effect_id: String, retry_class: RetryClass, body: BrowserActionEffect): EffectEnvelope =
            EffectEnvelope(
                operation = operation,
                effect_id = effect_id,
                kind = EffectKind.BROWSER_ACTION,
                retry_class = retry_class,
                storage_commit = null,
                page_observation = null,
                model_request = null,
                network_request = null,
                browser_action = body,
                tool_job = null,
                secure_store = null,
                auth_surface = null,
                permission_request = null,
                asset_delivery = null,
                catalog_fetch = null,
                provider_listing_fetch = null,
                composer_completion = null,
                custom_endpoint_probe = null,
            )

        fun toolJob(operation: OperationEnvelope, effect_id: String, retry_class: RetryClass, body: ToolJobEffect): EffectEnvelope =
            EffectEnvelope(
                operation = operation,
                effect_id = effect_id,
                kind = EffectKind.TOOL_JOB,
                retry_class = retry_class,
                storage_commit = null,
                page_observation = null,
                model_request = null,
                network_request = null,
                browser_action = null,
                tool_job = body,
                secure_store = null,
                auth_surface = null,
                permission_request = null,
                asset_delivery = null,
                catalog_fetch = null,
                provider_listing_fetch = null,
                composer_completion = null,
                custom_endpoint_probe = null,
            )

        fun secureStore(operation: OperationEnvelope, effect_id: String, retry_class: RetryClass, body: SecureStoreEffect): EffectEnvelope =
            EffectEnvelope(
                operation = operation,
                effect_id = effect_id,
                kind = EffectKind.SECURE_STORE,
                retry_class = retry_class,
                storage_commit = null,
                page_observation = null,
                model_request = null,
                network_request = null,
                browser_action = null,
                tool_job = null,
                secure_store = body,
                auth_surface = null,
                permission_request = null,
                asset_delivery = null,
                catalog_fetch = null,
                provider_listing_fetch = null,
                composer_completion = null,
                custom_endpoint_probe = null,
            )

        fun authSurface(operation: OperationEnvelope, effect_id: String, retry_class: RetryClass, body: AuthSurfaceEffect): EffectEnvelope =
            EffectEnvelope(
                operation = operation,
                effect_id = effect_id,
                kind = EffectKind.OPEN_AUTH_SURFACE,
                retry_class = retry_class,
                storage_commit = null,
                page_observation = null,
                model_request = null,
                network_request = null,
                browser_action = null,
                tool_job = null,
                secure_store = null,
                auth_surface = body,
                permission_request = null,
                asset_delivery = null,
                catalog_fetch = null,
                provider_listing_fetch = null,
                composer_completion = null,
                custom_endpoint_probe = null,
            )

        fun permissionRequest(operation: OperationEnvelope, effect_id: String, retry_class: RetryClass, body: PermissionRequestEffect): EffectEnvelope =
            EffectEnvelope(
                operation = operation,
                effect_id = effect_id,
                kind = EffectKind.REQUEST_PERMISSION,
                retry_class = retry_class,
                storage_commit = null,
                page_observation = null,
                model_request = null,
                network_request = null,
                browser_action = null,
                tool_job = null,
                secure_store = null,
                auth_surface = null,
                permission_request = body,
                asset_delivery = null,
                catalog_fetch = null,
                provider_listing_fetch = null,
                composer_completion = null,
                custom_endpoint_probe = null,
            )

        fun assetDelivery(operation: OperationEnvelope, effect_id: String, retry_class: RetryClass, body: AssetDeliveryEffect): EffectEnvelope =
            EffectEnvelope(
                operation = operation,
                effect_id = effect_id,
                kind = EffectKind.DELIVER_ASSET,
                retry_class = retry_class,
                storage_commit = null,
                page_observation = null,
                model_request = null,
                network_request = null,
                browser_action = null,
                tool_job = null,
                secure_store = null,
                auth_surface = null,
                permission_request = null,
                asset_delivery = body,
                catalog_fetch = null,
                provider_listing_fetch = null,
                composer_completion = null,
                custom_endpoint_probe = null,
            )

        fun catalogFetch(operation: OperationEnvelope, effect_id: String, retry_class: RetryClass, body: CatalogFetchEffect): EffectEnvelope =
            EffectEnvelope(
                operation = operation,
                effect_id = effect_id,
                kind = EffectKind.FETCH_CATALOG,
                retry_class = retry_class,
                storage_commit = null,
                page_observation = null,
                model_request = null,
                network_request = null,
                browser_action = null,
                tool_job = null,
                secure_store = null,
                auth_surface = null,
                permission_request = null,
                asset_delivery = null,
                catalog_fetch = body,
                provider_listing_fetch = null,
                composer_completion = null,
                custom_endpoint_probe = null,
            )

        fun providerListingFetch(operation: OperationEnvelope, effect_id: String, retry_class: RetryClass, body: ProviderListingFetchEffect): EffectEnvelope =
            EffectEnvelope(
                operation = operation,
                effect_id = effect_id,
                kind = EffectKind.FETCH_PROVIDER_LISTING,
                retry_class = retry_class,
                storage_commit = null,
                page_observation = null,
                model_request = null,
                network_request = null,
                browser_action = null,
                tool_job = null,
                secure_store = null,
                auth_surface = null,
                permission_request = null,
                asset_delivery = null,
                catalog_fetch = null,
                provider_listing_fetch = body,
                composer_completion = null,
                custom_endpoint_probe = null,
            )

        fun composerCompletion(operation: OperationEnvelope, effect_id: String, retry_class: RetryClass, body: ComposerCompletionEffect): EffectEnvelope =
            EffectEnvelope(
                operation = operation,
                effect_id = effect_id,
                kind = EffectKind.DELIVER_COMPOSER_COMPLETION,
                retry_class = retry_class,
                storage_commit = null,
                page_observation = null,
                model_request = null,
                network_request = null,
                browser_action = null,
                tool_job = null,
                secure_store = null,
                auth_surface = null,
                permission_request = null,
                asset_delivery = null,
                catalog_fetch = null,
                provider_listing_fetch = null,
                composer_completion = body,
                custom_endpoint_probe = null,
            )

        fun customEndpointProbe(operation: OperationEnvelope, effect_id: String, retry_class: RetryClass, body: CustomEndpointProbeEffect): EffectEnvelope =
            EffectEnvelope(
                operation = operation,
                effect_id = effect_id,
                kind = EffectKind.PROBE_CUSTOM_ENDPOINT,
                retry_class = retry_class,
                storage_commit = null,
                page_observation = null,
                model_request = null,
                network_request = null,
                browser_action = null,
                tool_job = null,
                secure_store = null,
                auth_surface = null,
                permission_request = null,
                asset_delivery = null,
                catalog_fetch = null,
                provider_listing_fetch = null,
                composer_completion = null,
                custom_endpoint_probe = body,
            )

    }
}

data class StorageEffectResult(
    val committed_revision: ULong
)

data class MediaObservationFact(
    val kind: MediaFactKind,
    val evidence: MediaEvidenceKind,
    val text: String,
    val source_locator: String,
    val source_start: UInt,
    val source_end: UInt,
    val page_index_plus_one: UInt,
    val timestamp_start_ms: ULong,
    val timestamp_end_ms: ULong,
    val row_index_plus_one: UInt,
    val confidence_ppm: UInt,
    val truncated: Boolean
)

data class MediaCaptureProvenance(
    val capture_x_dip: UInt,
    val capture_y_dip: UInt,
    val capture_width_dip: UInt,
    val capture_height_dip: UInt,
    val viewport_width_dip: UInt,
    val viewport_height_dip: UInt,
    val output_scale_ppm: UInt,
    val captured_at_monotonic_ms: ULong,
    val redacted_region_count: UInt
)

data class MediaObservationResult(
    val kind: MediaObservationKind,
    val facts: List<MediaObservationFact>,
    val attachment_handle: String?,
    val attachment_mime_type: String?,
    val width_px: UInt,
    val height_px: UInt,
    val has_meaningful_text: Boolean,
    val scanned_pdf_ocr_required: Boolean,
    val capture_provenance: MediaCaptureProvenance?
)

data class ObservationEffectResult(
    val status: BipObservationStatus,
    val schema_version: String,
    val tab_id: String,
    val frame_id: String,
    val page_epoch: String,
    val graph_revision: ULong,
    val origin: String,
    val is_potentially_trustworthy: Boolean,
    val private_profile: Boolean,
    val node_count: UInt,
    val total_bytes: UInt,
    val truncated: Boolean,
    val may_change_answer: Boolean,
    val redacted_field_count: UInt,
    val suppressed_secret_value_count: UInt,
    val sensitive_zone_count: UInt,
    val policy_filtered_frame_count: UInt,
    val highest_sensitivity: BipSensitivity,
    val graph_encoding: BipGraphEncoding,
    val graph_payload: ByteArray,
    val media: MediaObservationResult?
)

data class PageSnapshotExportCommand(
    val operation: OperationEnvelope,
    val format: PageSnapshotExportFormat,
    val expected_tab_id: String,
    val expected_frame_id: String,
    val expected_page_epoch: String,
    val expected_graph_revision: ULong,
    val expected_origin: String,
    val max_bytes: UInt,
    val observation: ObservationEffectResult,
    val captured_at_epoch_ms: ULong,
    val source_query_withheld: Boolean,
    val source_fragment_withheld: Boolean
)

data class PageSnapshotExportResult(
    val operation: OperationEnvelope,
    val status: PageSnapshotExportStatus,
    val format: PageSnapshotExportFormat,
    val tab_id: String,
    val frame_id: String,
    val page_epoch: String,
    val graph_revision: ULong,
    val origin: String,
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

data class SiteSkillMatchCommand(
    val operation: OperationEnvelope,
    val expected_tab_id: String,
    val expected_frame_id: String,
    val expected_page_epoch: String,
    val expected_graph_revision: ULong,
    val expected_origin: String,
    val observation: ObservationEffectResult
)

data class SiteSkillMatchOffer(
    val skill_version_id: String,
    val skill_id: String,
    val active_version: UInt,
    val step_count: UInt
)

data class SiteSkillMatchResult(
    val operation: OperationEnvelope,
    val status: SiteSkillMatchStatus,
    val tab_id: String,
    val frame_id: String,
    val page_epoch: String,
    val graph_revision: ULong,
    val origin: String,
    val offers: List<SiteSkillMatchOffer>
)

data class ModelStreamChunk(
    val operation: OperationEnvelope,
    val effect_id: String,
    val sequence: UInt,
    val data: ByteArray
)

data class TaskAnswerEvent(
    val task_id: String,
    val call_id: String,
    val sequence: UInt,
    val text: String?,
    val terminal: Boolean,
    val complete: Boolean
)

data class ModelFailure(
    val error_class: ModelErrorClass,
    val has_retry_after: Boolean,
    val retry_after_millis: ULong
)

data class ModelEffectResult(
    val model_id: String,
    val completion: ByteArray,
    val input_units: ULong,
    val output_units: ULong,
    val provider_http_status: UInt,
    val streamed: Boolean,
    val failure: ModelFailure?
)

data class AccountTokenValidationRequest(
    val operation: OperationEnvelope,
    val operation_kind: AccountNetworkOperation,
    val expected_auth_method: AccountAuthMethod,
    val expected_account_subject: String?,
    val target_rotation: ULong,
    val response_body: ByteArray
)

data class AccountTokenValidationResult(
    val status: AccountTokenValidationStatus,
    val operation_id: String,
    val operation_kind: AccountNetworkOperation,
    val auth_method: AccountAuthMethod,
    val account_subject: String,
    val expires_in_seconds: ULong,
    val target_rotation: ULong,
    val access_token: ByteArray,
    val refresh_token: ByteArray,
    val email: String?,
    val display_name: String?
)

data class AccountSessionReceipt(
    val session_handle: String,
    val account_subject: String,
    val expires_at_monotonic_ms: ULong,
    val rotation: ULong,
    val auth_method: AccountAuthMethod,
    val email: String?,
    val display_name: String?
)

data class EntitlementSummaryResult(
    val definitive_absent: Boolean,
    val plan_id: String,
    val model_ids: List<String>,
    val window_seconds: UInt,
    val requests_remaining: ULong,
    val credits_granted: ULong,
    val credits_remaining: ULong,
    val credit_unit_micros: ULong,
    val next_renewal_epoch_seconds: ULong,
    val valid_until_epoch_seconds: ULong,
    val minted_at_utc_ms: ULong,
    val worker_host: String,
    val gateway_host: String
)

data class EmailLinkNetworkResult(
    val flow_id: String,
    val accepted: Boolean
)

data class RevokedSessionResult(
    val session_handle: String,
    val deleted: Boolean
)

data class NetworkEffectResult(
    val operation_kind: AccountNetworkOperation,
    val authorization_code_session: AccountSessionReceipt?,
    val native_credential_session: AccountSessionReceipt?,
    val email_link: EmailLinkNetworkResult?,
    val refreshed_session: AccountSessionReceipt?,
    val revoked_session: RevokedSessionResult?,
    val entitlement_summary: EntitlementSummaryResult?
) {
    fun hasValidBody(): Boolean {
        val bodyCount = listOf(authorization_code_session, native_credential_session, email_link, refreshed_session, revoked_session, entitlement_summary).count { it != null }
        if (bodyCount != 1) return false
        return when (operation_kind) {
            AccountNetworkOperation.EXCHANGE_AUTHORIZATION_CODE -> authorization_code_session != null
            AccountNetworkOperation.EXCHANGE_NATIVE_CREDENTIAL -> native_credential_session != null
            AccountNetworkOperation.REQUEST_EMAIL_LINK -> email_link != null
            AccountNetworkOperation.REFRESH_SESSION -> refreshed_session != null
            AccountNetworkOperation.REVOKE_SESSION -> revoked_session != null
            AccountNetworkOperation.FETCH_ENTITLEMENT -> entitlement_summary != null
        }
    }

    companion object {
        fun authorizationCodeSession(body: AccountSessionReceipt): NetworkEffectResult =
            NetworkEffectResult(
                operation_kind = AccountNetworkOperation.EXCHANGE_AUTHORIZATION_CODE,
                authorization_code_session = body,
                native_credential_session = null,
                email_link = null,
                refreshed_session = null,
                revoked_session = null,
                entitlement_summary = null,
            )

        fun nativeCredentialSession(body: AccountSessionReceipt): NetworkEffectResult =
            NetworkEffectResult(
                operation_kind = AccountNetworkOperation.EXCHANGE_NATIVE_CREDENTIAL,
                authorization_code_session = null,
                native_credential_session = body,
                email_link = null,
                refreshed_session = null,
                revoked_session = null,
                entitlement_summary = null,
            )

        fun emailLink(body: EmailLinkNetworkResult): NetworkEffectResult =
            NetworkEffectResult(
                operation_kind = AccountNetworkOperation.REQUEST_EMAIL_LINK,
                authorization_code_session = null,
                native_credential_session = null,
                email_link = body,
                refreshed_session = null,
                revoked_session = null,
                entitlement_summary = null,
            )

        fun refreshedSession(body: AccountSessionReceipt): NetworkEffectResult =
            NetworkEffectResult(
                operation_kind = AccountNetworkOperation.REFRESH_SESSION,
                authorization_code_session = null,
                native_credential_session = null,
                email_link = null,
                refreshed_session = body,
                revoked_session = null,
                entitlement_summary = null,
            )

        fun revokedSession(body: RevokedSessionResult): NetworkEffectResult =
            NetworkEffectResult(
                operation_kind = AccountNetworkOperation.REVOKE_SESSION,
                authorization_code_session = null,
                native_credential_session = null,
                email_link = null,
                refreshed_session = null,
                revoked_session = body,
                entitlement_summary = null,
            )

        fun entitlementSummary(body: EntitlementSummaryResult): NetworkEffectResult =
            NetworkEffectResult(
                operation_kind = AccountNetworkOperation.FETCH_ENTITLEMENT,
                authorization_code_session = null,
                native_credential_session = null,
                email_link = null,
                refreshed_session = null,
                revoked_session = null,
                entitlement_summary = body,
            )

    }
}

data class TaskTabSnapshot(
    val target: TaskTabDocumentTarget,
    val active: Boolean
)

data class TaskTabActionResult(
    val browser_session_id: String,
    val operation_kind: TaskActionOperationKind,
    val postcondition: TaskTabPostcondition,
    val tabs: List<TaskTabSnapshot>,
    val target: TaskTabDocumentTarget?,
    val state_was_already_satisfied: Boolean
)

data class TaskDownloadSnapshot(
    val download_id: String,
    val state: TaskDownloadState,
    val media_type: TaskDownloadMediaType,
    val received_bytes: ULong,
    val directory_class: TaskDownloadDirectoryClass
)

data class TaskDownloadActionResult(
    val browser_session_id: String,
    val operation_kind: TaskActionOperationKind,
    val postcondition: TaskDownloadPostcondition,
    val downloads: List<TaskDownloadSnapshot>,
    val truncated: Boolean
)

data class TaskActionRefusal(
    val code: TaskActionResultCode
)

data class BrowserActionEffectResult(
    val outcome: BrowserActionOutcome,
    val dispatch_id: String?,
    val discovered_source: TaskConsentSource?,
    val discovery_tab_id: String?,
    val browser_session_id: String?,
    val task_tab: TaskTabActionResult?,
    val task_download: TaskDownloadActionResult?,
    val task_store: TaskStoreActionResult?,
    val refused_code: TaskActionRefusal?
)

data class TaskStoreRow(
    val title: String,
    val host: String,
    val path: String,
    val when_utc_ms: ULong
)

data class TaskStoreActionResult(
    val operation_kind: TaskActionOperationKind,
    val rows: List<TaskStoreRow>,
    val omitted: UInt
)

data class ToolProgress(
    val job_id: String,
    val sequence: UInt,
    val progress_basis_points: UInt
)

data class TextOutputChunk(
    val utf8: ByteArray
)

data class BinaryOutputChunk(
    val data: ByteArray
)

data class ToolOutputChunk(
    val job_id: String,
    val sequence: UInt,
    val kind: ToolChunkKind,
    val text: TextOutputChunk?,
    val binary: BinaryOutputChunk?,
    val is_final: Boolean
) {
    fun hasValidBody(): Boolean {
        val bodyCount = listOf(text, binary).count { it != null }
        if (bodyCount != 1) return false
        return when (kind) {
            ToolChunkKind.TEXT_UTF8 -> text != null
            ToolChunkKind.BINARY -> binary != null
        }
    }

    companion object {
        fun text(job_id: String, sequence: UInt, is_final: Boolean, body: TextOutputChunk): ToolOutputChunk =
            ToolOutputChunk(
                job_id = job_id,
                sequence = sequence,
                kind = ToolChunkKind.TEXT_UTF8,
                text = body,
                binary = null,
                is_final = is_final,
            )

        fun binary(job_id: String, sequence: UInt, is_final: Boolean, body: BinaryOutputChunk): ToolOutputChunk =
            ToolOutputChunk(
                job_id = job_id,
                sequence = sequence,
                kind = ToolChunkKind.BINARY,
                text = null,
                binary = body,
                is_final = is_final,
            )

    }
}

data class BundledPythonResult(
    val output: ByteArray
)

data class LocalModelResult(
    val finish_reason: LocalModelFinishReason,
    val input_tokens: UInt,
    val output_tokens: UInt
)

data class LocalEmbeddingResult(
    val dimensions: UInt,
    val element_kind: EmbeddingElementKind,
    val values: ByteArray,
    val input_tokens: UInt
)

data class MediaProbeResult(
    val duration_ms: ULong,
    val audio_streams: UInt,
    val video_streams: UInt,
    val width: UInt,
    val height: UInt
)

data class AudioExtractResult(
    val output_handle: String,
    val output_bytes: ULong,
    val output_digest: ByteArray
)

data class FrameSampleResult(
    val output_handle: String,
    val frame_count: UInt,
    val output_bytes: ULong,
    val output_digest: ByteArray
)

data class TranscodeResult(
    val output_handle: String,
    val output_bytes: ULong,
    val output_digest: ByteArray
)

data class SignedWasmResult(
    val output: ByteArray
)

data class ToolSuccess(
    val operation_kind: ToolOperation,
    val bundled_python: BundledPythonResult?,
    val local_model: LocalModelResult?,
    val media_probe: MediaProbeResult?,
    val audio_extract: AudioExtractResult?,
    val frame_sample: FrameSampleResult?,
    val transcode: TranscodeResult?,
    val signed_wasm: SignedWasmResult?,
    val local_embedding: LocalEmbeddingResult?
) {
    fun hasValidBody(): Boolean {
        val bodyCount = listOf(bundled_python, local_model, media_probe, audio_extract, frame_sample, transcode, signed_wasm, local_embedding).count { it != null }
        if (bodyCount != 1) return false
        return when (operation_kind) {
            ToolOperation.RUN_BUNDLED_PYTHON_MODULE -> bundled_python != null
            ToolOperation.GENERATE_LOCAL_MODEL -> local_model != null
            ToolOperation.PROBE_MEDIA -> media_probe != null
            ToolOperation.EXTRACT_AUDIO -> audio_extract != null
            ToolOperation.SAMPLE_FRAMES -> frame_sample != null
            ToolOperation.TRANSCODE_PRESET -> transcode != null
            ToolOperation.RUN_SIGNED_WASM_TRANSFORM -> signed_wasm != null
            ToolOperation.EMBED_LOCAL_MODEL -> local_embedding != null
        }
    }

    companion object {
        fun bundledPython(body: BundledPythonResult): ToolSuccess =
            ToolSuccess(
                operation_kind = ToolOperation.RUN_BUNDLED_PYTHON_MODULE,
                bundled_python = body,
                local_model = null,
                media_probe = null,
                audio_extract = null,
                frame_sample = null,
                transcode = null,
                signed_wasm = null,
                local_embedding = null,
            )

        fun localModel(body: LocalModelResult): ToolSuccess =
            ToolSuccess(
                operation_kind = ToolOperation.GENERATE_LOCAL_MODEL,
                bundled_python = null,
                local_model = body,
                media_probe = null,
                audio_extract = null,
                frame_sample = null,
                transcode = null,
                signed_wasm = null,
                local_embedding = null,
            )

        fun mediaProbe(body: MediaProbeResult): ToolSuccess =
            ToolSuccess(
                operation_kind = ToolOperation.PROBE_MEDIA,
                bundled_python = null,
                local_model = null,
                media_probe = body,
                audio_extract = null,
                frame_sample = null,
                transcode = null,
                signed_wasm = null,
                local_embedding = null,
            )

        fun audioExtract(body: AudioExtractResult): ToolSuccess =
            ToolSuccess(
                operation_kind = ToolOperation.EXTRACT_AUDIO,
                bundled_python = null,
                local_model = null,
                media_probe = null,
                audio_extract = body,
                frame_sample = null,
                transcode = null,
                signed_wasm = null,
                local_embedding = null,
            )

        fun frameSample(body: FrameSampleResult): ToolSuccess =
            ToolSuccess(
                operation_kind = ToolOperation.SAMPLE_FRAMES,
                bundled_python = null,
                local_model = null,
                media_probe = null,
                audio_extract = null,
                frame_sample = body,
                transcode = null,
                signed_wasm = null,
                local_embedding = null,
            )

        fun transcode(body: TranscodeResult): ToolSuccess =
            ToolSuccess(
                operation_kind = ToolOperation.TRANSCODE_PRESET,
                bundled_python = null,
                local_model = null,
                media_probe = null,
                audio_extract = null,
                frame_sample = null,
                transcode = body,
                signed_wasm = null,
                local_embedding = null,
            )

        fun signedWasm(body: SignedWasmResult): ToolSuccess =
            ToolSuccess(
                operation_kind = ToolOperation.RUN_SIGNED_WASM_TRANSFORM,
                bundled_python = null,
                local_model = null,
                media_probe = null,
                audio_extract = null,
                frame_sample = null,
                transcode = null,
                signed_wasm = body,
                local_embedding = null,
            )

        fun localEmbedding(body: LocalEmbeddingResult): ToolSuccess =
            ToolSuccess(
                operation_kind = ToolOperation.EMBED_LOCAL_MODEL,
                bundled_python = null,
                local_model = null,
                media_probe = null,
                audio_extract = null,
                frame_sample = null,
                transcode = null,
                signed_wasm = null,
                local_embedding = body,
            )

    }
}

data class ToolEffectResult(
    val job_id: String,
    val status: ToolTerminalStatus,
    val progress: List<ToolProgress>,
    val chunks: List<ToolOutputChunk>,
    val success: ToolSuccess?,
    val streamed_chunks: UInt
)

fun ToolEffectResult.hasValidPresence(): Boolean =
    (if (status == ToolTerminalStatus.COMPLETED) success != null else success == null)

data class ToolStreamChunk(
    val operation: OperationEnvelope,
    val effect_id: String,
    val job_id: String,
    val chunk: ToolOutputChunk
)

data class GeneratedEntropyResult(
    val flow_id: String,
    val entropy: ByteArray
)

data class TransientSecretWriteResult(
    val flow_id: String,
    val purpose: SecretMaterialPurpose,
    val secret_handle: String
)

data class DeletedSecretHandleResult(
    val secret_handle: String,
    val deleted: Boolean
)

data class SecureStoreEffectResult(
    val operation_kind: SecureStoreOperation,
    val generated_entropy: GeneratedEntropyResult?,
    val transient_write: TransientSecretWriteResult?,
    val deleted_handle: DeletedSecretHandleResult?
) {
    fun hasValidBody(): Boolean {
        val bodyCount = listOf(generated_entropy, transient_write, deleted_handle).count { it != null }
        if (bodyCount != 1) return false
        return when (operation_kind) {
            SecureStoreOperation.GENERATE_ENTROPY -> generated_entropy != null
            SecureStoreOperation.WRITE_TRANSIENT -> transient_write != null
            SecureStoreOperation.DELETE_HANDLE -> deleted_handle != null
        }
    }

    companion object {
        fun generatedEntropy(body: GeneratedEntropyResult): SecureStoreEffectResult =
            SecureStoreEffectResult(
                operation_kind = SecureStoreOperation.GENERATE_ENTROPY,
                generated_entropy = body,
                transient_write = null,
                deleted_handle = null,
            )

        fun transientWrite(body: TransientSecretWriteResult): SecureStoreEffectResult =
            SecureStoreEffectResult(
                operation_kind = SecureStoreOperation.WRITE_TRANSIENT,
                generated_entropy = null,
                transient_write = body,
                deleted_handle = null,
            )

        fun deletedHandle(body: DeletedSecretHandleResult): SecureStoreEffectResult =
            SecureStoreEffectResult(
                operation_kind = SecureStoreOperation.DELETE_HANDLE,
                generated_entropy = null,
                transient_write = null,
                deleted_handle = body,
            )

    }
}

data class OAuthSurfaceResult(
    val flow_id: String,
    val opened: Boolean
)

data class NativeCredentialSurfaceResult(
    val flow_id: String,
    val opened: Boolean
)

data class AuthSurfaceEffectResult(
    val operation_kind: AuthSurfaceOperation,
    val oauth: OAuthSurfaceResult?,
    val native_credential: NativeCredentialSurfaceResult?
) {
    fun hasValidBody(): Boolean {
        val bodyCount = listOf(oauth, native_credential).count { it != null }
        if (bodyCount != 1) return false
        return when (operation_kind) {
            AuthSurfaceOperation.OPEN_OAUTH -> oauth != null
            AuthSurfaceOperation.REQUEST_NATIVE_CREDENTIAL -> native_credential != null
        }
    }

    companion object {
        fun oauth(body: OAuthSurfaceResult): AuthSurfaceEffectResult =
            AuthSurfaceEffectResult(
                operation_kind = AuthSurfaceOperation.OPEN_OAUTH,
                oauth = body,
                native_credential = null,
            )

        fun nativeCredential(body: NativeCredentialSurfaceResult): AuthSurfaceEffectResult =
            AuthSurfaceEffectResult(
                operation_kind = AuthSurfaceOperation.REQUEST_NATIVE_CREDENTIAL,
                oauth = null,
                native_credential = body,
            )

    }
}

data class PermissionEffectResult(
    val request_id: String,
    val permission: PlatformPermission,
    val decision: PermissionDecision
)

data class EffectResult(
    val operation: OperationEnvelope,
    val effect_id: String,
    val status: EffectStatus,
    val kind: EffectKind,
    val storage: StorageEffectResult?,
    val observation: ObservationEffectResult?,
    val model: ModelEffectResult?,
    val network: NetworkEffectResult?,
    val browser_action: BrowserActionEffectResult?,
    val tool: ToolEffectResult?,
    val secure_store: SecureStoreEffectResult?,
    val auth_surface: AuthSurfaceEffectResult?,
    val permission: PermissionEffectResult?,
    val asset_delivery: AssetDeliveryEffectResult?,
    val catalog: CatalogFetchEffectResult?,
    val provider_listing: ProviderListingFetchResult?,
    val composer_completion: ComposerCompletionEffectResult?,
    val custom_endpoint_probe: CustomEndpointProbeResult?
) {
    fun hasValidBody(): Boolean {
        val bodyCount = listOf(storage, observation, model, network, browser_action, tool, secure_store, auth_surface, permission, asset_delivery, catalog, provider_listing, composer_completion, custom_endpoint_probe).count { it != null }
        if (bodyCount != 1) return false
        return when (kind) {
            EffectKind.STORAGE_COMMIT -> storage != null
            EffectKind.PAGE_OBSERVATION -> observation != null
            EffectKind.MODEL_REQUEST -> model != null
            EffectKind.NETWORK_REQUEST -> network != null
            EffectKind.BROWSER_ACTION -> browser_action != null
            EffectKind.TOOL_JOB -> tool != null
            EffectKind.SECURE_STORE -> secure_store != null
            EffectKind.OPEN_AUTH_SURFACE -> auth_surface != null
            EffectKind.REQUEST_PERMISSION -> permission != null
            EffectKind.DELIVER_ASSET -> asset_delivery != null
            EffectKind.FETCH_CATALOG -> catalog != null
            EffectKind.FETCH_PROVIDER_LISTING -> provider_listing != null
            EffectKind.DELIVER_COMPOSER_COMPLETION -> composer_completion != null
            EffectKind.PROBE_CUSTOM_ENDPOINT -> custom_endpoint_probe != null
        }
    }

    companion object {
        fun storage(operation: OperationEnvelope, effect_id: String, status: EffectStatus, body: StorageEffectResult): EffectResult =
            EffectResult(
                operation = operation,
                effect_id = effect_id,
                status = status,
                kind = EffectKind.STORAGE_COMMIT,
                storage = body,
                observation = null,
                model = null,
                network = null,
                browser_action = null,
                tool = null,
                secure_store = null,
                auth_surface = null,
                permission = null,
                asset_delivery = null,
                catalog = null,
                provider_listing = null,
                composer_completion = null,
                custom_endpoint_probe = null,
            )

        fun observation(operation: OperationEnvelope, effect_id: String, status: EffectStatus, body: ObservationEffectResult): EffectResult =
            EffectResult(
                operation = operation,
                effect_id = effect_id,
                status = status,
                kind = EffectKind.PAGE_OBSERVATION,
                storage = null,
                observation = body,
                model = null,
                network = null,
                browser_action = null,
                tool = null,
                secure_store = null,
                auth_surface = null,
                permission = null,
                asset_delivery = null,
                catalog = null,
                provider_listing = null,
                composer_completion = null,
                custom_endpoint_probe = null,
            )

        fun model(operation: OperationEnvelope, effect_id: String, status: EffectStatus, body: ModelEffectResult): EffectResult =
            EffectResult(
                operation = operation,
                effect_id = effect_id,
                status = status,
                kind = EffectKind.MODEL_REQUEST,
                storage = null,
                observation = null,
                model = body,
                network = null,
                browser_action = null,
                tool = null,
                secure_store = null,
                auth_surface = null,
                permission = null,
                asset_delivery = null,
                catalog = null,
                provider_listing = null,
                composer_completion = null,
                custom_endpoint_probe = null,
            )

        fun network(operation: OperationEnvelope, effect_id: String, status: EffectStatus, body: NetworkEffectResult): EffectResult =
            EffectResult(
                operation = operation,
                effect_id = effect_id,
                status = status,
                kind = EffectKind.NETWORK_REQUEST,
                storage = null,
                observation = null,
                model = null,
                network = body,
                browser_action = null,
                tool = null,
                secure_store = null,
                auth_surface = null,
                permission = null,
                asset_delivery = null,
                catalog = null,
                provider_listing = null,
                composer_completion = null,
                custom_endpoint_probe = null,
            )

        fun browserAction(operation: OperationEnvelope, effect_id: String, status: EffectStatus, body: BrowserActionEffectResult): EffectResult =
            EffectResult(
                operation = operation,
                effect_id = effect_id,
                status = status,
                kind = EffectKind.BROWSER_ACTION,
                storage = null,
                observation = null,
                model = null,
                network = null,
                browser_action = body,
                tool = null,
                secure_store = null,
                auth_surface = null,
                permission = null,
                asset_delivery = null,
                catalog = null,
                provider_listing = null,
                composer_completion = null,
                custom_endpoint_probe = null,
            )

        fun tool(operation: OperationEnvelope, effect_id: String, status: EffectStatus, body: ToolEffectResult): EffectResult =
            EffectResult(
                operation = operation,
                effect_id = effect_id,
                status = status,
                kind = EffectKind.TOOL_JOB,
                storage = null,
                observation = null,
                model = null,
                network = null,
                browser_action = null,
                tool = body,
                secure_store = null,
                auth_surface = null,
                permission = null,
                asset_delivery = null,
                catalog = null,
                provider_listing = null,
                composer_completion = null,
                custom_endpoint_probe = null,
            )

        fun secureStore(operation: OperationEnvelope, effect_id: String, status: EffectStatus, body: SecureStoreEffectResult): EffectResult =
            EffectResult(
                operation = operation,
                effect_id = effect_id,
                status = status,
                kind = EffectKind.SECURE_STORE,
                storage = null,
                observation = null,
                model = null,
                network = null,
                browser_action = null,
                tool = null,
                secure_store = body,
                auth_surface = null,
                permission = null,
                asset_delivery = null,
                catalog = null,
                provider_listing = null,
                composer_completion = null,
                custom_endpoint_probe = null,
            )

        fun authSurface(operation: OperationEnvelope, effect_id: String, status: EffectStatus, body: AuthSurfaceEffectResult): EffectResult =
            EffectResult(
                operation = operation,
                effect_id = effect_id,
                status = status,
                kind = EffectKind.OPEN_AUTH_SURFACE,
                storage = null,
                observation = null,
                model = null,
                network = null,
                browser_action = null,
                tool = null,
                secure_store = null,
                auth_surface = body,
                permission = null,
                asset_delivery = null,
                catalog = null,
                provider_listing = null,
                composer_completion = null,
                custom_endpoint_probe = null,
            )

        fun permission(operation: OperationEnvelope, effect_id: String, status: EffectStatus, body: PermissionEffectResult): EffectResult =
            EffectResult(
                operation = operation,
                effect_id = effect_id,
                status = status,
                kind = EffectKind.REQUEST_PERMISSION,
                storage = null,
                observation = null,
                model = null,
                network = null,
                browser_action = null,
                tool = null,
                secure_store = null,
                auth_surface = null,
                permission = body,
                asset_delivery = null,
                catalog = null,
                provider_listing = null,
                composer_completion = null,
                custom_endpoint_probe = null,
            )

        fun assetDelivery(operation: OperationEnvelope, effect_id: String, status: EffectStatus, body: AssetDeliveryEffectResult): EffectResult =
            EffectResult(
                operation = operation,
                effect_id = effect_id,
                status = status,
                kind = EffectKind.DELIVER_ASSET,
                storage = null,
                observation = null,
                model = null,
                network = null,
                browser_action = null,
                tool = null,
                secure_store = null,
                auth_surface = null,
                permission = null,
                asset_delivery = body,
                catalog = null,
                provider_listing = null,
                composer_completion = null,
                custom_endpoint_probe = null,
            )

        fun catalog(operation: OperationEnvelope, effect_id: String, status: EffectStatus, body: CatalogFetchEffectResult): EffectResult =
            EffectResult(
                operation = operation,
                effect_id = effect_id,
                status = status,
                kind = EffectKind.FETCH_CATALOG,
                storage = null,
                observation = null,
                model = null,
                network = null,
                browser_action = null,
                tool = null,
                secure_store = null,
                auth_surface = null,
                permission = null,
                asset_delivery = null,
                catalog = body,
                provider_listing = null,
                composer_completion = null,
                custom_endpoint_probe = null,
            )

        fun providerListing(operation: OperationEnvelope, effect_id: String, status: EffectStatus, body: ProviderListingFetchResult): EffectResult =
            EffectResult(
                operation = operation,
                effect_id = effect_id,
                status = status,
                kind = EffectKind.FETCH_PROVIDER_LISTING,
                storage = null,
                observation = null,
                model = null,
                network = null,
                browser_action = null,
                tool = null,
                secure_store = null,
                auth_surface = null,
                permission = null,
                asset_delivery = null,
                catalog = null,
                provider_listing = body,
                composer_completion = null,
                custom_endpoint_probe = null,
            )

        fun composerCompletion(operation: OperationEnvelope, effect_id: String, status: EffectStatus, body: ComposerCompletionEffectResult): EffectResult =
            EffectResult(
                operation = operation,
                effect_id = effect_id,
                status = status,
                kind = EffectKind.DELIVER_COMPOSER_COMPLETION,
                storage = null,
                observation = null,
                model = null,
                network = null,
                browser_action = null,
                tool = null,
                secure_store = null,
                auth_surface = null,
                permission = null,
                asset_delivery = null,
                catalog = null,
                provider_listing = null,
                composer_completion = body,
                custom_endpoint_probe = null,
            )

        fun customEndpointProbe(operation: OperationEnvelope, effect_id: String, status: EffectStatus, body: CustomEndpointProbeResult): EffectResult =
            EffectResult(
                operation = operation,
                effect_id = effect_id,
                status = status,
                kind = EffectKind.PROBE_CUSTOM_ENDPOINT,
                storage = null,
                observation = null,
                model = null,
                network = null,
                browser_action = null,
                tool = null,
                secure_store = null,
                auth_surface = null,
                permission = null,
                asset_delivery = null,
                catalog = null,
                provider_listing = null,
                composer_completion = null,
                custom_endpoint_probe = body,
            )

    }
}

data class BackupRecordDescriptor(
    val kind: BackupRecordKind,
    val stable_id: String,
    val revision: ULong,
    val schema_version: UInt,
    val state: BackupRecordState,
    val plaintext_bytes: ULong,
    val plaintext_sha256: ByteArray
)

data class BackupManifestPrepareRequest(
    val operation: OperationEnvelope,
    val backup_id: String,
    val source_installation_id: String,
    val created_at_utc: String,
    val selection: List<BackupRecordKind>,
    val records: List<BackupRecordDescriptor>
)

data class BackupManifestPrepareResult(
    val operation: OperationEnvelope,
    val status: BackupPlanningStatus,
    val manifest_plaintext: ByteArray,
    val snapshot_sha256: ByteArray,
    val payload_plaintext_bytes: ULong,
    val source_order: List<UInt>,
    val expected_sealed_chunks: UInt
)

data class BackupManifestInspectRequest(
    val operation: OperationEnvelope,
    val manifest_plaintext: ByteArray
)

data class BackupPayloadLayoutEntry(
    val state: BackupRecordState,
    val plaintext_bytes: ULong
)

data class BackupManifestInspectResult(
    val operation: OperationEnvelope,
    val status: BackupPlanningStatus,
    val backup_id: String,
    val source_installation_id: String,
    val created_at_utc: String,
    val selection: List<BackupRecordKind>,
    val record_count: UInt,
    val snapshot_sha256: ByteArray,
    val payload_plaintext_bytes: ULong,
    val records: List<BackupPayloadLayoutEntry>
)

data class StagedBackupRecord(
    val plaintext_bytes: ULong,
    val plaintext_sha256: ByteArray
)

data class BackupRestoreTarget(
    val kind: BackupRestoreTargetKind,
    val profile_id: String
)

data class BackupRestoreBinding(
    val planning_operation: OperationEnvelope,
    val owner_profile_id: String,
    val target: BackupRestoreTarget,
    val backup_id: String,
    val snapshot_sha256: ByteArray,
    val confirmation_sha256: ByteArray
)

data class BackupRestoreStageAuthorization(
    val binding: BackupRestoreBinding,
    val decision_operation: OperationEnvelope
)

data class BackupRestoreCommitAuthorization(
    val binding: BackupRestoreBinding,
    val decision_operation: OperationEnvelope
)

data class BackupRestoreResolutionAuthorization(
    val binding: BackupRestoreBinding,
    val decision_operation: OperationEnvelope,
    val choice: BackupRestoreResolutionChoice
)

data class BackupRestorePlanRequest(
    val operation: OperationEnvelope,
    val manifest_plaintext: ByteArray,
    val staged_records: List<StagedBackupRecord>,
    val current_records: List<BackupRecordDescriptor>,
    val target: BackupRestoreTarget
)

data class BackupRestorePlanEntry(
    val kind: BackupRecordKind,
    val stable_id: String,
    val archive_revision: ULong,
    val action: BackupRestoreAction,
    val schema_version: UInt,
    val state: BackupRecordState,
    val plaintext_bytes: ULong,
    val plaintext_sha256: ByteArray
)

data class BackupRestorePlanResult(
    val operation: OperationEnvelope,
    val status: BackupPlanningStatus,
    val backup_id: String,
    val snapshot_sha256: ByteArray,
    val target: BackupRestoreTarget,
    val entries: List<BackupRestorePlanEntry>,
    val has_conflicts: Boolean,
    val confirmation_sha256: ByteArray,
    val binding: BackupRestoreBinding?
)

fun BackupRestorePlanResult.hasValidPresence(): Boolean =
    (if (status == BackupPlanningStatus.SUCCEEDED) binding != null else binding == null)

data class BackupRestorePlanConfirmationRequest(
    val operation: OperationEnvelope,
    val binding: BackupRestoreBinding,
    val confirmed_sha256: ByteArray
)

data class BackupRestoreStageAuthorizationResult(
    val operation: OperationEnvelope,
    val status: BackupRestoreProtocolStatus,
    val authorization: BackupRestoreStageAuthorization?
)

fun BackupRestoreStageAuthorizationResult.hasValidPresence(): Boolean =
    (if (status == BackupRestoreProtocolStatus.SUCCEEDED) authorization != null else authorization == null)

data class BackupRestoreStageVerificationRequest(
    val operation: OperationEnvelope,
    val authorization: BackupRestoreStageAuthorization,
    val staged_snapshot_sha256: ByteArray,
    val skills: List<SkillRecord>
)

data class BackupRestoreCommitAuthorizationResult(
    val operation: OperationEnvelope,
    val status: BackupRestoreProtocolStatus,
    val authorization: BackupRestoreCommitAuthorization?
)

fun BackupRestoreCommitAuthorizationResult.hasValidPresence(): Boolean =
    (if (status == BackupRestoreProtocolStatus.SUCCEEDED) authorization != null else authorization == null)

data class BackupRestoreCommitOutcomeReport(
    val operation: OperationEnvelope,
    val authorization: BackupRestoreCommitAuthorization,
    val outcome: BackupRestoreCommitOutcome
)

data class BackupRestoreResolutionRequest(
    val operation: OperationEnvelope,
    val binding: BackupRestoreBinding,
    val choice: BackupRestoreResolutionChoice
)

data class BackupRestoreResolutionAuthorizationResult(
    val operation: OperationEnvelope,
    val status: BackupRestoreProtocolStatus,
    val authorization: BackupRestoreResolutionAuthorization?
)

fun BackupRestoreResolutionAuthorizationResult.hasValidPresence(): Boolean =
    (if (status == BackupRestoreProtocolStatus.SUCCEEDED) authorization != null else authorization == null)

data class BackupRestoreResolutionOutcomeReport(
    val operation: OperationEnvelope,
    val authorization: BackupRestoreResolutionAuthorization,
    val outcome: BackupRestoreResolutionOutcome
)

data class BackupRestoreCancellationRequest(
    val operation: OperationEnvelope,
    val binding: BackupRestoreBinding
)

data class BackupRestoreProtocolResult(
    val operation: OperationEnvelope,
    val status: BackupRestoreProtocolStatus
)

data class BackupRestoreCandidateWitness(
    val selection: List<BackupRecordKind>,
    val record_count: ULong,
    val candidate_records_sha256: ByteArray
)

data class BackupRestoreRecoveryBinding(
    val reservation_id: String,
    val owner_profile_id: String,
    val target_kind: BackupRestoreTargetKind,
    val target_profile_id: String,
    val backup_id: String,
    val snapshot_sha256: ByteArray,
    val confirmation_sha256: ByteArray,
    val selection: List<BackupRecordKind>,
    val record_count: ULong,
    val candidate_records_sha256: ByteArray
)

data class BackupRestoreRecoveryIntentFact(
    val intent_id: String,
    val intent: BackupRestorePhysicalIntent
)

data class BackupRestoreRecoveryOutcomeFact(
    val intent_id: String,
    val outcome: BackupRestoreObservedOutcome
)

data class BackupRestoreRecoveryRecord(
    val format_version: UInt,
    val sequence: ULong,
    val binding: BackupRestoreRecoveryBinding,
    val fact_kind: BackupRestoreRecoveryFactKind,
    val intent: BackupRestoreRecoveryIntentFact?,
    val outcome: BackupRestoreRecoveryOutcomeFact?
) {
    fun hasValidBody(): Boolean {
        val bodyCount = listOf(intent, outcome).count { it != null }
        if (bodyCount != 1) return false
        return when (fact_kind) {
            BackupRestoreRecoveryFactKind.INTENT_RECORDED -> intent != null
            BackupRestoreRecoveryFactKind.OUTCOME_OBSERVED -> outcome != null
        }
    }

    companion object {
        fun intent(format_version: UInt, sequence: ULong, binding: BackupRestoreRecoveryBinding, body: BackupRestoreRecoveryIntentFact): BackupRestoreRecoveryRecord =
            BackupRestoreRecoveryRecord(
                format_version = format_version,
                sequence = sequence,
                binding = binding,
                fact_kind = BackupRestoreRecoveryFactKind.INTENT_RECORDED,
                intent = body,
                outcome = null,
            )

        fun outcome(format_version: UInt, sequence: ULong, binding: BackupRestoreRecoveryBinding, body: BackupRestoreRecoveryOutcomeFact): BackupRestoreRecoveryRecord =
            BackupRestoreRecoveryRecord(
                format_version = format_version,
                sequence = sequence,
                binding = binding,
                fact_kind = BackupRestoreRecoveryFactKind.OUTCOME_OBSERVED,
                intent = null,
                outcome = body,
            )

    }
}

data class BackupRestoreRecoveryInspectionRequest(
    val operation: OperationEnvelope,
    val records: List<BackupRestoreRecoveryRecord>
)

data class BackupRestoreRecoveryReconciliation(
    val intent_id: String,
    val intent: BackupRestorePhysicalIntent
)

data class BackupRestoreRecoveryClassification(
    val kind: BackupRestoreRecoveryClassificationKind,
    val reconciliation: BackupRestoreRecoveryReconciliation?
)

fun BackupRestoreRecoveryClassification.hasValidPresence(): Boolean =
    (if (kind == BackupRestoreRecoveryClassificationKind.RECONCILE_REQUIRED) reconciliation != null else reconciliation == null)

data class BackupRestoreRecoveryFailure(
    val error: BackupRestoreRecoveryError
)

data class BackupRestoreRecoveryInspectionResult(
    val operation: OperationEnvelope,
    val status: BackupRestoreRecoveryInspectionStatus,
    val classification: BackupRestoreRecoveryClassification?,
    val failure: BackupRestoreRecoveryFailure?
)

fun BackupRestoreRecoveryInspectionResult.hasValidPresence(): Boolean =
    (if (status == BackupRestoreRecoveryInspectionStatus.SUCCEEDED) classification != null else classification == null) &&
        (if (status == BackupRestoreRecoveryInspectionStatus.INVALID_HISTORY) failure != null else failure == null)

data class BackupRestoreRecoveryResolutionRequest(
    val operation: OperationEnvelope,
    val history_prefix: List<BackupRestoreRecoveryRecord>,
    val choice: BackupRestoreResolutionChoice,
    val intent_id: String
)

data class BackupRestoreRecoveryResolutionAuthorization(
    val binding: BackupRestoreRecoveryBinding,
    val decision_operation: OperationEnvelope,
    val choice: BackupRestoreResolutionChoice,
    val intent_id: String,
    val history_prefix: List<BackupRestoreRecoveryRecord>
)

data class BackupRestoreRecoveryResolutionAuthorizationResult(
    val operation: OperationEnvelope,
    val status: BackupRestoreProtocolStatus,
    val authorization: BackupRestoreRecoveryResolutionAuthorization?
)

fun BackupRestoreRecoveryResolutionAuthorizationResult.hasValidPresence(): Boolean =
    (if (status == BackupRestoreProtocolStatus.SUCCEEDED) authorization != null else authorization == null)

data class BackupRestoreRecoveryResolutionOutcomeReport(
    val operation: OperationEnvelope,
    val authorization: BackupRestoreRecoveryResolutionAuthorization,
    val durable_history: List<BackupRestoreRecoveryRecord>
)

data class SavedFlowReview(
    val skill_id: String,
    val origin: String,
    val provenance: SkillProvenance,
    val status: SkillStatus,
    val active_version: UInt,
    val step_count: UInt,
    val installed_at_epoch_ms: ULong,
    val updated_at_epoch_ms: ULong,
    val recorded_from_task_id: String?,
    val reviewed_steps: List<SkillObservedStep>
)

data class SavedFlowQueryCommand(
    val operation: OperationEnvelope,
    val kind: SavedFlowQueryKind,
    val goal: String,
    val skill_id: String,
    val expected_version: UInt
)

data class SavedFlowQueryResult(
    val operation: OperationEnvelope,
    val status: SavedFlowQueryStatus,
    val flows: List<SavedFlowReview>
)
