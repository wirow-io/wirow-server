import * as Sentry from "@sentry/browser";
import { Integrations } from "@sentry/tracing";

Sentry.init({
  dsn: 'https://a@b/1', // DSN will be replaced in backend while tunneling
  integrations: [new Integrations.BrowserTracing()],
  attachStacktrace: true,
  autoSessionTracking: false,
  tunnel: '/sentry/envelope',
});

export default Sentry;