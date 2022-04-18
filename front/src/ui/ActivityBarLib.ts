/*
 * Copyright (C) 2022 Greenrooms, Inc.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/
 */

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
