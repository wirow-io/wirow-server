<script context="module" lang="ts">
  import colorLib from '@kurkle/color';

  const borderColors = ['#f09415', '#4baf73', '#5aa6c0', '#d17df9', '#6585cf'];
  const backgroundColors = borderColors.map((c) => {
    return colorLib(c).alpha(0.5).rgbString();
  });
</script>

<script lang="ts">
  import ChartComp from '../../kit/Chart.svelte';
  import { Chart } from 'chart.js';
  import type { ScatterDataPoint } from 'chart.js';
  import Button from '../../kit/Button.svelte';
  import { _ } from 'svelte-intl';
  import { t } from '../../translate';
  import { subscribe as wsSubscribe } from '../../ws';
  import { onMount } from 'svelte';
  import { dbPromiseOrNull } from '../../db';

  type Period = '12h' | '24h' | 'week' | 'month';
  export let period: Period = '12h';

  let chart: Chart | undefined;

  async function update(..._: any[]) {
    const datasets = await gaugesFetchDataset(period);
    if (chart) {
      chart.data.datasets = datasets;
      (chart.options.scales!.x as any).time.minUnit = period === '24h' || period === '12h' ? 'minute' : 'day';
      chart.options.plugins!.title!.text = gaugesChartTitle();
      chart.update();
    }
  }

  async function gaugesFetchDataset(period: Period) {
    const resp = await fetch(`admin/gauges/${period}`);
    const data = await resp.json();

    if (!Array.isArray(data)) {
      throw new Error('Invalid response');
    }

    const dsets = new Map<number, any>([
      [0x01, { id: 0x01, label: t('Dashboard.chart.label.rooms'), order: 1, stepped: true, data: [] }],
      [0x02, { id: 0x02, label: t('Dashboard.chart.label.users'), order: 3, stepped: true, data: [] }],
      [0x04, { id: 0x04, label: t('Dashboard.chart.label.workers'), order: 2, hidden: true, stepped: true, data: [] }],
      [0x08, { id: 0x08, label: t('Dashboard.chart.label.streams'), order: 4, hidden: true, stepped: true, data: [] }],
    ]);

    for (let i = data.length - 1; i >= 0; --i) {
      const n = data[i];
      if (!Array.isArray(n) || n.length !== 3) {
        continue;
      }
      // [[1620294810,4,0],[1620294667,1,0],[1620294607,8,0],...]
      const ds = dsets.get(n[1]);
      if (!ds) {
        continue;
      }
      ds.data.push({
        x: n[0] * 1000,
        y: n[2],
      });
    }
    const ret = [...dsets.values()];
    await datasetsInit(ret);
    return ret;
  }

  function datasetData(id: number): ScatterDataPoint[] {
    if (chart) {
      return (chart.data.datasets.find((d: any) => d.id === id)?.data || []) as ScatterDataPoint[];
    } else {
      return [];
    }
  }

  function lastDatasetValue(id: number): number {
    if (chart) {
      let data = datasetData(id);
      return data.length > 0 ? data[data.length - 1].y : 0;
    } else {
      return 0;
    }
  }

  function gaugesChartTitle(): string {
    if (chart == null) {
      return '';
    }
    const rooms = lastDatasetValue(0x01);
    const users = lastDatasetValue(0x02);
    if (users > 0) {
      return t('Dashboard.chart.title', { rooms, users });
    } else {
      return t('Dashboard.chart.title.noactive');
    }
  }

  async function datasetsInit(dsets: any[]) {
    for (let i = 0; i < dsets.length; ++i) {
      dsets[i].borderColor = borderColors[i % borderColors.length];
      dsets[i].backgroundColor = backgroundColors[i % backgroundColors.length];
      dsets[i].fill = true;
    }
    const db = await dbPromiseOrNull;
    if (db === null) {
      return;
    }
    const tasks = [] as Promise<any>[];
    for (let i = 0; i < dsets.length; ++i) {
      tasks.push(db.get(`admin.Dashboard.dataset.${i}`).catch(() => null));
    }
    const results = await Promise.all(tasks);
    for (let i = 0; i < results.length; ++i) {
      if (results[i] != null) {
        dsets[i].hidden = results[i] === false;
      }
    }
  }

  async function onToggleDataset(idx: number, show: boolean) {
    const db = await dbPromiseOrNull;
    return db?.put(`admin.Dashboard.dataset.${idx}`, show);
  }

  async function gaugesChartBuilder(canvas: HTMLCanvasElement | undefined): Promise<Chart | undefined> {
    if (!canvas) {
      // Chart destroyed
      chart = undefined;
      return;
    }
    const data = {
      datasets: [],
    };
    chart = new Chart(canvas, {
      type: 'line',
      data,
      options: {
        animation: false,
        responsive: true,
        maintainAspectRatio: false,
        aspectRatio: 1.75,
        elements: {
          line: {
            borderWidth: 1,
          },
          point: {
            radius: 0,
            hoverBorderWidth: 3,
            hitRadius: 4,
          },
        },
        layout: {
          padding: 14,
        },
        scales: {
          x: {
            type: 'time',
            time: {
              minUnit: 'minute',
              displayFormats: {
                hour: 'p',
              },
            },
            ticks: {
              autoSkip: true,
            },
          },
          y: {
            ticks: {
              stepSize: 1,
            },
          },
        },
        plugins: {
          legend: {
            position: 'bottom',
            labels: {
              color: '#ccc',
            },
          },
          title: {
            display: true,
            color: '#ebebeb',
            font: { weight: 'bold', size: 14 },
            text: gaugesChartTitle(),
          },
        },
      },
      plugins: [
        {
          id: '__internal__',
          afterDatasetUpdate(_, args) {
            if (args.mode === 'show' || args.mode === 'hide') {
              onToggleDataset(args.index, args.mode === 'show');
            }
          },
        },
      ],
    });

    return chart;
  }

  function onWsMessage(msg: any): void {
    if (msg.event !== 'GAUGE' || !Array.isArray(msg.data) || msg.data.length !== 3 || chart == null) {
      return;
    }
    const data = datasetData(msg.data[1]);
    const ts = msg.data[0] * 1000;
    if (data.length > 0 && data[data.length - 1].x === ts) {
      data[data.length - 1].y = msg.data[2]; // Update old ds
    } else {
      data.push({
        // Push new
        x: msg.data[0] * 1000,
        y: msg.data[2],
      });
    }
    chart.options.plugins!.title!.text = gaugesChartTitle();
    chart.update();
  }

  onMount(() => {
    const wsUnsubscribe = wsSubscribe(onWsMessage);
    return () => {
      wsUnsubscribe();
    };
  });

  $: update(period);
</script>

<template>
  <div class="admin-dashboard">
    <div class="controls">
      <Button
        on:click={() => (period = '12h')}
        clickable={period !== '12h'}
        componentClass={period === '12h' ? 'outline' : 'outline-noborder'}>{$_('Dashboard.12h')}</Button
      >
      <Button
        on:click={() => (period = '24h')}
        clickable={period !== '24h'}
        componentClass={period === '24h' ? 'outline' : 'outline-noborder'}>{$_('Dashboard.24h')}</Button
      >
      <Button
        on:click={() => (period = 'week')}
        clickable={period !== 'week'}
        componentClass={period === 'week' ? 'outline' : 'outline-noborder'}>{$_('Dashboard.week')}</Button
      >
      <Button
        on:click={() => (period = 'month')}
        clickable={period !== 'month'}
        componentClass={period === 'month' ? 'outline' : 'outline-noborder'}>{$_('Dashboard.month')}</Button
      >
    </div>
    <ChartComp chartBuilder={gaugesChartBuilder} />
  </div>
</template>
