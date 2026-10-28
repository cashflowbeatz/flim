import {Fragment, useEffect, useRef, useState} from 'react';
import type {SampleLocation as Location} from './types';

export default function SampleLocation({locked}: {locked: boolean}) {
  const api = window.flimDesktop;
  const [location, setLocation] = useState<Location | null>(null);
  const [busy, setBusy] = useState('');
  const [error, setError] = useState('');
  const [notice, setNotice] = useState('');
  const [attempt, setAttempt] = useState(0);
  const live = useRef(false);
  const flight = useRef(false);
  const chooseButton = useRef<HTMLButtonElement>(null);
  const focusOwner = useRef<Element | null>(null);
  const restoreFocus = useRef(false);
  useEffect(() => {
    live.current = true;
    let owned = true;
    if (api?.getSampleLocation) {
      setBusy('Loading folder…'); setError('');
      api.getSampleLocation().then(value => {if (owned) setLocation(value);})
        .catch(() => {if (owned) setError('The saved folder could not be checked. Retry to see where received files will be saved.');})
        .finally(() => {if (owned) setBusy('');});
    }
    return () => {owned = false; live.current = false;};
  }, [api, attempt, locked]);
  useEffect(() => {
    if (busy || !restoreFocus.current) return;
    restoreFocus.current = false;
    if (document.activeElement !== document.body && document.activeElement !== focusOwner.current) return;
    const owner = focusOwner.current;
    if (owner instanceof HTMLButtonElement && owner.isConnected && !owner.disabled) owner.focus({preventScroll: true});
    else chooseButton.current?.focus({preventScroll: true});
  }, [busy, location, error]);

  async function act(action: 'choose' | 'reset' | 'open') {
    if (!api || flight.current || busy || (action !== 'open' && (locked || !location?.canChange)) || !(action === 'open' ? api.openSampleFolder : action === 'choose' ? api.chooseSampleLocation : api.resetSampleLocation)) return;
    focusOwner.current = document.activeElement;
    flight.current = true; setBusy(action === 'open' ? 'Opening samples…' : action === 'reset' ? 'Restoring default…' : 'Choosing folder…'); setError(''); setNotice('');
    try {
      if (action === 'open') await api.openSampleFolder();
      else {
        const next = await (action === 'choose' ? api.chooseSampleLocation() : api.resetSampleLocation());
        if (live.current) {
          setLocation(next);
          if (next.path !== location?.path || next.custom !== location?.custom || location?.error) setNotice('Folder saved for your next FL session. Existing files stay where they are.');
        }
      }
    } catch (error) {if (live.current) setError(error instanceof Error ? error.message : 'The samples folder could not be used. Try again.');}
    finally {flight.current = false; if (live.current) {restoreFocus.current = true; setBusy('');}}
  }

  return <section className="sample-location" aria-label="Received files folder" aria-busy={Boolean(busy)}>
    {api?.getSampleLocation && <>
      <h3>Save received files to</h3>
      {location ? <><bdi className="sample-location-path" dir="ltr">{location.path.split(/(?<=[\\/])/).map((part, index) => <Fragment key={index}>{part}<wbr /></Fragment>)}</bdi><p className="file-help">{location.custom ? 'Your chosen folder' : 'Default samples folder'}</p></> : <p className="file-help" role="status">{busy || 'Folder not available'}</p>}
      {location?.error && <p className="files-notice" role="alert">{location.error}</p>}
      <div className="sample-location-actions">
        <button ref={chooseButton} type="button" className="text-button" disabled={!api?.chooseSampleLocation || Boolean(busy) || locked || !location?.canChange} onClick={() => void act('choose')}>{busy === 'Choosing folder…' ? busy : 'Choose folder'}</button>
        {(location?.custom || location?.error) && <button type="button" className="text-button" disabled={!api?.resetSampleLocation || Boolean(busy) || locked || !location.canChange} onClick={() => void act('reset')}>{busy === 'Restoring default…' ? busy : 'Use default'}</button>}
      </div>
      <p className="file-help">{locked ? 'Return to start to change this folder. Running FL sessions keep their current location.' : 'Applies to your next FL session on this computer. Existing files are kept.'}</p>
    </>}
    <button type="button" className="text-button files-folder" disabled={!api?.openSampleFolder || Boolean(busy) || Boolean(location?.error)} onClick={() => void act('open')}>{busy === 'Opening samples…' ? busy : 'Open samples folder'}</button>
    {location && busy && <p className="file-help" role="status">{busy}</p>}
    {error && <div><p className="files-notice" role="alert">{error}</p>{!location && api?.getSampleLocation && <button type="button" className="text-button" disabled={Boolean(busy)} onClick={() => setAttempt(value => value + 1)}>Retry folder check</button>}</div>}
    {notice && <p className="file-help" role="status">{notice}</p>}
  </section>;
}
