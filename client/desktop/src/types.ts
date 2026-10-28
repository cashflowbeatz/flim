export interface Install {
  id: string;
  path: string;
  version: string;
  supported: boolean;
  experimental?: boolean;
  reason: string;
}

export interface Room {
  code: string;
  role: 'host' | 'member';
  maxParticipants: number;
  expiresAt: string | null;
  state: 'ready' | 'launched' | 'uncertain';
  status: 'waiting' | 'active' | 'ended';
  statusUpdatedAt?: number | null;
  peers: { id: string; name: string; role: 'host' | 'member'; fl: string }[];
}

export interface DesktopState {
  platform: string;
  name: string;
  installs: Install[];
  selectedInstallId: string;
  room: Room | null;
  busy: boolean;
  nativeReady: boolean;
  diagnosticsAvailable?: boolean;
  notice?: string;
}

export interface DesktopAPI {
  getSampleLocation(): Promise<SampleLocation>;
  chooseSampleLocation(): Promise<SampleLocation>;
  resetSampleLocation(): Promise<SampleLocation>;
  getFileTransfers(): Promise<FileTransferState>;
  openSampleFolder(): Promise<{opened: true}>;
  getState(): Promise<DesktopState>;
  savePreferences(input: {name: string; installId: string}): Promise<DesktopState>;
  chooseInstall(currentInstallId?: string): Promise<DesktopState>;
  rescanInstalls(currentInstallId?: string): Promise<DesktopState>;
  createRoom(input: {name: string; installId: string}): Promise<DesktopState>;
  joinRoom(input: {code: string; name: string; installId: string}): Promise<DesktopState>;
  launchFL(): Promise<DesktopState>;
  leaveRoom(): Promise<DesktopState>;
  refreshRoom(options?: {force?: boolean}): Promise<DesktopState>;
  copyInvite(): Promise<{copied: true}>;
  pasteRoomCode(): Promise<{code: string}>;
  openLogs(): Promise<{opened: true}>;
  reloadControls(): Promise<void>;
  reportError(input: {message: string; stack?: string}): void;
  fitWindow?(height: number, width?: number): void;
}

export interface SampleLocation {path: string; defaultPath: string; custom: boolean; canChange: boolean; error: string}

export interface FileTransfer {
  id: string;
  name: string;
  direction: 'upload' | 'download';
  state: 'checking' | 'uploading' | 'downloading' | 'waiting' | 'ready' | 'missing' | 'failed' | 'blocked';
  reason: string;
  bytes: number;
  done: number;
  updatedAt: number;
}

export interface FileTransferState {items: FileTransfer[]; available: boolean; stale: boolean; total?: number; truncated?: boolean}

export interface FileView {
  query: string;
  filter: 'all' | 'pending' | 'attention' | 'ready';
  order: 'name' | 'recent';
  direction?: 'all' | 'upload' | 'download';
  scroll: number;
  snapshot: FileTransferState | null;
}

declare global {
  interface Window { flimDesktop?: DesktopAPI }
}
