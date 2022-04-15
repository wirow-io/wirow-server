<script lang="ts">
  import Icon from './Icon.svelte';
  import checkMarkIcon from './icons/checkMark';

  export let checked: boolean = false;
  export let error: boolean = false;
  export let disabled: boolean = false;
  export let componentClass = '';
  export let componentStyle = '';
</script>

<template>

  <label class="checkbox {componentClass}" style={componentStyle}>
    <input type="checkbox" bind:checked {disabled} />
    <div class="main" class:disabled class:error>
      <div class="holder">
        <div class="mark">
          <Icon icon={checkMarkIcon} size="0.8rem" />
        </div>
      </div>
      <slot />
    </div>
  </label>
</template>
