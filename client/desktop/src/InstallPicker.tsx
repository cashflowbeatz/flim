import { useEffect, useLayoutEffect, useRef, useState, type KeyboardEvent } from 'react';
import type { Install } from './types';

interface Props {
  options: (Install & {label: string})[];
  value: string;
  onChange: (id: string) => void;
  disabled: boolean;
  loading: boolean;
}

export default function InstallPicker({options, value, onChange, disabled, loading}: Props) {
  const [open, setOpen] = useState(false);
  const [active, setActive] = useState(value);
  const root = useRef<HTMLDivElement>(null);
  const trigger = useRef<HTMLButtonElement>(null);
  const list = useRef<HTMLDivElement>(null);
  const search = useRef({text: '', at: 0});
  const selected = options.find(option => option.id === value);
  const enabled = !disabled && options.length > 0;
  const expanded = open && enabled;
  const activeIndex = Math.max(0, options.findIndex(option => option.id === active));
  const optionId = (index: number) => `fl-install-option-${index}`;

  useEffect(() => {setOpen(false); setActive(value); search.current.text = '';}, [disabled, value]);
  useEffect(() => {
    if (open && !options.some(option => option.id === active)) setActive(value);
  }, [options, active, open, value]);
  useEffect(() => {
    if (!expanded) return;
    const dismiss = (event: PointerEvent) => {
      if (event.target instanceof Node && !root.current?.contains(event.target)) {setOpen(false); search.current.text = '';}
    };
    document.addEventListener('pointerdown', dismiss);
    return () => document.removeEventListener('pointerdown', dismiss);
  }, [expanded]);
  useLayoutEffect(() => {
    const container = list.current, option = container?.children[activeIndex];
    if (!expanded || !container || !option) return;
    const outer = container.getBoundingClientRect(), inner = option.getBoundingClientRect();
    if (inner.top < outer.top) container.scrollTop -= outer.top - inner.top;
    else if (inner.bottom > outer.bottom) container.scrollTop += inner.bottom - outer.bottom;
  }, [activeIndex, expanded]);

  function choose(id: string) {
    if (!enabled || !options.some(option => option.id === id)) return;
    setOpen(false); search.current.text = '';
    onChange(id); trigger.current?.focus({preventScroll: true});
  }

  function keys(event: KeyboardEvent<HTMLButtonElement>) {
    if (!enabled || event.defaultPrevented || event.nativeEvent.isComposing || event.keyCode === 229 || event.ctrlKey || event.metaKey || event.shiftKey) return;
    if (!event.altKey && event.key === 'F4' || event.altKey && ['ArrowDown', 'ArrowUp'].includes(event.key)) {
      event.preventDefault(); search.current.text = '';
      if (event.key === 'ArrowUp') setOpen(false);
      else if (event.key === 'ArrowDown') {setActive(value); setOpen(true);}
      else {setActive(value); setOpen(current => !current);}
      return;
    }
    if (event.altKey) return;
    if (event.key === 'Escape' && expanded) {
      event.preventDefault(); event.stopPropagation(); setOpen(false); search.current.text = ''; return;
    }
    if (event.key === 'Tab') {setOpen(false); return;}
    if (['Enter', ' '].includes(event.key)) {
      event.preventDefault();
      if (expanded) choose(options[activeIndex].id);
      else {setActive(value); setOpen(true);}
      return;
    }
    if (['ArrowDown', 'ArrowUp', 'Home', 'End'].includes(event.key)) {
      event.preventDefault(); search.current.text = '';
      const index = event.key === 'Home' ? 0 : event.key === 'End' ? options.length - 1 :
        expanded ? Math.min(options.length - 1, Math.max(0, activeIndex + (event.key === 'ArrowDown' ? 1 : -1))) :
        Math.max(0, options.findIndex(option => option.id === value));
      setActive(options[index].id); setOpen(true); return;
    }
    if (event.key.length !== 1 || event.key === ' ') return;
    event.preventDefault();
    const now = Date.now();
    const text = (now - search.current.at > 700 ? '' : search.current.text) + event.key.toLowerCase();
    search.current = {text, at: now};
    const repeat = [...text].every(character => character === text[0]);
    const lookup = repeat ? text[0] : text;
    const candidates = repeat && expanded ? [...options.slice(activeIndex + 1), ...options.slice(0, activeIndex + 1)] : options;
    const match = candidates.find(option => option.label.toLowerCase().startsWith(lookup) || option.version.split('.')[0].startsWith(lookup));
    if (match) {setActive(match.id); setOpen(true);}
  }

  return <div ref={root} className="install-picker" onBlur={event => {
    if (!event.currentTarget.contains(event.relatedTarget)) {setOpen(false); search.current.text = '';}
  }}>
    <button ref={trigger} id="fl-install" type="button" role="combobox" className="install-trigger" value={value}
      aria-labelledby="fl-install-label" aria-describedby="install-help" aria-description={selected ? `Build ${selected.version}. ${selected.supported ? 'Supported installation.' : selected.reason}` : undefined} aria-expanded={expanded}
      aria-controls={expanded ? 'fl-install-list' : undefined} aria-haspopup="listbox"
      aria-activedescendant={expanded ? optionId(activeIndex) : undefined} disabled={!enabled}
      onKeyDown={keys} onClick={() => {setActive(value); search.current.text = ''; setOpen(current => !current);}}>
      <span>{selected?.label || (loading ? 'Finding your installation…' : 'No installation found')}</span>
      <svg width="18" height="18" viewBox="0 0 24 24" fill="none" aria-hidden="true"><path d="m6 9 6 6 6-6" stroke="currentColor" strokeWidth="1.7" strokeLinecap="round" strokeLinejoin="round" /></svg>
    </button>
    {expanded && <div ref={list} id="fl-install-list" role="listbox" aria-labelledby="fl-install-label" className="install-options">
      {options.map((option, index) => <div id={optionId(index)} key={option.id} role="option" data-value={option.id}
        aria-selected={option.id === value} aria-description={`Build ${option.version}. ${option.supported ? 'Supported installation.' : option.reason}`} title={`Build ${option.version}${option.supported ? '' : `: ${option.reason}`}`} data-active={index === activeIndex}
        className="install-option" onPointerMove={event => {if (event.pointerType === 'mouse') setActive(option.id);}} onPointerDown={event => event.preventDefault()} onClick={() => choose(option.id)}>
        <span>{option.label}</span>
        {option.id === value && <svg width="18" height="18" viewBox="0 0 24 24" fill="none" aria-hidden="true"><path d="m5 12 4 4 10-10" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round" /></svg>}
      </div>)}
    </div>}
  </div>;
}
