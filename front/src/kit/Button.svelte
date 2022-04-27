<script lang="ts">
  ///  Button.
  ///
  /// Slots:
  ///   - default Button text. Optional.
  ///
  /// Props:
  ///   - icon Icon instance to show on button. Optional.
  ///   - iconColor Color of button icon. Optional.
  ///   - iconClass Additional CSS class of button icon. Optional.
  ///   - specialStyle Special button style. Optional.

  import { createEventDispatcher, onMount } from 'svelte';
  import { getContext, get_current_component } from 'svelte/internal';
  import type { SBindFn } from '../interfaces';
  import { ifClassThen } from '../utils';
  import type { ButtonComponentBind } from './button';
  import Icon from './Icon.svelte';
  import Loader from './Loader.svelte';

  export let id: string | undefined = undefined;
  export let icon: any = null;
  export let toggleIcon: any = null;
  export let toggled: boolean = false;
  export let iconColor: string | undefined = undefined;
  export let toggleIconColor: string | undefined = undefined;
  export let iconClass: string | undefined = undefined;
  export let toggleIconClass: string | undefined = undefined;
  export let specialStyle: '1' | undefined = undefined;
  export let title: string | undefined = undefined;
  export let disabled = false;
  export let loading = false;
  export let clickable = true;
  export let autofocus = false;
  export let autotoggle = true;
  export let attention = false;
  export let attentionIconColor: string | undefined = undefined;
  export let attentionIconClass: string | undefined = undefined;
  export let hidden = false;
  export let bind: SBindFn<ButtonComponentBind> | undefined = undefined;
  export let actionAcceptorName = 'actionAcceptor';
  export let componentClass = '';
  export let componentStyle = '';

  let iconClassComplete: string | undefined = undefined;
  let componentClassComplete: string | undefined = undefined;
  let button: HTMLElement;

  const props$$ = arguments[1];
  const withText = props$$.$$slots?.default != null;
  const dispatch = createEventDispatcher();
  const iconSize = ifClassThen(componentClass, ['x', 'xx'], [36, 48], 24);

  const actionAcceptor = getContext(actionAcceptorName);

  function onClick(ev: any) {
    if (clickable) {
      if (autotoggle) {
        toggled = !toggled;
      }
      dispatch('click', toggled);
      if (typeof actionAcceptor === 'function') {
        actionAcceptor(ev);
      }
    }
  }

  function iconClassFn(..._: any): string | undefined {
    const ret: string[] = [];
    if (withText) {
      ret.push('with-text');
    }
    if (!loading) {
      if (toggled && toggleIconClass) {
        ret.push(toggleIconClass);
      } else if (iconClass) {
        ret.push(iconClass);
      }
    }
    return ret.length ? ret.join(' ') : undefined;
  }

  function buttonClassFn(..._: any[]): string | undefined {
    const ret: string[] = [];
    if (specialStyle) {
      ret.push(`s${specialStyle}`);
    }
    if (componentClass) {
      ret.push(componentClass);
    }
    return ret.length ? ret.join(' ') : undefined;
  }

  bind?.(get_current_component(), {
    get disabled(): boolean {
      return disabled;
    },
    set disabled(v: boolean) {
      disabled = v;
    },
    get toggled(): boolean {
      return toggled;
    },
    set toggled(v: boolean) {
      toggled = v;
    },
  });

  onMount(() => {
    autofocus && button.focus();
  });

  $: iconClassComplete = iconClassFn(toggled, toggleIconClass, iconClass);
  $: componentClassComplete = buttonClassFn(componentClass, specialStyle);
</script>

<template>
  <button
    {id}
    bind:this={button}
    type="button"
    {title}
    class={componentClassComplete}
    style={componentStyle}
    disabled={disabled || loading}
    class:disabled
    class:hidden
    class:non-clickable={!clickable}
    on:click|stopPropagation={onClick}
  >
    {#if loading}
      <Loader size={iconSize} componentClass={iconClassComplete} />
    {:else if toggled && toggleIcon}
      <Icon size={iconSize} icon={toggleIcon} color={toggleIconColor} componentClass={iconClassComplete} />
    {:else if icon}
      <Icon size={iconSize} {icon} color={iconColor} componentClass={iconClassComplete} />
    {/if}
    {#if attention}
      <svg
        style={attentionIconColor ? `fill:${attentionIconColor}` : ''}
        class="attention {attentionIconColor || attentionIconClass ? `${attentionIconClass || ''}` : 'attention-color'}"
        viewBox="25 25 50 50"
      >
        <circle cx="50" cy="50" r="20" />
      </svg>
    {/if}
    <span class="text">
      <slot />
    </span>
  </button>
</template>
