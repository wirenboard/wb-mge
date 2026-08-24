import { createI18n } from 'vue-i18n';
import { messages } from './messages';
import { DEFAULT_LANGUAGE, preferredTags, resolveLocale, type Locale } from './resolveLocale';
import { slavicPluralization } from './slavicPluralization';

export type { Locale } from './resolveLocale';

export const languages: Record<Locale, string> = {
  en: 'English',
  ru: 'Русский',
  kk: 'Қазақша',
  it: 'Italiano',
  de: 'Deutsch',
};

// Reading localStorage throws, rather than returning null, where a site is denied
// storage. This runs at module scope, so an unguarded throw would abort the import of
// @/i18n and take the whole UI down — degrade to "no stored choice" instead.
const storedLanguage = (): string | null => {
  try {
    return localStorage.getItem('lang');
  } catch {
    return null;
  }
};

const locale = resolveLocale(storedLanguage(), preferredTags(navigator.languages, navigator.language));
document.documentElement.lang = locale;

export const i18n = createI18n({
  legacy: false,
  locale,
  fallbackLocale: DEFAULT_LANGUAGE,
  fallbackWarn: false,
  missingWarn: false,
  pluralRules: {
    ru: slavicPluralization,
  },
  messages
});

export const changeLang = (lang: Locale) => {
  i18n.global.locale.value = lang;
  try {
    localStorage.setItem('lang', lang);
  } catch {
    // Storage is denied: the switch still applies for this page load, it just will not
    // survive a reload.
  }
  document.documentElement.lang = lang;
};
