/**
 * Locale resolution, kept free of DOM access so it can be unit tested.
 *
 * The UI ships messages per primary language subtag ('ru', 'de', ...), while both the
 * browser and a stored preference hand us full BCP 47 tags with a region ('ru-RU',
 * 'de-AT'). Matching those tags verbatim against the supported list is what made a
 * Russian browser open the UI in English (SOFT-7355), so every tag goes through
 * normalisation first.
 */

export type Locale = 'en' | 'ru' | 'kk' | 'it' | 'de';

export const SUPPORTED_LANGUAGES: readonly Locale[] = ['en', 'ru', 'kk', 'it', 'de'];

export const DEFAULT_LANGUAGE: Locale = 'en';

const isSupported = (tag: string): tag is Locale => (SUPPORTED_LANGUAGES as readonly string[]).includes(tag);

/**
 * Reduce one language tag to a supported locale, or null when we ship nothing for it.
 * Falls back to the primary subtag — everything before the first '-' or '_' — so both
 * 'ru-RU' and a hand-written 'ru_RU' resolve to 'ru'.
 */
const normalizeTag = (tag: string | null | undefined): Locale | null => {
  const normalized = (tag ?? '').trim().toLowerCase();
  if (!normalized) {
    return null;
  }
  if (isSupported(normalized)) {
    return normalized;
  }
  const primary = normalized.split(/[-_]/)[0];
  return isSupported(primary) ? primary : null;
};

/**
 * Order the browser's language tags by preference. `navigator.languages` is the full
 * ordered list; `navigator.language` is only its top entry and the sole thing older
 * engines expose, so it stands in when the list is missing. Anything unusable degrades
 * to an empty list rather than throwing — this runs while the module is still loading.
 */
export const preferredTags = (languages: readonly string[] | undefined, language: string | undefined): string[] => {
  if (Array.isArray(languages) && languages.length > 0) {
    return [...languages];
  }
  return language ? [language] : [];
};

/**
 * Pick the locale to start the UI in.
 *
 * `stored` is the explicit choice made through the language switcher and wins over the
 * browser — but only when we actually ship it, so a stale or hand-edited value cannot
 * pin the UI to a locale with no messages. Otherwise the browser preference list is
 * walked in order, and the first supported entry wins.
 */
export const resolveLocale = (stored: string | null | undefined, preferred: readonly string[]): Locale => {
  const explicit = normalizeTag(stored);
  if (explicit) {
    return explicit;
  }

  for (const tag of preferred) {
    const detected = normalizeTag(tag);
    if (detected) {
      return detected;
    }
  }

  return DEFAULT_LANGUAGE;
};
