<script lang="ts">
  import { _ } from 'svelte-intl';
  import type { RoomActivityEntry } from '.';
  import Table from '../../kit/table/Table.svelte';
  import Tbody from '../../kit/table/Tbody.svelte';
  import Thead from '../../kit/table/Thead.svelte';
  import { t } from '../../translate';

  export let activity: RoomActivityEntry[];
  export let pageSize = 15; // Events per page

  let pageActivity: RoomActivityEntry[] = [];
  let currentPage = 1;
  let pages: number[] = [];

  function setPage(num: number) {
    currentPage = num;
    const off = pageSize * (num - 1);
    pageActivity = activity.slice(off, off + pageSize);
  }

  function fillPages(): number[] {
    const res: number[] = [];
    const npages = Math.ceil(activity.length / pageSize);
    for (let i = 0; i < npages; ++i) {
      res.push(i + 1);
    }
    return res;
  }

  function init(..._: any[]) {
    pages = fillPages();
    setPage(1);
  }

  $: init(activity, pageSize);
</script>

<template>
  {#if pages.length > 1}
    <div class="pager lead">
      {#if currentPage != pages.length}
        <a href={'#'} on:click|preventDefault={() => setPage(currentPage + 1)} class="knob">&#62;&#62;</a>
      {/if}
      {#each pages as p}
        <a href={'#'} on:click|preventDefault={() => setPage(p)} class:selected={p === currentPage}>{p} </a>
      {/each}
    </div>
  {/if}
  <Table componentStyle="width:100%">
    <Thead>
      <tr>
        <th>{$_('RoomActivity.time')}</th>
        <th>{$_('RoomActivity.details')}</th>
      </tr>
    </Thead>
    <Tbody>
      {#each pageActivity as a}
        <tr>
          <td>{$_('time.short', { time: new Date(a.ts) })}</td>
          <td>
            {#if a.event === 'joined'}
              {$_('RoomActivity.joined', { member: a.member })}
            {:else if a.event === 'left'}
              {$_('RoomActivity.left', { member: a.member })}
            {:else if a.event === 'renamed'}
              {$_('RoomActivity.renamed', { old: a.old_name, new: a.new_name })}
            {:else if a.event === 'whiteboard'}
              <a href={a.link} target="_blank">{$_('RoomActivity.whiteboard', { member: a.member })}</a>
            {:else}
              {t(`RoomActivity.${a.event}`) || 'unknown'}
            {/if}
          </td>
        </tr>
      {/each}
    </Tbody>
  </Table>
  {#if pages.length > 1}
    <div class="pager">
      {#if currentPage != pages.length}
        <a href={'#'} on:click|preventDefault={() => setPage(currentPage + 1)} class="knob">&#62;&#62;</a>
      {/if}
      {#each pages as p}
        <a href={'#'} on:click|preventDefault={() => setPage(p)} class:selected={p === currentPage}>{p} </a>
      {/each}
    </div>
  {/if}
</template>
