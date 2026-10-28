import {useLayoutEffect, useRef, useState, type RefObject} from 'react';

export default function useOverflow(ref: RefObject<HTMLElement>, revision: unknown) {
  const [overflow, setOverflow] = useState(false);
  const measured = useRef(false);
  useLayoutEffect(() => {
    const element = ref.current;
    const update = (next: boolean) => {
      if (next !== measured.current) {measured.current = next; setOverflow(next);}
    };
    if (!element) {update(false); return;}
    const measure = () => update(element.clientHeight > 0 && element.scrollHeight > element.clientHeight + 1);
    const observer = new ResizeObserver(measure);
    observer.observe(element);
    for (const child of element.children) observer.observe(child);
    measure();
    return () => observer.disconnect();
  }, [ref, revision]);
  return overflow;
}
