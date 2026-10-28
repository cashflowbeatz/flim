import { useCallback, useEffect, useLayoutEffect, useRef, useState, type FormEvent, type KeyboardEvent } from 'react';
import Card from './RoomModule';
import LayoutIsland from './utils/LayoutIsland';
import FileSidebar from './FileSidebar';
import InstallPicker from './InstallPicker';
import useOverflow from './utils/useOverflow';
import { installOptions } from './utils/installOptions';
import { cleanCodeInput, connectionStatus, formatCode, sortedPeers, updatedLabel, validateCode, validateName } from './utils/roomUI';
import { version } from '../package.json';
import type { DesktopState, FileView } from './types';

const initial: DesktopState = {platform: '', name: '', installs: [], selectedInstallId: '', room: null, busy: false, nativeReady: false};
const message = (error: unknown) => error instanceof Error ? error.message : 'Something went wrong. Please try again.';
type Mode = 'create' | 'join';

function Chevron() {
  return <svg width="16" height="16" viewBox="0 0 24 24" fill="none" aria-hidden="true"><path d="m6 9 6 6 6-6" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round" /></svg>;
}

export default function App() {
  const api = window.flimDesktop;
  const [state, setState] = useState(initial);
  const [loading, setLoading] = useState(Boolean(api));
  const [setupAttempt, setSetupAttempt] = useState(0);
  const [setupError, setSetupError] = useState('');
  const [open, setOpen] = useState(false);
  const [filesOpen, setFilesOpen] = useState(false);
  const [fileSearchRequest, setFileSearchRequest] = useState(0);
  const filesButton = useRef<HTMLButtonElement>(null);
  const sidebarFocus = useRef<'files' | 'setup' | null>(null);
  const [mode, setMode] = useState<Mode>('create');
  const [name, setName] = useState('');
  const [installId, setInstallId] = useState('');
  const [code, setCode] = useState('');
  const [busy, setBusy] = useState('');
  const [slow, setSlow] = useState(false);
  const [error, setError] = useState('');
  const [fieldErrors, setFieldErrors] = useState({name: '', code: ''});
  const [formNotice, setFormNotice] = useState('');
  const [pollError, setPollError] = useState('');
  const [refreshing, setRefreshing] = useState(false);
  const [online, setOnline] = useState(navigator.onLine);
  const [clock, setClock] = useState(Date.now());
  const [copied, setCopied] = useState(false);
  const [copying, setCopying] = useState(false);
  const [copyError, setCopyError] = useState('');
  const [pasting, setPasting] = useState(false);
  const [logsError, setLogsError] = useState('');
  const [openingLogs, setOpeningLogs] = useState(false);
  const peopleScroll = useRef<HTMLDivElement>(null);
  const dismissFocus = useRef(false);
  const nameInput = useRef<HTMLInputElement>(null);
  const codeInput = useRef<HTMLInputElement>(null);
  const startButton = useRef<HTMLButtonElement>(null);
  const roomHeading = useRef<HTMLHeadingElement>(null);
  const errorBox = useRef<HTMLParagraphElement>(null);
  const actionInFlight = useRef(false);
  const actionEpoch = useRef(0);
  const clipboardFlight = useRef(false);
  const pasteFlight = useRef(false);
  const codeRevision = useRef(0);
  const modeFocus = useRef<{owner: Element | null; revision: number; mode: Mode} | null>(null);
  const statusInFlight = useRef<number | null>(null);
  const copyTimer = useRef<number | undefined>(undefined);
  const shell = useRef<HTMLElement>(null);
  const lastFit = useRef({height: 0, width: 0});
  const logsFlight = useRef(false);
  const restoreStartFocus = useRef(false);
  const room = state.room;
  const showFiles = filesOpen;
  const roomIdentity = useRef(room);
  roomIdentity.current = room;
  const currentMode = useRef(mode);
  currentMode.current = mode;
  const install = state.installs.find(item => item.id === installId);
  const peers = room ? sortedPeers(room.peers) : [];
  const mixedVersions = new Set(peers.map(peer => peer.fl)).size > 1;
  const operationBusy = Boolean(busy) || state.busy;
  const unavailable = operationBusy || loading || Boolean(setupError);
  const connection = room ? connectionStatus(room.status, online, Boolean(pollError)) : null;
  const peopleOverflow = useOverflow(peopleScroll, `${open}:${room?.code}:${peers.map(peer => `${peer.id}:${peer.name}`).join('|')}`);
  const fileMemory = useRef<{key: string | null; view: FileView} | null>(null);
  if (!fileMemory.current || fileMemory.current.key !== (room?.code ?? null)) fileMemory.current = {key: room?.code ?? null, view: {query: '', filter: 'all', order: 'name', scroll: 0, snapshot: null}};
  const submitHelp = loading ? 'Room actions will be available after discovery.' : setupError ? 'Retry setup above to continue.' : operationBusy ? (busy || 'Finishing the current step…') : !api ? 'Open the flim desktop app to connect.' : !online ? 'Reconnect to create or join a room.' : !state.nativeReady ? 'Reopen the complete flim app to restore its FL Studio launcher.' : !install ? 'Choose an FL Studio installation to continue.' : !install.supported ? 'Choose a supported installation. Its compatibility details appear above.' : 'Starts a new FL Studio window for this room.';

  useEffect(() => {
    if (!api?.fitWindow || !shell.current) return;
    let timer: number | undefined, resized = false, failed = false;
    const element = shell.current;
    const fit = (height: number, width: number) => {
      if (failed) return;
      try {api.fitWindow?.(height, width);}
      catch (error) {failed = true; try {api.reportError({message: 'Window fitting failed: ' + message(error)});} catch {}}
    };
    const schedule = () => {
      window.clearTimeout(timer);
      timer = window.setTimeout(() => {
        const height = Math.ceil(element.getBoundingClientRect().height);
        const width = showFiles ? 680 : 400;
        const previous = lastFit.current;
        // Fit width first: the narrow Files layout can otherwise collapse the window before it expands.
        if (previous.width && previous.width !== width) {
          lastFit.current = {height: previous.height, width};
          fit(previous.height, width);
          if (!failed) schedule();
          return;
        }
        if (resized || Math.abs(height - previous.height) >= 2 || previous.width !== width) {
          lastFit.current = {height, width}; fit(height, width);
        }
        resized = false;
      }, 80);
    };
    const onResize = () => { resized = true; schedule(); };
    const observer = new ResizeObserver(schedule);
    observer.observe(element);
    window.addEventListener('resize', onResize);
    return () => { observer.disconnect(); window.removeEventListener('resize', onResize); window.clearTimeout(timer); };
  }, [api, showFiles]);

  useEffect(() => {
    if (!api) return;
    let live = true;
    setLoading(true); setSetupError('');
    api.getState().then(next => {
      if (!live) return;
      setState(next); setName(next.name); setInstallId(next.selectedInstallId);
      if (next.room) setOpen(true);
    }).catch(error => { if (live) setSetupError(message(error)); })
      .finally(() => { if (live) setLoading(false); });
    return () => { live = false; };
  }, [api, setupAttempt]);

  useEffect(() => {
    if (!api || !state.busy || actionInFlight.current) return;
    let live = true, timer: number | undefined;
    const check = async () => {
      try {
        const next = await api.getState();
        if (!live) return;
        setState(next);
        if (!next.busy) {setName(next.name); setInstallId(next.selectedInstallId); if (next.room) setOpen(true); return;}
      } catch { /* Keep checking the operation owned by main after a controls reload. */ }
      if (live) timer = window.setTimeout(check, 750);
    };
    timer = window.setTimeout(check, 750);
    return () => {live = false; window.clearTimeout(timer);};
  }, [api, state.busy]);

  useEffect(() => {
    const findFiles = (event: globalThis.KeyboardEvent) => {
      if (event.defaultPrevented || event.isComposing || event.keyCode === 229 || event.shiftKey || event.altKey || !(event.ctrlKey || event.metaKey) || event.key.toLowerCase() !== 'f' || !roomIdentity.current || !api?.getFileTransfers) return;
      event.preventDefault(); setFilesOpen(true); setFileSearchRequest(value => value + 1);
    };
    window.addEventListener('keydown', findFiles);
    return () => window.removeEventListener('keydown', findFiles);
  }, [api]);

  useLayoutEffect(() => {
    const request = modeFocus.current;
    modeFocus.current = null;
    if (!request || document.activeElement !== request.owner || codeRevision.current !== request.revision || mode !== request.mode) return;
    (mode === 'join' && !validateName(nameInput.current?.value || '') ? codeInput : nameInput).current?.focus({preventScroll: true});
  }, [mode]);

  useLayoutEffect(() => {
    const target = sidebarFocus.current;
    if (showFiles || !target) return;
    sidebarFocus.current = null;
    (target === 'files' ? filesButton : mode === 'join' && !validateName(name) ? codeInput : nameInput).current?.focus({preventScroll: true});
  }, [showFiles]);

  useEffect(() => {
    if (open && !showFiles && !room && !loading && !setupError) {
      (currentMode.current === 'join' && !validateName(nameInput.current?.value || '') ? codeInput : nameInput).current?.focus({preventScroll: true});
    }
  }, [open, Boolean(room), loading, setupError]);
  useEffect(() => { if (open && !showFiles && room) roomHeading.current?.focus({preventScroll: true}); }, [open, room?.code]);
  useEffect(() => {
    if (!open && restoreStartFocus.current) { startButton.current?.focus({preventScroll: true}); restoreStartFocus.current = false; }
  }, [open]);
  useEffect(() => { if (error && !busy && !showFiles) errorBox.current?.focus(); }, [error, busy]);
  useLayoutEffect(() => {
    if (!dismissFocus.current) return;
    dismissFocus.current = false;
    if (showFiles) document.querySelector<HTMLElement>('#files-sidebar h2')?.focus({preventScroll: true});
    else (open ? room ? roomHeading : nameInput : startButton).current?.focus({preventScroll: true});
  }, [error, logsError]);
  useEffect(() => {
    setSlow(false);
    if (!busy) return;
    const timer = window.setTimeout(() => setSlow(true), 8000);
    return () => window.clearTimeout(timer);
  }, [busy]);
  useEffect(() => { document.title = connection ? `flim · ${connection.label}` : 'flim Rooms'; }, [connection?.label]);
  useEffect(() => {
    const connection = () => setOnline(navigator.onLine);
    window.addEventListener('online', connection); window.addEventListener('offline', connection);
    return () => {
      window.removeEventListener('online', connection); window.removeEventListener('offline', connection);
    };
  }, []);
  useEffect(() => {
    setCopied(false); setCopyError('');
    return () => window.clearTimeout(copyTimer.current);
  }, [room?.code]);

  const refresh = useCallback(async (force = false) => {
    if (!api || !roomIdentity.current || actionInFlight.current || statusInFlight.current === actionEpoch.current || document.hidden) return;
    if (!navigator.onLine) { setOnline(false); return; }
    const epoch = actionEpoch.current;
    statusInFlight.current = epoch; setRefreshing(true);
    try {
      const next = await api.refreshRoom(force ? {force: true} : undefined);
      if (epoch === actionEpoch.current && !actionInFlight.current) { setState(next); setPollError(''); setClock(Date.now()); }
    } catch (error) {
      if (epoch === actionEpoch.current && !actionInFlight.current) setPollError(message(error));
    } finally {
      if (statusInFlight.current === epoch) { statusInFlight.current = null; setRefreshing(false); }
    }
  }, [api]);

  useEffect(() => {
    if (room && room.status !== 'ended' && !state.busy) void refresh();
  }, [room?.code, state.busy, refresh]);
  useEffect(() => {
    if (!room || room.status === 'ended') return;
    const timer = window.setInterval(() => { void refresh(); }, room.status === 'waiting' || pollError ? 5000 : 30_000);
    const resume = () => { if (!document.hidden) { setClock(Date.now()); void refresh(); } };
    document.addEventListener('visibilitychange', resume); window.addEventListener('online', resume); window.addEventListener('focus', resume);
    return () => { window.clearInterval(timer); document.removeEventListener('visibilitychange', resume); window.removeEventListener('online', resume); window.removeEventListener('focus', resume); };
  }, [room?.code, room?.status, Boolean(pollError), refresh]);
  useEffect(() => {
    if (!open || !room || room.status === 'ended') return;
    const timer = window.setInterval(() => { if (!document.hidden) setClock(Date.now()); }, 10_000);
    return () => window.clearInterval(timer);
  }, [open, room?.code, room?.status]);

  function collapse() { codeRevision.current++; restoreStartFocus.current = true; setOpen(false); }

  function closeFiles(setup = false) {
    sidebarFocus.current = setup ? 'setup' : 'files';
    setFilesOpen(false); setFileSearchRequest(0);
    if (setup) setOpen(true);
  }

  function changeMode(next: Mode, focusField = false) {
    if (next === mode || unavailable) return;
    codeRevision.current++;
    modeFocus.current = focusField ? {owner: document.activeElement, revision: codeRevision.current, mode: next} : null;
    setMode(next); setError(''); setFormNotice(''); setFieldErrors({name: '', code: ''});
  }

  function modeKeys(event: KeyboardEvent<HTMLDivElement>) {
    if (event.defaultPrevented || event.nativeEvent.isComposing || event.keyCode === 229 || event.altKey || event.ctrlKey || event.metaKey || event.shiftKey) return;
    if (!['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(event.key)) return;
    event.preventDefault();
    const next = event.key === 'Home' ? 'create' : event.key === 'End' ? 'join' : mode === 'create' ? 'join' : 'create';
    if (unavailable) return;
    changeMode(next); event.currentTarget.querySelectorAll('button')[next === 'create' ? 0 : 1]?.focus();
  }

  async function action(label: string, task: () => Promise<void>) {
    if (actionInFlight.current) return;
    actionEpoch.current++; statusInFlight.current = null; setRefreshing(false);
    actionInFlight.current = true; setBusy(label); setError(''); setFormNotice('');
    try { await task(); }
    catch (error) {
      if (api) { try { setState(await api.getState()); } catch {} }
      setError(message(error));
    } finally { actionInFlight.current = false; setBusy(''); }
  }

  async function launch() {
    if (!api) return;
    setBusy('Opening FL Studio…'); setState(await api.launchFL());
    // Startup can finish before the native WebSocket handshake.
    try { setState(await api.refreshRoom()); setPollError(''); setClock(Date.now()); }
    catch (error) { setPollError(message(error)); }
  }

  function submit(event: FormEvent) {
    event.preventDefault();
    if (!api || unavailable || actionInFlight.current || !navigator.onLine || !state.nativeReady || !install?.supported) return;
    const errors = {name: validateName(name), code: mode === 'join' ? validateCode(code) : ''};
    setFieldErrors(errors); setError('');
    if (errors.name || errors.code) { (errors.name ? nameInput : codeInput).current?.focus(); return; }
    void action(mode === 'create' ? 'Creating your room…' : 'Joining the room…', async () => {
      const input = {name: name.trim(), installId};
      const next = mode === 'create' ? await api.createRoom(input) : await api.joinRoom({...input, code});
      setState(next); setName(next.name); setInstallId(next.selectedInstallId); setPollError('');
      await launch();
    });
  }

  async function copyInvitation() {
    if (!api || clipboardFlight.current || !room) return;
    const owner = room;
    clipboardFlight.current = true; setCopying(true); setCopyError(''); setCopied(false);
    try {
      await api.copyInvite();
      if (roomIdentity.current?.code !== owner.code || roomIdentity.current.status === 'ended') return;
      window.clearTimeout(copyTimer.current); setCopied(true);
      copyTimer.current = window.setTimeout(() => setCopied(false), 2500);
    } catch (error) { if (roomIdentity.current?.code === owner.code && roomIdentity.current.status !== 'ended') setCopyError(message(error)); }
    finally { clipboardFlight.current = false; setCopying(false); }
  }

  async function pasteCode() {
    if (!api || pasteFlight.current) return;
    const epoch = actionEpoch.current, revision = codeRevision.current, focusOwner = document.activeElement;
    pasteFlight.current = true; setPasting(true);
    const current = () => currentMode.current === 'join' && actionEpoch.current === epoch && codeRevision.current === revision && !roomIdentity.current && (document.activeElement === focusOwner || document.activeElement === codeInput.current);
    try {
      const result = await api.pasteRoomCode();
      if (!current()) return;
      setCode(formatCode(result.code)); setFieldErrors(errors => ({...errors, code: ''})); setError(''); codeInput.current?.focus();
    } catch (error) {
      if (current()) { setFieldErrors(errors => ({...errors, code: message(error)})); codeInput.current?.focus(); }
    } finally { pasteFlight.current = false; setPasting(false); }
  }

  async function openLogs() {
    if (!api || logsFlight.current) return;
    logsFlight.current = true; setOpeningLogs(true); setLogsError('');
    try { await api.openLogs(); } catch (error) { setLogsError(message(error)); }
    finally { logsFlight.current = false; setOpeningLogs(false); }
  }

  async function returnToSetup() {
    setState(await api!.leaveRoom());
    setFilesOpen(false);
    codeRevision.current++; setMode('create'); setCode('');
    setCopied(false); setCopyError(''); setPollError('');
    setFieldErrors({name: '', code: ''});
  }

  const form = <form onSubmit={submit} noValidate className="room-form" onKeyDown={event => {if (event.key === 'Enter' && (event.nativeEvent.isComposing || event.keyCode === 229)) event.preventDefault();}}>
    <div className="menu-heading"><h2>Meet in a room.</h2></div>
    {!api && <p className="notice">This is the browser preview. Open the flim desktop app to connect to FL Studio.</p>}
    {loading && <p className="notice" role="status">Checking your FL Studio installations…</p>}
    {setupError && <div className="error" role="alert"><p>Setup could not be loaded. {setupError}</p><button type="button" className="text-button" onClick={() => setSetupAttempt(attempt => attempt + 1)}>Retry setup</button></div>}
    {api && !loading && !setupError && !state.nativeReady && <p className="notice">The FL Studio launcher is missing. Reopen the complete flim app, or download a fresh copy.</p>}
    <div className="mode-switch" role="tablist" aria-label="Room action" onKeyDown={modeKeys}>
      <button type="button" id="create-tab" role="tab" aria-selected={mode === 'create'} aria-controls="room-fields" tabIndex={mode === 'create' ? 0 : -1} onClick={() => changeMode('create', true)} disabled={unavailable}>Create a room</button>
      <button type="button" id="join-tab" role="tab" aria-selected={mode === 'join'} aria-controls="room-fields" tabIndex={mode === 'join' ? 0 : -1} onClick={() => changeMode('join', true)} disabled={unavailable}>Join a room</button>
    </div>
    <div id="room-fields" role="tabpanel" aria-labelledby={`${mode}-tab`}>
    <div className="field">
      <div className="field-top"><label htmlFor="room-name">Your name</label><span className="field-counter" aria-hidden="true">{name.length}/24</span></div>
      <input id="room-name" ref={nameInput} autoComplete="nickname" placeholder="What should we call you?" maxLength={24} required value={name} aria-invalid={Boolean(fieldErrors.name)} aria-describedby={fieldErrors.name ? 'name-error name-help' : 'name-help'} onChange={event => {setName(event.target.value); setFieldErrors(errors => ({...errors, name: ''})); setError('');}} disabled={unavailable} />
      <span id="name-help" className="sr-only">A name your friends will recognize, up to 24 characters.</span>
      {fieldErrors.name && <p id="name-error" className="field-error" role="alert">{fieldErrors.name}</p>}
    </div>
    <div className="field">
      <div className="field-top"><label id="fl-install-label" htmlFor="fl-install">FL Studio</label><button className="text-button" type="button" disabled={!api || unavailable} onClick={() => void action('Finding FL Studio…', async () => {const next = await api!.chooseInstall(installId); setState(next); setInstallId(next.selectedInstallId);})}>Choose folder</button></div>
      <InstallPicker options={installOptions(state.installs, installId)} value={installId} onChange={id => {setInstallId(id); setError(''); setFormNotice('');}} disabled={!api || unavailable} loading={loading} />
      <p id="install-help" className="field-help">{install && !install.supported ? install.reason : !state.installs.length ? 'Choose the folder containing FL64.exe, or check again after installing FL Studio.' : 'Any supported version can host. Older FL joins newer hosts with notes and basic controls.'}</p>
      <div className="install-actions"><button type="button" className="text-button" disabled={!api || unavailable || !state.nativeReady} onClick={() => void action('Checking installations…', async () => {
        const next = await api!.rescanInstalls(installId); setState(next); setInstallId(next.selectedInstallId);
        setFormNotice(`Found ${next.installs.length} ${next.installs.length === 1 ? 'installation' : 'installations'}. Compatibility was checked again.`);
      })}>Check again</button>{install && <details key={install.id} className="install-details" onKeyDown={event => {
        if (event.key === 'Escape' && event.currentTarget.open && !event.defaultPrevented && !event.nativeEvent.isComposing && event.keyCode !== 229 && !event.altKey && !event.ctrlKey && !event.metaKey) {
          event.preventDefault(); event.stopPropagation(); event.currentTarget.open = false; event.currentTarget.querySelector('summary')?.focus({preventScroll: true});
        }
      }}><summary>Installation details</summary><p>Build {install.version}</p><p><bdi>{install.path}</bdi></p><p>To use another copy, choose its folder.</p></details>}</div>
    </div>
    <div className="room-entry" data-mode={mode}>
    <div className="create-explanation" aria-hidden={mode !== 'create'}><p>Your own room number.</p><span>Create a room, then send its eight-digit number to your friends. No account needed.</span></div>
    <div className="field room-number-field" aria-hidden={mode !== 'join'}>
      <div className="field-top"><label htmlFor="room-code">Room number</label><button type="button" className="text-button" disabled={mode !== 'join' || !api || unavailable || pasting} onClick={() => void pasteCode()}>{pasting ? 'Pasting…' : 'Paste room number'}</button></div>
      <input id="room-code" ref={codeInput} className="code-input" type="text" inputMode="numeric" enterKeyHint="go" autoComplete="off" spellCheck={false} placeholder="1234 5678" maxLength={24} required pattern={String.raw`(?:[ \-]*[0-9]){8}[ \-]*`} title="Enter eight digits. Spaces and hyphens are optional." value={code} aria-invalid={Boolean(fieldErrors.code)} aria-describedby={fieldErrors.code ? 'code-error code-help' : 'code-help'} onChange={event => {codeRevision.current++; setCode(cleanCodeInput(event.target.value)); setFieldErrors(errors => ({...errors, code: ''})); setError(''); setFormNotice('');}} onPaste={event => {
        const text = event.clipboardData.getData('text');
        event.preventDefault(); codeRevision.current++;
        if (validateCode(text)) { setFieldErrors(errors => ({...errors, code: 'Paste just the eight-digit room number.'})); return; }
        setCode(formatCode(cleanCodeInput(text).replace(/\D/g, '')));
        setFieldErrors(errors => ({...errors, code: ''})); setError(''); setFormNotice('');
      }} onBlur={() => { if (!validateCode(code)) setCode(formatCode(cleanCodeInput(code).replace(/\D/g, ''))); }} disabled={mode !== 'join' || unavailable} />
      <span id="code-help" className="field-help">{code ? `${code.replace(/\D/g, '').length} of 8 digits entered.` : 'Ask your friend for their eight-digit number.'}</span>
      {fieldErrors.code && <p id="code-error" className="field-error" role="alert">{fieldErrors.code}</p>}
    </div>
    </div>
    </div>
    {formNotice && <p className="form-feedback" role="status">{formNotice}</p>}
    <button className="primary submit" aria-describedby="submit-help" disabled={!api || unavailable || !state.nativeReady || !install?.supported || !online}>{busy || (state.busy ? 'Finishing the current step…' : mode === 'create' ? 'Create room & open FL Studio' : 'Join room & open FL Studio')}<span aria-hidden="true">↗</span></button>
    <p id="submit-help" className="form-footnote" role="status">{submitHelp}</p>
  </form>;

  const roomView = room && <section className="room-view" aria-label="Your room">
    <div className="room-overview">
    <div className="room-top"><span className={`status ${connection!.tone}`} role="status"><span aria-hidden="true" />{connection!.label}</span><span className="subtle">{room.role === 'host' ? 'You created this room' : 'You joined this room'}</span></div>
    <h2 ref={roomHeading} tabIndex={-1}>Room <span className="room-number">{formatCode(room.code)}</span></h2>
    <p className="room-description">{room.status === 'ended' ? 'Return to start to create or join another room.' : room.role === 'host' ? 'Share this number. Your friends enter it in flim.' : 'Your edits travel with the room. Keep flim running in FL Studio.'}</p>
    {room.status !== 'ended' && <button type="button" className="copy-button" disabled={copying} aria-busy={copying} onClick={() => void copyInvitation()}>{copying ? 'Copying…' : copied ? 'Room number copied' : 'Copy room number'}<svg width="17" height="17" viewBox="0 0 24 24" fill="none" aria-hidden="true">{copied ? <path d="m5 12 4 4 10-10" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round" /> : <><rect x="8" y="8" width="12" height="13" rx="2" stroke="currentColor" strokeWidth="1.7"/><path d="M16 8V5a2 2 0 0 0-2-2H5a2 2 0 0 0-2 2v9a2 2 0 0 0 2 2h3" stroke="currentColor" strokeWidth="1.7"/></>}</svg></button>}
    <p className="sr-only" role="status">{copied ? 'Room number copied to clipboard.' : ''}</p>
    {copyError && <p className="error" role="alert">{copyError} You can select and copy the room number above.</p>}
    <div className="people-heading"><h3 id="people-title">In this room</h3>{peopleOverflow && <span id="people-scroll-help" className="list-scroll-hint">Scroll for everyone</span>}<span role="status" aria-label={`${peers.length} of ${room.maxParticipants} people connected`}>{peers.length} / {room.maxParticipants}</span></div>
    {peers.length ? <div ref={peopleScroll} className="people-scroll" role="region" aria-labelledby="people-title" aria-describedby={peopleOverflow ? 'people-scroll-help' : undefined} tabIndex={peopleOverflow ? 0 : undefined}><ul className="people">{peers.map(peer => <li key={peer.id}>
      <div><bdi>{peer.name}</bdi><small aria-label={`FL Studio ${peer.fl.split('.')[0]}, build ${peer.fl}`} title={`Build ${peer.fl}`}>FL Studio {peer.fl.split('.')[0]}</small></div>
      <span className="peer-role">{peer.role === 'host' ? 'Host' : 'Member'}</span>
    </li>)}</ul></div> : <p className="empty-people">{room.status === 'ended' ? 'Everyone has disconnected.' : 'Waiting for FL Studio to connect. People appear here once connected.'}</p>}
    {room.status !== 'ended' && <p className={`list-freshness ${!online || pollError ? 'stale' : ''}`} role={pollError ? 'status' : undefined}>{!online || pollError ? 'Showing the last confirmed people. Live status is unavailable.' : updatedLabel(room.statusUpdatedAt, clock)}</p>}
    {mixedVersions && room.status !== 'ended' && <p className="compatibility-note">Different FL versions: older FL shares notes and basic controls; some sounds and playlist features may be unavailable.</p>}
    {room.state === 'ready' && room.status !== 'ended' && <button className="primary submit" disabled={operationBusy || !online} onClick={() => void action('Opening FL Studio…', launch)}>{busy || (state.busy ? 'Finishing the current step…' : 'Open FL Studio')}<span aria-hidden="true">↗</span></button>}
    {room.state === 'uncertain' && room.status !== 'ended' && <p className="notice">The launcher did not confirm startup. Check FL Studio before starting another room.</p>}
    {pollError && room.status !== 'ended' && <div className="notice" role="alert"><p>{pollError}</p><p>The room list retries automatically while this window is visible.</p><button type="button" className="text-button" disabled={!online || refreshing || operationBusy} onClick={() => void refresh(true)}>Retry status</button></div>}
    {busy && room.state !== 'ready' && <p className="busy-message" role="status">{busy}</p>}
    </div>
    <div className="room-bottom"><button type="button" className="text-button" disabled={operationBusy} onClick={() => void action('Returning…', returnToSetup)}>Return to start</button>{room.status !== 'ended' && <button type="button" className="text-button" disabled={!online || operationBusy || refreshing} aria-busy={refreshing} onClick={() => void refresh(true)}>{refreshing ? 'Refreshing people…' : 'Refresh people'}</button>}</div>
    <p className="form-footnote">FL Studio stays open when you close these controls. Closing the host’s FL Studio ends the room.</p>
  </section>;

  const footer = <footer className="module-footer"><p className="module-caption">{loading ? 'Checking FL Studio…' : setupError && !open ? 'Open Get started to retry setup.' : room ? room.status === 'ended' ? 'Return to start for another room.' : 'Keep FL Studio open for your room.' : open ? 'No account needed.' : 'Create a room or join a friend. No signup.'}</p>{api && <button type="button" className="text-button" aria-describedby="logs-help" disabled={state.diagnosticsAvailable === false || openingLogs} aria-busy={openingLogs} onClick={() => void openLogs()}>{openingLogs ? 'Opening test logs…' : 'Open test logs'}</button>}<span id="logs-help" className="sr-only">{state.diagnosticsAvailable === false ? 'Logs unavailable. Check disk space.' : 'Logs stay on this computer.'}</span></footer>;

  return <main ref={shell} className={`app-shell${showFiles ? ' files-open' : ''}`}>
    <p className="sr-only" role="status">{busy || (loading ? 'Checking your FL Studio installations.' : '')}</p>
    <header className="app-header"><h1><img className="app-logo" src="./brand/flim-logo.png" width="32" height="32" alt="" />flim<span aria-hidden="true">.</span></h1>
      <span className="app-version" title={`flim Rooms ${version}`}>{version}</span>
    </header>
    <div className="card-wrap" data-open={open} onKeyDown={event => {if (event.key === 'Escape' && !event.defaultPrevented && !event.nativeEvent.isComposing && event.keyCode !== 229 && !event.altKey && !event.ctrlKey && !event.metaKey && open && !busy) {event.preventDefault(); collapse();}}}>
      <nav className="panel-nav" aria-label="Room navigation">
        {open ? <button type="button" className="text-button back-button" aria-label="Collapse room controls" disabled={Boolean(busy)} onClick={collapse}><span aria-hidden="true">←</span> Back</button> : <span className="panel-label">Rooms</span>}
        <button ref={filesButton} type="button" className="sidebar-toggle" title={showFiles ? 'Hide files sidebar' : 'Show files sidebar'} aria-expanded={showFiles} aria-controls="files-sidebar" onClick={() => showFiles ? closeFiles() : setFilesOpen(true)}><span>Files</span><svg width="16" height="16" viewBox="0 0 24 24" fill="none" aria-hidden="true"><path d={showFiles ? 'm15 6-6 6 6 6' : 'm9 6 6 6-6 6'} stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round" /></svg></button>
      </nav>
      <LayoutIsland><Card variant={open ? 'Opened' : 'Closed'} nv4C0a_ii="./framer-assets/session-link.png" className="room-card"
        heroContent={<div className="hero-content"><div className="hero-top"><span>FL STUDIO × FRIENDS</span><span>{room ? `ROOM ${formatCode(room.code)}` : 'ROOMS'}</span></div><button ref={startButton} className="start-button" disabled={Boolean(busy)} aria-expanded={open} aria-controls="room-menu" onClick={() => open ? collapse() : setOpen(true)}>{room ? 'Your room' : 'Get started'}<Chevron /></button><p className="hero-caption">{room ? `${connection!.label} · ${room.peers.length} ${room.peers.length === 1 ? 'person' : 'people'}${!online || pollError ? ' last confirmed' : ''}` : 'Same project. Different places.'}</p></div>}
        expandedContent={<div id="room-menu" className="menu-content" aria-busy={operationBusy || loading}>{!online && <p className="notice" role="status">You are offline. {room ? 'FL Studio stays open. The people list will update when your connection returns.' : 'Reconnect to create or join a room.'}</p>}{state.notice && <p className="notice" role="status">{state.notice}</p>}{room ? roomView : form}{slow && <p className="notice slow-operation" role="status">This is taking longer than usual. flim is still waiting for the current step to finish. Avoid opening another room while it completes.</p>}</div>}
        footerContent={showFiles ? null : footer}
      /></LayoutIsland>
    </div>
    {showFiles && <FileSidebar key={room?.code ?? 'setup'} roomKey={room?.code ?? null} ended={room?.status === 'ended'} busy={unavailable} searchRequest={fileSearchRequest} memory={fileMemory.current.view} onSetup={() => closeFiles(true)} onClose={() => closeFiles()} />}
    {showFiles && footer}
    {api && state.diagnosticsAvailable === false && <p className="form-footnote diagnostics-notice">Test logs are unavailable. Check disk space and folder access.</p>}
    {(error || logsError) && <div className="error app-feedback" role="alert"><p ref={errorBox} tabIndex={-1}>{error || logsError}</p><button type="button" className="text-button" onClick={() => {dismissFocus.current = true; setError(''); setLogsError('');}}>Dismiss message</button></div>}
  </main>;
}
