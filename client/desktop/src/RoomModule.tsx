import type {ReactNode} from 'react';
import {motion, useReducedMotion} from 'framer-motion';

export default function RoomModule({variant, nv4C0a_ii: image, className, heroContent, expandedContent, footerContent}: {
  variant: 'Opened' | 'Closed'; nv4C0a_ii: string; className?: string;
  heroContent: ReactNode; expandedContent: ReactNode; footerContent: ReactNode;
}) {
  const reduced = useReducedMotion();
  const open = variant === 'Opened';
  return <section className={`room-module ${className || ''}`} aria-label="flim Rooms">
    <div className="module-artwork" hidden={open}>
      <img src={image} alt="" draggable={false} />{heroContent}
    </div>
    {open && <motion.div className="module-body" initial={reduced ? false : {opacity: 0}}
      animate={{opacity: 1}} transition={{duration: .18, ease: 'easeOut'}}>{expandedContent}</motion.div>}
    {footerContent}
  </section>;
}
