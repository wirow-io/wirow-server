<script lang="ts">
  export let checked: boolean = false;
  export let disabled: boolean = false;
  export let title: string | undefined = undefined;
  export let left: string | undefined = undefined;
  export let right: string | undefined = undefined;
  export let componentClass = '';
  export let componentStyle = '';
</script>

<!-- svelte-ignore a11y-label-has-associated-control -->
<template>
  <label class="switch {componentClass}" style={componentStyle} class:disabled>
    {#if title}
      <div class="title">{title}</div>
    {/if}
    <div class="wrapper">
      {#if left}
        <div class="left">{left}</div>
      {/if}
      <input type="checkbox" bind:checked {disabled} />
      <div class="main" class:disabled>
        <div class="knob" />
      </div>
      {#if right}
        <div class="right">{right}</div>
      {/if}
    </div>
  </label>
</template>
