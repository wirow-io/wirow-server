<script lang="ts">
  export let size = 40;
  export let status = 'white';
  export let componentClass = '';
  export let componentStyle = '';
  export let caption = '';
</script>

<template>
  <div class="loader {componentClass}" style={componentStyle}>
    <svg class="icon" width={size} height={size} viewBox="25 25 50 50">
      <circle class="path {status}" cx="50" cy="50" r="20" fill="none" stroke-width="6" stroke-miter-limit="10" />
    </svg>
    {#if caption != ''}
      <div class="caption">
        {caption}
      </div>
    {/if}
  </div>
</template>
