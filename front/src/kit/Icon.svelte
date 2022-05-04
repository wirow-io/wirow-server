<script lang="ts">
  import type { IconSpec, Svg } from '../kit/icons';
  import { createIconSvg } from '../kit/icons';
  import Tooltip from './Tooltip.svelte';

  export let icon: IconSpec = '';
  export let size: number | string = 32;
  export let color: string | undefined = undefined;
  export let tooltip: string | undefined = undefined;
  export let title = '';
  export let tooltipClass: string | undefined = undefined;
  export let componentClass = '';
  export let componentStyle = '';
  export let clickable = false;

  let iconSvg: Svg;
  let sizeAttr: string | number;
  let sizeStyle: string;

  if (clickable) {
    componentClass = `${componentClass} clickable`;
  }

  function updateSize(..._: any[]) {
    sizeStyle =
      size === ''
        ? ''
        : typeof size === 'number'
        ? `min-width:${size}px;min-height:${size}px;`
        : `min-width:${size};min-height:${size};`;

    if (typeof size === 'number') {
      sizeAttr = size;
    } else if (size.endsWith('px')) {
      sizeAttr = size.substring(0, size.length - 2);
    } else {
      sizeAttr = 0;
    }
  }

  $: iconSvg = createIconSvg(icon);
  $: updateSize(size);
</script>

<template>
  {#if tooltip != null}
    <Tooltip componentClass={tooltipClass || ''}>
      <svg
        width={sizeAttr}
        height={sizeAttr}
        viewBox="0 0 {iconSvg.viewbox}
        {iconSvg.viewbox}"
        class="icon {componentClass}"
        style="{sizeStyle}{componentStyle}"
        on:click
      >
        <g>
          {#each iconSvg.paths as p}
            <path style={color || p.color ? `fill:${color || p.color}` : ''} d={p.path} />
          {/each}
        </g>
      </svg>
      <div slot="tooltip">{tooltip}</div>
    </Tooltip>
  {:else}
    <svg
      width={sizeAttr}
      height={sizeAttr}
      viewBox="0 0 {iconSvg.viewbox}
      {iconSvg.viewbox}"
      class="icon {componentClass}"
      style="{sizeStyle} {componentStyle}"
      on:click
    >
      {#if title !== ''}<title>{title}</title>{/if}
      <g>
        {#each iconSvg.paths as p}
          <path style={color || p.color ? `fill:${color || p.color}` : ''} d={p.path} />
        {/each}
      </g>
    </svg>
  {/if}
</template>
