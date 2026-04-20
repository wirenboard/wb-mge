<script setup lang="ts">
import { reactive, ref } from 'vue';
import { useI18n } from 'vue-i18n';
import { useRoute, useRouter } from 'vue-router';
import Logo from '@/assets/logo.svg?component';
import { useAlerts } from '@/common/alert';
import { changeLang, languages, Locale } from '@/i18n';
import { documentation } from '@/common/links';
import type { Auth } from '@/common/types';
import AlertsWrapper from '@/components/AlertsWrapper.vue';
import { api } from '@/utils/api';
import { useHostname } from '@/common/hostname';

const { t, locale } = useI18n();
const router = useRouter();
const route = useRoute();
const { showAlert } = useAlerts();
const data = reactive({ login: '', pass: '' });
const isLoading = ref(false);
const { hostname } = useHostname();

const login = async () => {
  isLoading.value = true;
  try {
    const { auth } = await api<Auth>('auth', { method: 'POST', json: data });
    if (auth) {
      await router.push(route.query.redirect ? `/${route.query.redirect}` : '/');
    } else {
      showAlert(t('wrong_credentials'));
    }
  } finally {
    isLoading.value = false;
  }
};
</script>

<template>
  <section class="login">
    <Logo alt="Wiren Board" />
    <div v-if="hostname" class="login-hostname">{{ hostname }}</div>

    <form class="card login-card" @submit.prevent="login">
      <div class="card-header">
        <div class="title">{{ t('title') }}</div>
      </div>
      <div class="card-body">
        <div class="login-field">
          <label for="username">{{ t('login') }}</label>
          <input id="username" v-model="data.login" name="username" type="text" autocomplete="username" :required="!!data.login" autofocus />
        </div>
        <div class="login-field">
          <label for="password">{{ t('password') }}</label>
          <input id="password" v-model="data.pass" name="pass" type="password" autocomplete="current-password" :required="!!data.pass" />
        </div>
      </div>
      <div class="login-actions">
        <button type="submit" :disabled="isLoading || !data.login || !data.pass">{{ t('sign_in') }}</button>
      </div>
    </form>

    <nav class="login-links">
      <a :href="documentation" target="_blank">{{ t('documentation') }}</a>

      <label class="login-languageWrapper">
        <span class="login-languageIcon" />
        <select v-model="locale" class="login-language" @change="changeLang(locale as Locale)">
          <option v-for="(lang, code) in languages" :key="code" :value="code">{{ lang }}</option>
        </select>
      </label>
    </nav>
  </section>
  <AlertsWrapper />
</template>

<style scoped>
.login {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  height: 100dvh;
  margin-top: -30px;
  padding: 0 24px;
  background: var(--bg-sidebar);

  @media (max-height: 600px) {
    margin-top: 0;
  }
}

.login-card {
  margin-top: 24px;
  max-width: 350px;
  width: 100%;
  overflow: hidden;
}

.login-field {
  display: flex;
  flex-direction: column;
  gap: 6px;
  padding: 9px 0;
  border-bottom: 1px dashed var(--border-color);
}

.login-field:last-child {
  border-bottom: 0;
}

.login-field > label {
  color: var(--text-secondary);
  font-size: 13px;
}

.login-actions {
  padding: 14px 18px;
  display: flex;
  align-items: center;
  justify-content: end;
  background: var(--bg-surface-subtle);
  border-top: 1px solid var(--border-color);
}

.login-links {
  display: flex;
  gap: 24px;
  align-items: center;
  justify-content: space-between;
  width: 100%;
  max-width: 350px;
  box-sizing: border-box;
  padding: 0 12px 0 18px;
  margin-top: 8px;
}

.login-links a {
  font-size: 13px;
  color: var(--text-on-dark-muted);
}

.login-links a:hover {
  color: var(--text-on-dark);
}

.login-languageWrapper {
  display: flex;
  align-items: center;
}

.login-language {
  border: 0;
  box-shadow: none;
  font-size: 13px;
  background: none;
  color: var(--text-on-dark-muted);
  cursor: pointer;
  padding-right: 12px;
  height: auto;
}

.login-languageIcon {
  background-image: url("@/assets/locale.svg");
  background-repeat: no-repeat;
  background-position: center;
  background-size: 14px;
  height: 14px;
  width: 14px;
  filter: invert(0.6);
}

.login-hostname {
  font-size: 12px;
  color: var(--text-on-dark-muted);
  margin-top: 6px;
}
</style>

<i18n>
{
  "en": {
    "title": "WB-MGE gateway interface",
    "sign_in": "Sign in",
    "documentation": "Documentation",
    "wrong_credentials": "Please enter correct login and password"
  },
  "ru": {
    "title": "Интерфейс шлюза WB-MGE",
    "sign_in": "Войти",
    "documentation": "Документация",
    "wrong_credentials": "Введены неверные логин или пароль"
  },
  "kk": {
    "title": "WB-MGE шлюзының интерфейсі",
    "sign_in": "Кіру",
    "documentation": "Құжаттама",
    "wrong_credentials": "Логин немесе құпиясөз қате енгізілді"
  },
  "it": {
    "title": "Interfaccia gateway WB-MGE",
    "sign_in": "Accedi",
    "documentation": "Documentazione",
    "wrong_credentials": "Inserisci login e password corretti"
  },
  "de": {
    "title": "WB-MGE-Gateway-Oberfläche",
    "sign_in": "Einloggen",
    "documentation": "Dokumentation",
    "wrong_credentials": "Bitte korrekten Benutzernamen und Passwort eingeben"
  }
}
</i18n>
