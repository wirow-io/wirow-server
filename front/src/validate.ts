///
/// Svelte components validation staff
///

import {
  createEventDispatcher,
  getContext,
  get_current_component,
  onMount,
  setContext,
  SvelteComponent
} from 'svelte/internal';
import type { Readable, Writable } from 'svelte/store';
import { writable } from 'svelte/store';
import type { SEvent } from './interfaces';

const ValidationContextKey = {};

type ValidatorFnReturn = ValidatorFn | ValidatorFn[] | ValueValidator | ValueValidator[] | undefined | void;

export type ValidatorFn = (errors: string[]) => ValidatorFnReturn;

/**
 * Value validator function.
 */
export interface ValueValidator {
  /**
   * Validates a given value returns not empty string in the case of validation error.
   */
  (errors: string[], value?: string): string | undefined;
}

/**
 * Validation error of underlying component.
 */
export interface ValidationResult {
  errors: string[];
  component: SvelteComponent;
}

export interface ValidationContext {
  validate: (component?: any) => ValidationResult[];
  results: Map<SvelteComponent, ValidationResult>;
  valid: Readable<boolean>;
  component: SvelteComponent;
}

interface ValidationContextInternal extends ValidationContext {
  calledValidators?: Set<ValidatorFnReturn>;
}

interface ValidationEventDetail extends Pick<ValidationContext, 'results'> {
  component?: SvelteComponent;
}

/**
 * Creates new form validation context.
 */
export function createValidationContext(...requiredValidators: (ValidatorFn | ValueValidator)[]): ValidationContext {
  const dispatcher = createEventDispatcher();
  const ctx = <ValidationContextInternal>{
    calledValidators: requiredValidators.length ? new Set<ValidatorFnReturn>() : undefined,
    results: new Map<SvelteComponent, ValidationResult>(),
    valid: writable<boolean>(false),
    component: get_current_component(),
    validate: (component?: any) => {
      dispatcher('validate', { results: ctx.results, component });
      const results: ValidationResult[] = [];
      for (const value of ctx.results.values()) {
        results.push(value);
      }
      (ctx.valid as Writable<boolean>).set(
        results.length === 0 && !requiredValidators.some((v) => ctx.calledValidators && !ctx.calledValidators.has(v))
      );
      return results;
    },
  };
  setContext(ValidationContextKey, ctx);
  return ctx;
}

export function getValidate(skip = false): () => string[] {
  if (skip) {
    return () => [];
  }
  const cc = get_current_component();
  const ctx = getContext<ValidationContext>(ValidationContextKey);
  if (!ctx) {
    console.debug(
      "validate() will not take effect since parent validation context does't estabilished by createValidationContext()"
    );
    return () => [];
  }
  return () => {
    const res = ctx.validate(cc).find((r) => r.component === cc);
    return res ? res.errors : [];
  };
}

/**
 * On validate trigger callback.
 */
export function onValidate(fn: ValidatorFn): void {
  const cc = get_current_component();
  const ctx = getContext<ValidationContextInternal>(ValidationContextKey);
  if (!ctx) {
    console.debug(
      "onValidate() will not take effect since parent validation context does't estabilished by createValidationContext()"
    );
    return;
  }
  const dispose = ctx.component.$on('validate', (evt: SEvent) => {
    const { results, component } = evt.detail as ValidationEventDetail;
    if (component != null && component !== cc) {
      return;
    }
    const errors: string[] = [];
    if (fn) {
      const cv = fn(errors);
      if (ctx.calledValidators) {
        if (typeof cv === 'function') {
          ctx.calledValidators.add(cv);
        } else if (Array.isArray(cv)) {
          cv.forEach((v) => ctx.calledValidators!.add(v));
        }
      }
    }
    if (errors.length > 0) {
      results.set(cc, { component: cc, errors });
    } else {
      results.delete(cc);
    }
  });

  onMount(() => dispose);
}
