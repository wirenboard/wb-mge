import { createI18n } from 'vue-i18n';
import { messages } from './messages';
import { slavicPluralization } from './slavicPluralization';

export type Locale = 'en' | 'ru';

const SUPPORTED_LANGUAGES: Locale[] = ['en', 'ru'];

const DEFAULT_LANGUAGE = 'en';

const locale = localStorage.getItem('lang') ?? ( SUPPORTED_LANGUAGES.includes(navigator.language as Locale) ? navigator.language : DEFAULT_LANGUAGE);

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
  localStorage.setItem('lang', lang);
};
