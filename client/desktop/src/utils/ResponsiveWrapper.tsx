import { useEffect, useState, type ComponentType } from 'react';

const breakpoints = {base: 0, sm: 390, md: 810, lg: 1200, xl: 1440, '2xl': 1920};
export default function ResponsiveWrapper({Component, variants, ...props}: {
  Component: ComponentType<any>; variants: Partial<Record<keyof typeof breakpoints, string>>; [key: string]: unknown;
}) {
  const [width, setWidth] = useState(window.innerWidth);
  useEffect(() => {
    const queries = Object.values(breakpoints).map(value => window.matchMedia(`(min-width: ${value}px)`));
    const update = () => setWidth(window.innerWidth);
    queries.forEach(query => query.addEventListener('change', update));
    return () => queries.forEach(query => query.removeEventListener('change', update));
  }, []);
  const variant = Object.entries(breakpoints).reverse().find(([key, min]) => width >= min && variants[key as keyof typeof variants]);
  return <Component {...props} variant={variant ? variants[variant[0] as keyof typeof variants] : undefined} />;
}
