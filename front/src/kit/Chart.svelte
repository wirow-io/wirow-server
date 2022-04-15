<script lang="ts">
  import type { Chart } from 'chart.js';
  import { onMount } from 'svelte';

  export let chartBuilder: (canvas: HTMLCanvasElement | undefined) => Promise<Chart | undefined>;
  export let componentClass = '';
  export let componentStyle = '';

  let canvas: HTMLCanvasElement | undefined;
  let chart: Chart | undefined;

  async function init() {
    chart = await chartBuilder(canvas!);
  }

  onMount(() => {
    init();
    return () => {
      if (chart) {
        chart.destroy();
        chart = undefined;
        chartBuilder(undefined);
      }
    };
  });
</script>

<template>
  <div class="chart {componentClass || ''}" style="{componentStyle || ''}">
    <canvas bind:this={canvas} />
  </div>
</template>
