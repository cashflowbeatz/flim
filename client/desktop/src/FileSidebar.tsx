import {useEffect, useLayoutEffect, useMemo, useRef, useState} from 'react';
import type {FileTransferState, FileView} from './types';
import {fileAmount, fileProgress, fileSearchKey, fileStatus, reconcileTransfers} from './utils/fileTransfers';
import {formatCode} from './utils/roomUI';
import useOverflow from './utils/useOverflow';
import SampleLocation from './SampleLocation';
import './file-sidebar.css';

const filters = [{id: 'all', label: 'All'}, {id: 'pending', label: 'Pending'}, {id: 'attention', label: 'Attention'}, {id: 'ready', label: 'Ready'}] as const;
const collator = new Intl.Collator(undefined, {numeric: true, sensitivity: 'base'});

export default function FileSidebar({roomKey, ended, memory, busy = false, searchRequest = 0, onClose, onSetup}: {roomKey: string | null; ended: boolean; memory: FileView; busy?: boolean; searchRequest?: number; onClose(): void; onSetup(): void}) {
  const api = window.flimDesktop;
  const [state, setState] = useState<FileTransferState>(() => memory.snapshot ?? {items: [], available: true, stale: false});
  const [loading, setLoading] = useState(true);
  const [refreshing, setRefreshing] = useState(false);
  const [query, setQuery] = useState(memory.query);
  const [filter, setFilter] = useState(memory.filter);
  const [order, setOrder] = useState(memory.order);
  const [direction, setDirection] = useState(memory.direction ?? 'all');
  const [scrollEdge, setScrollEdge] = useState<{atBottom: boolean; revision: unknown}>({atBottom: false, revision: null});
  const heading = useRef<HTMLHeadingElement>(null);
  const search = useRef<HTMLInputElement>(null);
  const list = useRef<HTMLDivElement>(null);
  const restoreScroll = useRef(memory.scroll);
  const resetScroll = useRef(false);
  const refresh = useRef<() => void>(() => {});
  const readRoom = useRef(roomKey);
  const hasSnapshot = useRef(Boolean(memory.snapshot && (memory.snapshot.items.length || memory.snapshot.available && !memory.snapshot.stale)));
  const canRead = Boolean(api?.getFileTransfers);

  useEffect(() => { heading.current?.focus({preventScroll: true}); }, []);
  useEffect(() => {if (searchRequest) {search.current?.focus({preventScroll: true}); search.current?.select();}}, [searchRequest]);
  useLayoutEffect(() => {memory.query = query; memory.filter = filter; memory.order = order; memory.direction = direction; memory.snapshot = state;}, [memory, query, filter, order, direction, state]);
  useLayoutEffect(() => {
    if (list.current && restoreScroll.current) list.current.scrollTop = restoreScroll.current;
    restoreScroll.current = 0;
  }, []);
  useLayoutEffect(() => {
    if (resetScroll.current && list.current) {list.current.scrollTop = 0; memory.scroll = 0;}
    resetScroll.current = true;
  }, [query, filter, order, direction, memory]);
  useEffect(() => {
    let live = true, flight = false, failures = 0, hasRead = false;
    let timer: number | undefined;
    const sameRoom = readRoom.current === roomKey;
    readRoom.current = roomKey;
    if (!sameRoom) {setState({items: [], available: true, stale: false}); hasSnapshot.current = false;}
    setLoading(Boolean(roomKey && canRead && (!ended || !hasSnapshot.current)));
    refresh.current = () => {};
    if (!roomKey || !canRead) return;
    const schedule = () => {
      window.clearTimeout(timer);
      if (live && !ended && !document.hidden) timer = window.setTimeout(() => void poll(), Math.min(15_000, 1000 * 2 ** Math.min(failures, 4)));
    };
    const poll = async (manual = false) => {
      if (document.hidden || !live) return;
      if (manual) setRefreshing(true);
      if (flight) return;
      window.clearTimeout(timer); flight = true;
      try {
        const next = await api!.getFileTransfers();
        if (live) {
          failures = next.available && !next.stale ? 0 : failures + 1;
          if (next.items.length || next.available && !next.stale) hasSnapshot.current = true;
          setState(old => reconcileTransfers(old, next));
        }
      } catch {
        if (live) { failures++; setState(old => ({...old, available: false, stale: true})); }
      } finally {
        flight = false;
        if (live) { hasRead = true; setLoading(false); setRefreshing(false); schedule(); }
      }
    };
    refresh.current = () => void poll(true);
    const resume = () => {
      window.clearTimeout(timer);
      if (!document.hidden && (!ended || !hasRead)) { failures = 0; void poll(); }
    };
    if (!ended || !hasSnapshot.current) void poll();
    else hasRead = true;
    document.addEventListener('visibilitychange', resume);
    return () => { live = false; refresh.current = () => {}; window.clearTimeout(timer); document.removeEventListener('visibilitychange', resume); };
  }, [api, roomKey, ended, canRead]);

  const groups = useMemo(() => {
    const sorted = [...state.items].sort((a, b) => (order === 'recent' ? b.updatedAt - a.updatedAt : 0) || collator.compare(a.name, b.name) || a.id.localeCompare(b.id));
    const grouped = [
      {title: 'Transferring', filter: 'pending', items: [] as typeof sorted},
      {title: 'Waiting', filter: 'pending', items: [] as typeof sorted},
      {title: 'Needs attention', filter: 'attention', items: [] as typeof sorted},
      {title: 'Ready', filter: 'ready', items: [] as typeof sorted},
    ];
    for (const file of sorted) grouped[file.state === 'ready' ? 3 : file.state === 'waiting' ? 1 : ['failed', 'missing', 'blocked'].includes(file.state) ? 2 : 0].items.push(file);
    return grouped;
  }, [state.items, order]);
  const searchKeys = useMemo(() => new Map(state.items.map(file => [file.id, fileSearchKey(file.name)])), [state.items]);
  const queryKey = fileSearchKey(query);
  const searched = useMemo(() => groups.map(group => ({...group, items: queryKey || direction !== 'all' ? group.items.filter(file => (direction === 'all' || file.direction === direction) && (!queryKey || searchKeys.get(file.id)?.includes(queryKey))) : group.items})), [groups, queryKey, searchKeys, direction]);
  const counts = useMemo(() => Object.fromEntries(filters.map(item => [item.id, searched.reduce((count, group) => count + (item.id === 'all' || group.filter === item.id ? group.items.length : 0), 0)])), [searched]);
  const visible = useMemo(() => searched.filter(group => filter === 'all' || group.filter === filter), [searched, filter]);
  const matches = visible.reduce((count, group) => count + group.items.length, 0);
  const unavailable = !state.available || state.stale;
  const overflowing = useOverflow(list, visible);

  return <aside id="files-sidebar" className="files-sidebar" aria-label="Shared files" onKeyDown={event => {
    if (event.defaultPrevented || event.nativeEvent.isComposing || event.keyCode === 229) return;
    if ((event.ctrlKey || event.metaKey) && !event.altKey && !event.shiftKey && event.key.toLowerCase() === 'f' && roomKey && canRead) {event.preventDefault(); search.current?.focus(); search.current?.select();}
    if (event.key === 'Escape' && !event.altKey && !event.ctrlKey && !event.metaKey) {
      event.preventDefault(); event.stopPropagation();
      if (query) {setQuery(''); search.current?.focus({preventScroll: true});} else onClose();
    }
  }}>
    <div className="files-heading"><h2 ref={heading} tabIndex={-1}>Files</h2><button type="button" className="text-button" onClick={onClose}>Hide files</button></div>
    <SampleLocation locked={Boolean(roomKey) || busy} />
    {!roomKey ? <div className="files-start"><p className="files-empty">Create or join a room to share samples.</p><p className="file-help">Add a kick or another sample in FL Studio. Its transfer will appear here when the room is connected.</p><button type="button" className="text-button" onClick={onSetup}>Open room setup</button></div> : <>
    <p className="files-room">Room <bdi>{formatCode(roomKey)}</bdi>{ended ? ' · Closed' : ''}</p>
    <p className="files-intro">Samples used in shared edits arrive automatically. Transfers continue when this panel is closed.</p>
    {ended && <p className="files-notice">This room has closed. Showing its last file activity; there are no automatic status checks.</p>}
    {!canRead ? <p className="files-notice" role="status">File status is unavailable in these controls. Reopen the complete current flim app to view transfers.</p> : <>
    <div className="file-search"><label className="sr-only" htmlFor="file-query">Find a file</label><input ref={search} id="file-query" type="search" autoComplete="off" spellCheck={false} maxLength={128} placeholder="Find a file" aria-keyshortcuts="Control+f Meta+f" value={query} onChange={event => setQuery(event.target.value)} /><button type="button" className="text-button clear-file-search" aria-label="Clear file search" disabled={!query} style={{visibility: query ? 'visible' : 'hidden'}} onClick={() => {setQuery(''); search.current?.focus({preventScroll: true});}}>Clear</button></div>
    <div className="file-directions" role="group" aria-label="Transfer direction">
      {([{id: 'all', label: 'Both'}, {id: 'download', label: 'To me'}, {id: 'upload', label: 'From me'}] as const).map(item => <button key={item.id} type="button" aria-pressed={direction === item.id} onClick={() => setDirection(item.id)}>{item.label}</button>)}
    </div>
    <div className="file-filters" role="toolbar" aria-label="File status filter" onKeyDown={event => {
      if (event.defaultPrevented || event.nativeEvent.isComposing || event.keyCode === 229 || event.altKey || event.ctrlKey || event.metaKey || event.shiftKey) return;
      if (!['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown', 'Home', 'End'].includes(event.key)) return;
      event.preventDefault();
      const index = filters.findIndex(item => item.id === filter), delta = event.key === 'ArrowUp' ? -2 : event.key === 'ArrowDown' ? 2 : event.key === 'ArrowLeft' ? -1 : 1;
      const next = event.key === 'Home' ? 0 : event.key === 'End' ? filters.length - 1 : (index + delta + filters.length) % filters.length;
      setFilter(filters[next].id); event.currentTarget.querySelectorAll('button')[next]?.focus({preventScroll: true});
    }}>{filters.map(item => <button key={item.id} type="button" aria-label={item.label} aria-describedby={`file-count-${item.id}`} aria-pressed={filter === item.id} tabIndex={filter === item.id ? 0 : -1} onClick={() => setFilter(item.id)}><svg width="14" height="14" viewBox="0 0 24 24" fill="none" aria-hidden="true" style={{visibility: filter === item.id ? 'visible' : 'hidden'}}><path d="m5 12 4 4 10-10" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" /></svg><span>{item.label}</span><span className="file-filter-count" aria-hidden="true">{counts[item.id]}</span><span id={`file-count-${item.id}`} className="sr-only">{loading ? 'Checking file counts' : `${counts[item.id]} matching files`}</span></button>)}</div>
    <p className="files-summary" role="status">{loading ? 'Checking file transfers…' : `${groups[0].items.length} transferring · ${groups[1].items.length} waiting · ${groups[3].items.length} ready · ${groups[2].items.length} need attention`}</p>
    {state.truncated && <p className="files-notice" role="status">Showing a limited activity list{state.total ? ` from at least ${state.total} files` : ''}. Unfinished transfers take priority over ready files. Sharing continues for files outside this list.</p>}
    {unavailable && <p className="files-notice" role="status">File status is temporarily unavailable. {state.items.length ? 'The last confirmed list is shown.' : 'No confirmed file list is available yet.'} Sharing continues in FL Studio; status checks retry with a longer pause.</p>}
    {!loading && !unavailable && !state.items.length && <p className="files-empty">{ended ? 'No sample transfers were recorded for this room.' : 'Add a sample to a shared channel or playlist in FL Studio. Its transfer will appear here.'}</p>}
    {loading && state.items.length > 0 && <p className="files-notice">Confirming saved file activity. The previous list is shown while checking.</p>}
    {state.items.length > 0 && <div className="files-results"><p className="files-matches" role="status">Showing {matches} of {state.items.length} {state.items.length === 1 ? 'file' : 'files'}</p><button type="button" className="text-button file-order" aria-label={order === 'name' ? 'Sort by recent activity' : 'Sort by file name'} title={order === 'name' ? 'Switch to newest activity first' : 'Switch to file name order'} onClick={() => setOrder(value => value === 'name' ? 'recent' : 'name')}>{order === 'name' ? 'Name ↑' : 'Recent ↓'}</button></div>}
    {state.items.length > 0 && !matches && <div className="files-no-matches"><p>No files match this search or filter.</p><button type="button" className="text-button" onClick={() => {setQuery(''); setFilter('all'); setDirection('all'); search.current?.focus({preventScroll: true});}}>Show all files</button></div>}
    {overflowing && <p id="files-scroll-help" className="files-scroll-hint">{scrollEdge.atBottom && scrollEdge.revision === visible ? 'End of file list' : 'Scroll for more files'}</p>}
    <div ref={list} className="files-list" tabIndex={overflowing ? 0 : undefined} aria-label="File transfer list" aria-describedby={overflowing ? 'files-scroll-help' : undefined} role="region" onScroll={event => {
      const target = event.currentTarget; memory.scroll = target.scrollTop;
      const atBottom = target.scrollTop + target.clientHeight >= target.scrollHeight - 2;
      setScrollEdge(old => old.atBottom === atBottom && old.revision === visible ? old : {atBottom, revision: visible});
    }}>
      {visible.map(group => group.items.length > 0 && <section key={group.title} aria-label={group.title}>
        <h3>{group.title}<span className="file-group-count" aria-label={`${group.items.length} files`}>{group.items.length}</span></h3>
        {group.title === 'Waiting' && <p className="file-group-help file-help">The shared edit is pending until its file is ready. Retries slow down during an outage; no extra sample edit is needed.</p>}
        {group.items.some(file => file.state === 'blocked') && <p className="file-group-help file-help">Checks the allowance again automatically after a minute. File-dependent edits wait in order; FL Studio stays open.</p>}
        <ul>{group.items.map(file => <li key={file.id}>
          <bdi className="file-name" title={file.name}>{file.name}</bdi>
          <span className="file-direction">{file.direction === 'upload' ? 'From this computer' : 'To this computer'}</span>
          <span className="file-status">{fileStatus(file)}</span>
          <span className="file-size">{fileAmount(file)}</span>
          {(file.state === 'uploading' || file.state === 'downloading') && <progress max="100" value={fileProgress(file)} aria-label={`${file.name}: ${fileStatus(file)}`} aria-valuetext={fileAmount(file)} />}
          {file.state === 'failed' && file.reason === 'network' && <span className="file-help">The connection retry did not finish. A later change to this sample can send it again.</span>}
          {file.state === 'missing' && <span className="file-help">Restore the file on the sending computer, then update that shared sample.</span>}
          {file.state === 'failed' && file.reason === 'verification' && <span className="file-help">The download was discarded after repeated verification failures. The original audio was kept.</span>}
          {file.state === 'failed' && (file.reason === 'too_large' || file.reason === 'empty') && <span className="file-help">Choose a smaller audio file that contains data, then add it again.</span>}
          {file.state === 'failed' && file.reason === 'source_changed' && <span className="file-help">Wait until the file is saved, then update that shared sample.</span>}
          {file.state === 'waiting' && file.reason === 'local_cache' && <span className="file-help">Check the received-files folder, drive access and free space. This edit retries automatically and keeps the room connected.</span>}
        </li>)}</ul>
      </section>)}
    </div>
    <button type="button" className="text-button files-refresh" disabled={loading || refreshing} aria-busy={loading || refreshing} onClick={() => refresh.current()}>{refreshing ? 'Checking files…' : 'Refresh files'}</button>
    </>}
    <p className="file-help">Ready files are reused by their contents. Full source paths stay out of this list.</p>
    </>}
  </aside>;
}
