<script lang="ts">
  import type { Svg } from '../kit/icons';
  import { createIconSvg } from '../kit/icons';
  import { createEventDispatcher, getContext } from 'svelte';

  export let icon: any = null;
  export let iconAfter: any = null;
  export let transitionFn: any = undefined;
  export let hoverable: boolean = false;
  export let closeable: boolean = false;
  export let clickable: boolean = false;
  export let actionAcceptorName = 'actionAcceptor';
  export let componentClass = '';
  export let componentStyle = '';

  const dispatch = createEventDispatcher();
  const actionAcceptor = getContext(actionAcceptorName);

  let iconSvg: Svg | null = null;
  let iconSvgAfter: Svg | null = null;

  function onKeyDown(ev: KeyboardEvent) {
    if (ev.key === 'Enter') {
      dispatch('action');
    }
  }

  $: iconSvg = icon ? createIconSvg(icon) : null;
  $: iconSvgAfter = iconAfter ? createIconSvg(iconAfter) : null;

  function transition(el: any, params?: any): any {
    return transitionFn ? transitionFn(el, params) : null;
  }

  function onActionAfter(ev: MouseEvent) {
    if (typeof actionAcceptor === 'function') {
      actionAcceptor(ev);
    }
  }

  function onActionAfterKey(ev: KeyboardEvent) {
    if (ev.key === 'Enter' && typeof actionAcceptor === 'function') {
      actionAcceptor(ev);
    }
  }
</script>

<template>
  <div
    in:transition|local
    tabindex={clickable ? 0 : -1}
    class:clickable
    class:hoverable
    class="box {icon ? 'icon-box-before' : ''} {iconAfter ? 'icon-box-after' : ''} {componentClass}"
    style={componentStyle}
    on:keydown={onKeyDown}
    on:click
  >
    {#if iconSvg}
      <div class="svg-before">
        <svg viewBox="0 0 {iconSvg.viewbox} {iconSvg.viewbox}">
          <g>
            {#each iconSvg.paths as p}
              <path style={p.color ? `fill:${p.color}` : undefined} d={p.path} />
            {/each}
          </g>
        </svg>
      </div>
    {/if}
    <div class="slot">
      {#if closeable}
        <div class="close" on:click|stopPropagation|once={() => dispatch('close')}><strong>x</strong></div>
      {/if}
      <slot />
    </div>
    {#if iconSvgAfter}
      <div
        tabindex="0"
        class="svg-after"
        on:click|stopPropagation={onActionAfter}
        on:keydown|stopPropagation={onActionAfterKey}
      >
        <svg viewBox="0 0 {iconSvgAfter.viewbox} {iconSvgAfter.viewbox}">
          <g>
            {#each iconSvgAfter.paths as p}
              <path style={p.color ? `fill:${p.color}` : undefined} d={p.path} />
            {/each}
          </g>
        </svg>
      </div>
    {/if}
  </div>
</template>
