import { createApp } from 'vue';
import { createI18n } from 'vue-i18n';
import App from './App.vue';
import router from './router';
import './style.css';

export const LOCALE = 'en';

export const i18n = createI18n({
  legacy: false,
  locale: LOCALE,
  fallbackLocale: 'en',
  fallbackWarn: false,
  missingWarn: false,
  messages: {
    en: {
      connection_error: 'Controller connection error',
      dashboard: 'Dashboard',
      traffic: 'Traffic analysis',
      serial: 'Serial',
      bridge: 'Bridge',
      network: 'Network',
      system: 'System',
      login: 'Login',
    }
  }
});

createApp(App)
  .use(i18n)
  .use(router)
  .mount('#app');
