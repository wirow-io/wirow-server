<script lang="ts">
  export let rowPadding = 0;
  export let rounded: boolean = false;
  export let componentClass = '';
  export let componentStyle = '';
</script>

<template>
  <table
    class="table {componentClass}"
    class:rowPadding
    class:rounded
    style="border-spacing: 0 {rowPadding}px;{componentStyle}">
    <slot />
  </table>
</template>
