<script lang="ts" context="module">
  export interface Option {
    label: string;
    value?: any;
  }

  function compareValueEq(o1: any, o2: any): boolean {
    return o1 == o2;
  }
</script>

<script lang="ts">
  import angleDownIcon from './icons/angleDown';
  import Input from './Input.svelte';
  import Dropdown from './Dropdown.svelte';
  import { createEventDispatcher } from 'svelte';
  import type { DropdownBind } from './dropdown';

  export let title: string | undefined = undefined;
  export let label = '';
  export let value: any = undefined;
  export let options: Option[] = [];
  export let size: 'big' | 'normal' | 'small' = 'big';
  export let placeholder = '';
  export let flavor: 'error' | 'warning' | 'success' | undefined = undefined;
  export let required: boolean = false;
  export let disabled: boolean = false;
  export let maxlength: number | undefined = undefined;
  export let tooltip: string | undefined = undefined;
  export let valueComparator: (o1: any, o2: any) => boolean = compareValueEq;
  export let componentClass = '';

  const dispatch = createEventDispatcher();
  let isActive = false;
  let dropdownBind: DropdownBind | undefined;
  let selectRef: HTMLElement;

  function focus() {
    const el = selectRef && selectRef.getElementsByTagName('input')[0];
    if (el) {
      window.setTimeout(() => {
        el.focus();
      }, 0);
    }
  }

  function select(_value: any, _label: string) {
    if (!valueComparator(value, _value)) {
      value = _value;
      dispatch('change', { label: _label, value: _value });
    }
    dropdownBind!.toggled = false;
    focus();
  }

  function onAction() {
    dropdownBind!.toggled = !dropdownBind!.toggled;
  }

  function onItemKeydown(ev: KeyboardEvent, val: any, label: any) {
    if (!dropdownBind!.toggled) {
      return;
    }
    if (ev.key === 'Escape') {
      dropdownBind!.toggled = false;
      focus();
    } else if (ev.key === 'Enter') {
      select(val, label);
    }
  }

  function onInputEscape() {
    dropdownBind!.toggled = false;
    focus();
  }

  function update(..._: any[]) {
    let selected = options.find((o) => valueComparator(o.value, value)) || options[0];
    const newLabel = selected ? selected.label : '';
    if (newLabel != label) {
      label = newLabel;
    }
    if (!valueComparator(value, selected?.value)) {
      value = selected ? selected.value : undefined;
    }
  }

  function onBlur(ev: UIEvent) {
    const el = ev.target as HTMLElement;
    const pel = el.parentElement;
    if (!el || !pel) {
      return;
    }
    window.setTimeout(() => {
      for (let i = 0; i < pel.children.length; ++i) {
        const cel = pel.children[i];
        if (cel != el && document.activeElement === cel) {
          return;
        }
      }
      dropdownBind!.toggled = false;
      focus();
    }, 0);
  }

  $: update(options, value);
</script>

<template>
  <div class="select {componentClass}" bind:this={selectRef}>
    <Dropdown
      {disabled}
      bind={(_, bind) => {
        dropdownBind = bind;
      }}
      on:open={() => (isActive = true)}
      on:close={() => (isActive = false)}
    >
      <Input
        value={label}
        on:action={onAction}
        on:escape={onInputEscape}
        {title}
        {placeholder}
        readonly
        {flavor}
        {required}
        {disabled}
        {size}
        {maxlength}
        {tooltip}
        icon={angleDownIcon}
        iconClass={isActive ? 'dd-icon-open dd-icon' : 'dd-icon'}
      />
      <div slot="dropdown">
        {#each options as opt (opt.value)}
          <div
            class="item"
            tabindex="0"
            on:keydown|stopPropagation={(ev) => onItemKeydown(ev, opt.value, opt.label)}
            on:click|stopPropagation={() => select(opt.value, opt.label)}
            on:blur={onBlur}
          >
            {opt.label}
          </div>
        {/each}
      </div>
    </Dropdown>
  </div>
</template>
