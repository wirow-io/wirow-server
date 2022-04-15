<!-- svelte-ignore unused-export-let -->
<script lang="ts">
  import { createEventDispatcher, onMount } from 'svelte';
  import { _ } from 'svelte-intl';
  import Button from '../../kit/Button.svelte';
  import uploadIcon from '../../kit/icons/upload';
  import paperPlaneIcon from '../../kit/icons/paperPlane';
  import Tooltip from '../../kit/Tooltip.svelte';
  import anchorme from 'anchorme';
  import Dropzone from './ChatDropzone.svelte';

  export let disabled: boolean = false;
  export let autofocus: boolean = false;
  export let clear = () => {
    inputHtml = '';
  };
  let input: HTMLElement | undefined;
  let dropzone: Dropzone | undefined;
  let inputHtml: string = '';

  const dispatch = createEventDispatcher();

  function send() {
    if (inputHtml.length > 0) {
      const html = anchorme({
        input: inputHtml,
        options: {
          attributes: {
            target: '_blank',
          },
          protocol: "https://",
          truncate: 32,
        },
      });

      dispatch('send', { html });
    }
  }

  function onKey(evt: KeyboardEvent) {
    if (!evt.shiftKey && !evt.ctrlKey && evt.key === 'Enter') {
      evt.preventDefault();
      send();
    }
  }

  onMount(() => {
    autofocus && input?.focus();
    input?.addEventListener('keydown', onKey);
    return () => {
      input?.removeEventListener('keydown', onKey);
    };
  });
</script>

<template>
  <Dropzone bind:this={dropzone} {disabled} on:send={(ev) => dispatch('send', ev.detail)} />

  <div class="chat-input-holder">
    <div class="chat-left-control">
      <Button on:click={() => dropzone?.peekFile()} componentClass="chat-control-button" {disabled} icon={uploadIcon} />
    </div>
    <div
      spellcheck={false}
      class="chat-input"
      class:disabled
      bind:this={input}
      contenteditable="true"
      bind:innerHTML={inputHtml}
      tabindex="0"
    />
    <div class="chat-right-control">
      <Tooltip>
        <Button
          on:click={send}
          componentClass="chat-control-button {inputHtml.length ? 'active' : ''}"
          icon={paperPlaneIcon}
        />
        <div slot="tooltip">{$_('ChatInput.tooltip_send')}</div>
      </Tooltip>
    </div>
  </div>
</template>
