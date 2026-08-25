import { describe, expect, it } from 'vitest';
import { preferredTags, resolveLocale } from '@/i18n/resolveLocale';

describe('resolveLocale', () => {
  it('resolves a region-qualified ru-RU to ru (the SOFT-7355 bug)', () => {
    expect(resolveLocale(null, ['ru-RU'])).toBe('ru');
  });

  it('still resolves a bare ru to ru', () => {
    expect(resolveLocale(null, ['ru'])).toBe('ru');
  });

  it('matches tags case-insensitively', () => {
    expect(resolveLocale(null, ['RU-ru'])).toBe('ru');
    expect(resolveLocale(null, ['DE'])).toBe('de');
  });

  it('resolves the other region-qualified languages we ship', () => {
    expect(resolveLocale(null, ['de-AT'])).toBe('de');
    expect(resolveLocale(null, ['it-CH'])).toBe('it');
    expect(resolveLocale(null, ['kk-KZ'])).toBe('kk');
  });

  it('accepts an underscore as the subtag separator', () => {
    expect(resolveLocale(null, ['ru_RU'])).toBe('ru');
  });

  it('falls back to the default for a multi-subtag tag we do not ship', () => {
    expect(resolveLocale(null, ['zh-Hant-TW'])).toBe('en');
  });

  it('falls back to the default for an unsupported language', () => {
    expect(resolveLocale(null, ['fr-FR'])).toBe('en');
  });

  it('honours the browser preference order rather than the supported-list order', () => {
    expect(resolveLocale(null, ['fr-FR', 'ru-RU', 'en-US'])).toBe('ru');
  });

  it('lets an explicit stored choice win over the browser preference', () => {
    expect(resolveLocale('en', ['ru-RU'])).toBe('en');
  });

  it('normalises a region-qualified stored value written by an older build', () => {
    expect(resolveLocale('ru-RU', [])).toBe('ru');
  });

  it('ignores an unsupported stored value and keeps detecting from the browser', () => {
    expect(resolveLocale('fr', ['ru-RU'])).toBe('ru');
  });

  it('returns the default when nothing is stored and nothing is preferred', () => {
    expect(resolveLocale(null, [])).toBe('en');
    expect(resolveLocale(undefined, [])).toBe('en');
  });

  it('skips blank entries instead of letting them match', () => {
    expect(resolveLocale('', ['   ', 'ru-RU'])).toBe('ru');
    expect(resolveLocale(null, ['', '  '])).toBe('en');
  });

  it('trims surrounding whitespace off a tag before matching it', () => {
    expect(resolveLocale(' ru ', [])).toBe('ru');
    expect(resolveLocale(null, ['\tru-RU '])).toBe('ru');
  });

  it('rejects a tag that is only a subtag separator', () => {
    expect(resolveLocale(null, ['-', '_', 'ru'])).toBe('ru');
    expect(resolveLocale('-', ['ru-RU'])).toBe('ru');
  });

  it('matches a language code exactly rather than by prefix', () => {
    // Guards against rewriting normalizeTag()'s lookups as a startsWith() scan: that
    // keeps every other test green, but hands Russian to a 'rue' speaker and lets a
    // stored 'english' outrank the browser.
    expect(resolveLocale(null, ['rue'])).toBe('en');
    expect(resolveLocale(null, ['ita-IT'])).toBe('en');
    expect(resolveLocale('english', ['ru-RU'])).toBe('ru');
  });
});

describe('preferredTags', () => {
  it('returns the browser preference list in order and unchanged', () => {
    expect(preferredTags(['ru-RU', 'en-US'], 'ru-RU')).toEqual(['ru-RU', 'en-US']);
  });

  it('returns a copy rather than the live navigator.languages array', () => {
    const languages = ['ru-RU', 'en-US'];
    const tags = preferredTags(languages, 'ru-RU');
    tags.push('de-AT');
    expect(languages).toEqual(['ru-RU', 'en-US']);
  });

  it('falls back to the single language when the list is empty', () => {
    expect(preferredTags([], 'ru-RU')).toEqual(['ru-RU']);
  });

  it('falls back to the single language when the list is absent', () => {
    expect(preferredTags(undefined, 'de-AT')).toEqual(['de-AT']);
  });

  it('falls back instead of throwing when the list is not an array', () => {
    expect(preferredTags('ru-RU' as unknown as readonly string[], 'ru-RU')).toEqual(['ru-RU']);
  });

  it('yields an empty list when the browser exposes nothing usable', () => {
    expect(preferredTags(undefined, undefined)).toEqual([]);
    expect(preferredTags(undefined, '')).toEqual([]);
  });

  it('feeds resolveLocale so a Russian browser opens the UI in Russian (SOFT-7355)', () => {
    expect(resolveLocale(null, preferredTags(['ru-RU', 'en-US'], 'ru-RU'))).toBe('ru');
  });
});
