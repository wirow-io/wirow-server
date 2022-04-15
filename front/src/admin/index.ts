import 'focus-visible';
import '../translate';
import Root from './Admin.svelte';
import './translations';

import {
  Chart,
  LineController,
  LineElement,
  PointElement,
  Title,
  Legend,
  Tooltip,
  LinearScale,
  TimeScale,
  Filler,
} from 'chart.js';

Chart.register(LineController, LineElement, PointElement, Title, Legend, Tooltip, LinearScale, TimeScale, Filler);

import 'chartjs-adapter-date-fns';

const root = new Root({
  target: document.body,
});

export default root;
