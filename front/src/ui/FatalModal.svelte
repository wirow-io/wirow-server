<script lang="ts">
  ///
  /// Fatal modal component.
  /// Used to display unescapable errors
  ///

  import Box from '../kit/Box.svelte';

  export let title: string;

  export let content: string | undefined;
</script>

<template>
  <Box componentClass="error modal-medium-width" hoverable>
    <h3>{title}</h3>
    <div style="padding-bottom:1em">{content || ''}</div>
  </Box>
</template>
