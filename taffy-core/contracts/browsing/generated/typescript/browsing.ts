// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Generated from schema/contract.json. Do not edit.
// Contract browsing 1.1.

export const MAX_TABS = 256 as const;
export const MAX_DOWNLOADS = 256 as const;
export const MAX_IDENTIFIER_BYTES = 256 as const;
export const MAX_HOST_BYTES = 253 as const;
export const MAX_TITLE_BYTES = 1024 as const;
export const MAX_FILE_NAME_BYTES = 512 as const;

export enum TabOwner {
  Unknown = 0,
  User = 1,
  Assistant = 2,
}

export enum TabPrivacy {
  Normal = 0,
  Private = 1,
}

export enum NavigationFailure {
  None = 0,
  DnsFailure = 1,
  Offline = 2,
  ConnectionFailure = 3,
  Timeout = 4,
  TlsFailure = 5,
  HttpErrorStatus = 6,
  BlockedByClient = 7,
  BlockedBySafeBrowsing = 8,
  Aborted = 9,
  UnknownFailure = 10,
}

export enum InterstitialKind {
  None = 0,
  CertificateError = 1,
  SafeBrowsing = 2,
  BlockedByPolicy = 3,
  Other = 4,
}

export enum DownloadState {
  Created = 0,
  InProgress = 1,
  Paused = 2,
  Interrupted = 3,
  Complete = 4,
  Cancelled = 5,
}

export enum DownloadFailure {
  None = 0,
  Network = 1,
  Server = 2,
  FileSystem = 3,
  InsufficientSpace = 4,
  PermissionDenied = 5,
  BlockedBySecurityCheck = 6,
  CancelledByUser = 7,
  BrowserShutdown = 8,
  Unknown = 9,
}

export enum DownloadDestination {
  Undecided = 0,
  DefaultDirectory = 1,
  UserChosenLocation = 2,
  ApplicationPrivateDirectory = 3,
}

export enum DownloadCommand {
  Pause = 0,
  Resume = 1,
  Cancel = 2,
  Retry = 3,
  OpenWhenComplete = 4,
  OpenNow = 5,
}

export enum BrowsingStatus {
  Accepted = 0,
  InvalidRequest = 1,
  UnknownTab = 2,
  UnknownDownload = 3,
  IllegalInState = 4,
  RefusedByPolicy = 5,
  NoHistory = 6,
  Unavailable = 7,
}

export interface TabView {
  readonly tab_id: string;
  readonly title: string;
  readonly host: string;
  readonly has_been_nowhere: boolean;
  readonly owner: TabOwner;
  readonly privacy: TabPrivacy;
  readonly selected: boolean;
}

export interface NavigationView {
  readonly host: string;
  readonly title: string;
  readonly can_go_back: boolean;
  readonly can_go_forward: boolean;
  readonly is_loading: boolean;
  readonly failure: NavigationFailure;
  readonly interstitial: InterstitialKind;
  readonly http_status_code: number;
  readonly filtering_active: boolean;
  readonly blocked_request_count: number;
}

export interface DownloadView {
  readonly download_id: string;
  readonly file_name: string;
  readonly host: string;
  readonly received_bytes: bigint;
  readonly total_bytes: bigint;
  readonly total_known: boolean;
  readonly state: DownloadState;
  readonly failure: DownloadFailure;
  readonly destination: DownloadDestination;
  readonly resumable: boolean;
  readonly requires_danger_confirmation: boolean;
}

export interface BrowsingStateView {
  readonly tabs: ReadonlyArray<TabView>;
  readonly navigation: NavigationView;
  readonly downloads: ReadonlyArray<DownloadView>;
}
