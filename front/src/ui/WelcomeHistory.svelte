<script lang="ts">
  import { _ } from 'svelte-intl';
  import ConfirmDialog from '../ConfirmDialog.svelte';
  import type { RoomHistoryEntry } from '../interfaces';
  import Input from '../kit/Input.svelte';
  import type { CloseModalFn } from '../kit/Modal.svelte';
  import { openModal, recommendedModalOptions } from '../kit/Modal.svelte';
  import { Logger } from '../logger';
  import { recommendedTimeout, sendNotification } from '../notify';
  import { canCreateMeeting, canCreateWebinar } from '../permissions';
  import { route } from '../router';
  import { t } from '../translate';
  import { user } from '../user';
  import { catchLogResolve } from '../utils';
  import { sendAwait } from '../ws';
  import RoomInfoDialog from './info/RoomInfoDialog.svelte';
  import HistoryItem from './WelcomeHistoryItem.svelte';
  import searchIcon from '../kit/icons/search';
  import { __values } from 'tslib';

  const log = new Logger('History');

  let roomsHistory: RoomHistoryEntry[] = [];
  let historyFilter = '';

  function onRoomRemove(entry: RoomHistoryEntry) {
    const { uuid, name } = entry;
    openModal(ConfirmDialog, recommendedModalOptions, (close: CloseModalFn) => ({
      caption: t('History.remove_caption'),
      content: t('History.remove_content', { name }),
      onYes: async () => {
        await sendAwait({
          cmd: 'history_rooms_remove',
          room: uuid,
        });
        roomsHistory = roomsHistory.filter((h) => h.uuid !== uuid);
        close();
      },
    }));
  }

  async function onRoomAction(entry: RoomHistoryEntry) {
    const { uuid } = entry;
    if ($canCreateMeeting || $canCreateWebinar) {
      route(uuid);
      return;
    }
    const resp = await fetch(`status?ref=${encodeURIComponent(uuid)}`).catch(catchLogResolve(log));
    if (resp?.status === 200) {
      route(uuid);
      return;
    }
    sendNotification({
      text: $_('Welcome.cannot_join_room_is_not_active'),
      style: 'warning',
      timeout: recommendedTimeout,
    });
  }

  function onRoomDetails(entry: RoomHistoryEntry) {
    openModal(RoomInfoDialog, { ...recommendedModalOptions, closeOnOutsideClick: false }, () => ({
      entry,
    }));
  }

  function applyHistoryFilter(entries: RoomHistoryEntry[]): RoomHistoryEntry[] {
    const maxEntries = 32;
    let pt = historyFilter ? historyFilter.trim() : '';
    if (pt === '') {
      return entries.slice(0, maxEntries);
    }
    pt = pt.toLowerCase();
    entries = entries.filter((e) => {
      return e.name.toLowerCase().includes(pt);
    });
    return entries;
  }

  function updateHistory(..._: any[]) {
    if ($user) {
      roomsHistory = applyHistoryFilter($user.rooms);
    } else {
      roomsHistory = [];
    }
  }

  $: updateHistory($user, historyFilter);
</script>

<template>
  {#if roomsHistory.length > 0 || historyFilter !== ''}
    <div class="welcome-history">
      <div class="welcome-history-header">
        <h2>{$_('History.caption')}</h2>
        <div class="spacer" />
        <Input
          id="welcome-input"
          placeholder={$_('History.search_placeholder')}
          size="small"
          flavor="success"
          componentClass="welcome-input"
          icon={searchIcon}
          bind:value={historyFilter}
          onLeft
        />
      </div>
      {#each roomsHistory as item (item.uuid)}
        <HistoryItem {item} {onRoomAction} {onRoomRemove} {onRoomDetails} />
      {/each}
    </div>
  {/if}
</template>
