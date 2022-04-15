<script lang="ts">
  import { onMount, createEventDispatcher } from 'svelte';
  import { onValidate, getValidate } from '../validate';
  import type { ValueValidator } from '../validate';
  import Icon from './Icon.svelte';

  export let id: string | undefined = undefined;
  export let title: string | undefined = undefined;
  export let placeholder = '';
  export let name: string | undefined = undefined;
  export let type: 'text' | 'password' | 'textarea' = 'text';
  export let size: 'big' | 'normal' | 'small' = 'big';
  export let flavor: 'error' | 'warning' | 'success' | undefined = undefined;
  export let error: string | undefined = undefined;
  export let required: boolean = false;
  export let disabled: boolean = false;
  export let readonly: boolean = false;
  export let autofocus: boolean = false;
  export let autocomplete: string | undefined = undefined;
  export let rows: number = 3;
  export let icon: any = undefined; // Icon spec
  export let iconClass: string | undefined = undefined;
  export let maxlength: number | undefined = undefined;
  export let tooltip: string | undefined = undefined;
  export let value = '';
  export let validator: ValueValidator | undefined = undefined;
  export let noValidateOnInput = false;
  export let rounded = true;
  export let componentClass = '';
  export let componentStyle = '';
  export let onLeft = false;

  const hasDefaultSlot = arguments[1].$$slots?.default != null;
  const dispatch = createEventDispatcher();

  let hasSide = false;
  let hasErrorBorder = false;
  let input: any;
  const validate = getValidate(validator == null);

  $: hasSide = icon != null || hasDefaultSlot || flavor != null;
  $: hasErrorBorder = error != null || flavor === 'error';

  onMount(() => {
    autofocus && input.focus();
  });

  function onBlur() {
    if (validate().length == 0) {
      value = input.value;
    }
  }

  function onInput() {
    if (!noValidateOnInput && validate().length == 0) {
      value = input.value;
    }
  }

  function onKeyDown(ev: KeyboardEvent) {
    if (ev.key === 'Enter') {
      dispatch('action');
    } else if (ev.key === 'Escape') {
      dispatch('escape');
    }
  }

  onValidate((errors: string[]) => {
    error = validator?.(errors, input?.value);
    return validator;
  });
</script>

<template>
  <div class="input {componentClass}" class:disabled style={componentStyle}>
    {#if title != null && size === 'big'}
      <div class:error class="title {flavor ? flavor : ''}" class:required>{error || title}</div>
    {/if}
    {#if type === 'textarea'}
      <textarea
        {id}
        bind:this={input}
        class:error-border={hasErrorBorder}
        class:has-right-side={hasSide && !onLeft}
        class:has-left-side={hasSide && onLeft}
        class:no-title={!title}
        class:no-rounded={!rounded}
        class={size}
        on:blur={onBlur}
        on:blur
        on:input={onInput}
        on:input
        {disabled}
        {maxlength}
        {name}
        {placeholder}
        {readonly}
        {rows}
        {value}
      />
    {:else}
      <input
        {id}
        bind:this={input}
        class:error-border={hasErrorBorder}
        class:has-right-side={hasSide && !onLeft}
        class:has-left-side={hasSide && onLeft}
        class:no-title={!title}
        class:no-rounded={!rounded}
        class={size}
        on:blur={onBlur}
        on:blur
        on:input={onInput}
        on:input
        on:keydown={onKeyDown}
        {autocomplete}
        {disabled}
        {maxlength}
        {name}
        {placeholder}
        {readonly}
        {type}
        {value}
      />
    {/if}

    {#if hasSide}
      <div class:after={hasSide && !onLeft} class:before={hasSide && onLeft}>
        {#if icon != null}
          <Icon
            size={size === 'small' ? 24 : 32}
            {icon}
            {tooltip}
            tooltipClass={flavor ? `tooltip-${flavor}` : ''}
            componentClass="input-icon {flavor ? `input-icon-${flavor}` : ''} {iconClass || ''}"
          />
        {/if}
      </div>
    {/if}
  </div>
</template>
