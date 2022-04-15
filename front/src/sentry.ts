// By default import this mock file without module imports
// If need to use sentry build with env SENTRY_FRONT_DSN and instead of ./sentry ./sentry.internal will be imported

import type Sentry from './sentry.internal';
export default undefined as (undefined | typeof Sentry);