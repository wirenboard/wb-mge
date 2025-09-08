import { createApp, type Directive } from 'vue';
import App from './App.vue';
import router from './router';
import { i18n } from './i18n';
import './style.css';
import { createHead } from '@unhead/vue/client';

const app = createApp(App);

const vFocus: Directive<HTMLElement> = {
  mounted: (el) => {
    const input = el.querySelector<HTMLInputElement>('input');
    if (input) {
      input.focus();
    } else {
      el.focus();
    }
  },
};

const head = createHead();

app.use(i18n);
app.use(head);
app.use(router);
app.directive('focus', vFocus);

app.mount('#app');
