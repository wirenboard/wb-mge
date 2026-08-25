// Locale resolution, kept free of DOM access so it can be unit tested.

export type Locale = 'en' | 'ru' | 'kk' | 'it' | 'de';

export const SUPPORTED_LANGUAGES: readonly Locale[] = ['en', 'ru', 'kk', 'it', 'de'];

export const DEFAULT_LANGUAGE: Locale = 'en';

const isSupported = (tag: string): tag is Locale => (SUPPORTED_LANGUAGES as readonly string[]).includes(tag);

// Browsers report region-qualified tags ('ru-RU', 'de-AT'), while messages ship per
// primary subtag — hence the fallback. Both lookups compare whole codes, never prefixes.
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

export const preferredTags = (languages: readonly string[] | undefined, language: string | undefined): string[] => {
  if (Array.isArray(languages) && languages.length > 0) {
    return [...languages];
  }
  return language ? [language] : [];
};

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
