import type { Readable } from 'svelte/store';
import type { SComponent, SEvent } from '../interfaces';

export interface ActivityBarSlot {
  id: string;
  side: 'top' | 'bottom';
  icon: string;
  props?: Record<string, any>;
  onAction: (ev: SEvent) => void;
  active?: boolean;
  activeStore?: Readable<boolean>;
  buttonClass?: string;
  state?: Record<string, any>;
  sideComponent?: SComponent;
  sideProps?: Record<string, any>;
  mainComponent?: SComponent;
  mainProps?: Record<string, any>;
  attentionStore?: Readable<boolean>;
  visible?: () => boolean;
  tooltip: () => string;
}
