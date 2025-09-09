import { createApp } from 'vue';
import App from './App.vue';
import router from './router';
import { i18n } from './i18n';
import './style.css';
import { createHead } from '@unhead/vue/client';

const app = createApp(App);
const head = createHead();

app.use(i18n);
app.use(head);
app.use(router);

app.mount('#app');
