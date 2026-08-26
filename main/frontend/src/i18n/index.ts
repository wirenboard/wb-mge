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

// Where a site is denied storage the localStorage access itself throws, and this is called
// while the module loads — an unguarded throw would take the whole UI down with it.
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
    // Storage denied: the switch applies now, it just will not survive a reload.
  }
  document.documentElement.lang = lang;
};
