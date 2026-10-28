import { useEffect } from 'react';

// Use only on a confirmed runtime-owned style conflict. Comparing first avoids observer loops.
export default function useStripFramerStyles(selector: string, properties: Record<string, string>) {
  const serialized = JSON.stringify(properties);
  useEffect(() => {
    const el = document.querySelector<HTMLElement>(selector);
    if (!el) return;
    const values: Record<string, string> = JSON.parse(serialized);
    const strip = () => {
      for (const [property, value] of Object.entries(values)) {
        if (el.style.getPropertyValue(property) !== value) el.style.setProperty(property, value);
      }
    };
    strip();
    const observer = new MutationObserver(strip);
    observer.observe(el, {attributes: true, attributeFilter: ['style']});
    return () => observer.disconnect();
  }, [selector, serialized]);
}
