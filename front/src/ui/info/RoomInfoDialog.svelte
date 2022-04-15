<script lang="ts">
  import { onMount } from 'svelte';
  import { _ } from 'svelte-intl';
  import { t } from '../../translate';
  import type { RoomInfo } from '.';
  import type { RoomHistoryEntry } from '../../interfaces';
  import Box from '../../kit/Box.svelte';
  import Button from '../../kit/Button.svelte';
  import Loader from '../../kit/Loader.svelte';
  import { closeModal, focusModal } from '../../kit/Modal.svelte';
  import { sendNotification } from '../../kit/Notifications.svelte';
  import { Logger } from '../../logger';
  import RoomActivity from './RoomActivity.svelte';
  import RoomActivityChat from './RoomActivityChat.svelte';
  import RoomActivityRecording from './RoomActivityRecording.svelte';
  import { subscribe as wsSubscribe } from '../../ws';
  import type { Option } from '../../kit/Select.svelte';
  import Select from '../../kit/Select.svelte';

  const log = new Logger('RoomInfo');
  type Side = keyof RoomInfo;

  /// Component properties
  export let entry: RoomHistoryEntry;

  let side: Side | '' = '';
  let info: RoomInfo | undefined;
  let infoPending = true;
  let wsUnsubscribe: (() => void) | undefined = undefined;

  let ref = '';
  let history: Option[] = [];

  function fail(text?: string) {
    text = text ?? $_('RoomInfoDialog.fail');
    sendNotification({
      text,
      style: 'error',
      closeable: true,
    });
    closeModal();
  }

  async function fetchRoomInfoImpl(): Promise<RoomInfo> {
    const resp = await fetch(`room/info?ref=${encodeURIComponent(ref)}`);
    if (resp.status !== 200) {
      return Promise.reject('Unexpected HTTP response code: ');
    }
    const data = await resp.json();
    if (typeof data !== 'object' || !('activity' in data)) {
      return Promise.reject('Unexpected response');
    }
    return data as RoomInfo;
  }

  async function fetchRoomInfo(..._: any[]) {
    infoPending = true;
    info = await fetchRoomInfoImpl()
      .catch((err) => {
        log.error(err);
        fail();
        return undefined;
      })
      .finally(() => {
        infoPending = false;
      });
    if (info == null) {
      history = [];
      return;
    }
    history = info.history.map((h) => ({
      value: h[0],
      label: `${t('time.medium', { time: new Date(h[1]) })} ${t('date.long', { date: new Date(h[1]) })}`,
    }));
    side = 'activity';
  }

  function init(..._: any[]) {
    ref = entry.cid || entry.uuid;
  }

  function isDisabled(s: Side, info: RoomInfo | undefined) {
    if (
      info == null ||
      info[s] == null ||
      (Array.isArray(info[s]) && (<any>info[s]).length === 0) ||
      (s === 'recording' && info[s].status == null)
    ) {
      return true;
    } else {
      return false;
    }
  }

  function openSide(v: Side) {
    side = v;
  }

  function onWsMessage(data: any) {
    if (info && data.event === 'WRC_EVT_ROOM_RECORDING_PP' && data.cid === ref) {
      info.recording.status = 'success';
      info = info;
    }
  }

  onMount(() => {
    focusModal();
    wsUnsubscribe = wsSubscribe(onWsMessage);
    return () => {
      wsUnsubscribe?.();
    };
  });

  $: init(entry);
  $: fetchRoomInfo(ref);
</script>

<template>
  <Box closeable on:close={() => closeModal()} componentClass="modal-medium-width">
    <div class="history">
      {#if history.length}
        <Select bind:value={ref} options={history} size="normal" />
      {/if}
    </div>
    <div>
      <h3>
        {info?.name ?? entry.name}
      </h3>
      {#if infoPending}
        <Loader />
      {:else}
        <Button
          componentClass={side == 'activity' ? 'outline' : 'outline-noborder'}
          clickable={side !== 'activity'}
          on:click={() => openSide('activity')}
          disabled={isDisabled('activity', info)}>{$_('RoomInfoDialog.activity')}</Button
        >
        <Button
          componentClass={side === 'chat' ? 'outline' : 'outline-noborder'}
          clickable={side !== 'chat'}
          on:click={() => openSide('chat')}
          disabled={isDisabled('chat', info)}>{$_('RoomInfoDialog.chat')}</Button
        >
        <Button
          componentClass={side === 'recording' ? 'outline' : 'outline-noborder'}
          clickable={side !== 'recording'}
          on:click={() => openSide('recording')}
          disabled={isDisabled('recording', info)}>{$_('RoomInfoDialog.recording')}</Button
        >
        {#if info}
          {#if side === 'activity'}
            <RoomActivity activity={info.activity} />
          {:else if side === 'chat'}
            <RoomActivityChat chat={info.chat} />
          {:else if side === 'recording'}
            <RoomActivityRecording recording={info.recording} />
          {/if}
        {/if}
      {/if}
    </div>
    <div class="buttons">
      <Button on:click={() => closeModal()} componentClass="full-width">{$_('button.close')}</Button>
    </div>
  </Box>
</template>

<style lang="scss">
  .history {
    margin-top: 2em;
  }
</style>
