<script lang="ts">
  export let value: string | number = '';
  export let group: string | number = '';
  export let disabled: boolean = false;
  export let error: boolean = false;
  export let componentClass = '';
  export let componentStyle = '';

  let checked: boolean = false;
  $: checked = group === value;

  function handleChange() {
    group = value;
  }
</script>

<template>
  <label class="radio {componentClass}" style={componentStyle}>
    <input type="radio" {checked} {disabled} on:change={handleChange} />
    <div class="main" class:disabled class:error>
      <div class="holder">
        <div class="mark" />
      </div>
      <slot />
    </div>
  </label>
</template>
