import { LayoutGroup } from 'framer-motion';
import { useId, type ReactNode } from 'react';

export default function LayoutIsland({children}: {children: ReactNode}) {
  return <LayoutGroup id={useId()} inherit={false}>{children}</LayoutGroup>;
}
