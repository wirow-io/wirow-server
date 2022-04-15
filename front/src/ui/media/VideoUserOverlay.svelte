<script lang="ts">
  export let name: string | undefined = undefined;
</script>

<template>
  <div class="video-overlay">
    <!-- svelte-ignore a11y-missing-attribute -->
    <div class="video-user-overlay"><img src="monster/{name || ''}" /></div>
  </div>
</template>
