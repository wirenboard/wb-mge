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

const onLangChange = (e: Event) => {
  changeLang((e.target as HTMLSelectElement).value as Locale);
};

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
  <div class="login-viewport">
    <div class="login-left">
      <Logo class="login-logo" alt="Wiren Board" />

      <div class="login-stack">
        <div v-if="hostname" class="login-chip">
          <span class="chip-kind">WB-MGE v3</span>
          <span class="chip-sep"></span>
          <a class="chip-id" :href="`http://${hostname}.local`" target="_blank" rel="noopener noreferrer">{{ hostname }}</a>
        </div>

        <div class="login-hero">
          <h1 v-html="t('heading')"></h1>
          <p>{{ t('description') }}</p>
        </div>

        <form @submit.prevent="login">
          <div class="login-field">
            <label for="username">{{ t('login') }}</label>
            <input id="username" v-model="data.login" name="username" type="text" autocomplete="username" autofocus />
          </div>
          <div class="login-field">
            <label for="password">{{ t('password') }}</label>
            <input id="password" v-model="data.pass" name="pass" type="password" autocomplete="current-password" />
          </div>
          <div class="login-actions">
            <button type="submit" :disabled="isLoading || !data.login || !data.pass">{{ t('sign_in') }}</button>
          </div>
          <div class="login-links">
            <a :href="documentation" target="_blank">{{ t('documentation') }}</a>
            <label class="login-lang-wrapper">
              <select v-model="locale" class="login-lang" @change="onLangChange">
                <option v-for="(lang, code) in languages" :key="code" :value="code">{{ lang }}</option>
              </select>
            </label>
          </div>
        </form>
      </div>

      <div class="login-colophon"><a href="https://wirenboard.com" target="_blank" rel="noopener noreferrer">wirenboard.com</a></div>
    </div>

    <div class="login-right"></div>
    <AlertsWrapper />
  </div>
</template>

<style scoped>
.login-viewport {
  min-height: 100dvh;
  display: grid;
  grid-template-columns: 1fr 1fr;
  background: var(--bg-sidebar);
}

.login-left {
  position: relative;
  padding: 48px 80px;
  display: flex;
  flex-direction: column;
  justify-content: space-between;
  border-right: 1px solid var(--border-sidebar);
  background:
    radial-gradient(circle at 30% 30%, rgba(77, 184, 116, 0.22) 0%, transparent 55%),
    radial-gradient(circle at 70% 80%, rgba(77, 184, 116, 0.10) 0%, transparent 50%),
    var(--bg-sidebar);

  @media (max-width: 900px) {
    border-right: none;
    border-bottom: 1px solid var(--border-sidebar);
    padding: 40px;
  }
}

.login-logo {
  height: 22px;
  width: auto;
  display: block;
  opacity: 0.92;
  align-self: flex-start; /* pin logo to the left edge of the flex column */
}

.login-logo :deep(svg) {
  height: 22px;
  width: auto;
  display: block;
}

.login-stack {
  width: 380px;
  max-width: 100%;
}

.login-chip {
  display: inline-flex;
  align-items: center;
  gap: 10px;
  padding: 5px 12px;
  border: 1px solid var(--border-sidebar);
  border-radius: 999px;
  background: color-mix(in oklch, var(--bg-sidebar) 80%, black);
  font-family: var(--font-mono);
  font-size: 11px;
  letter-spacing: 0.02em;
  margin-bottom: 18px;
}

.chip-kind {
  color: var(--brand-on-dark);
  font-weight: 600;
}

.chip-sep {
  width: 1px;
  height: 12px;
  background: var(--border-sidebar);
}

.chip-id {
  color: var(--text-on-dark-muted);
  text-decoration: none;
}

.chip-id:hover {
  text-decoration: underline;
}

.login-hero {
  margin-bottom: 36px;
}

.login-hero h1 {
  font-size: 26px;
  font-weight: 600;
  letter-spacing: -0.01em;
  line-height: 1.22;
  margin: 0 0 14px;
  color: #fff;
}

.login-hero h1 em {
  font-style: normal;
  color: var(--brand-on-dark);
}

.login-hero p {
  font-size: 13px;
  line-height: 1.6;
  color: var(--text-on-dark-muted);
  margin: 0;
}

.login-field {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.login-field + .login-field {
  margin-top: 18px;
}

.login-field label {
  font-size: 11px;
  color: var(--text-on-dark-muted);
  text-transform: uppercase;
  letter-spacing: 0.08em;
  font-weight: 500;
}

.login-field input {
  width: 100%;
  height: 34px;
  background: transparent;
  border: 0;
  border-bottom: 1px solid rgba(138, 148, 163, 0.4);
  color: var(--text-on-dark);
  padding: 0 0 8px;
  font-family: var(--font-mono);
  font-size: 14px;
  transition: border-color 0.12s;
  border-radius: 0;
}

.login-field input:focus {
  outline: none;
  border-bottom-color: var(--brand-on-dark);
}

.login-actions {
  margin-top: 26px;
}

.login-actions button {
  width: 100%;
  height: 40px;
  border: 0;
  border-radius: 6px;
  background: var(--brand-on-dark);
  color: #0e1114;
  font-size: 13px;
  font-weight: 600;
  letter-spacing: 0.01em;
  cursor: pointer;
  font-family: inherit;
  transition: opacity 0.12s;
}

.login-actions button:hover:not(:disabled) {
  opacity: 0.85;
}

.login-actions button:disabled {
  opacity: 0.45;
  cursor: not-allowed;
}

.login-links {
  margin-top: 22px;
  display: flex;
  justify-content: space-between;
  align-items: center;
  font-size: 12px;
  color: var(--text-on-dark-muted);
}

.login-links a {
  color: var(--text-on-dark-muted);
  text-decoration: none;
  cursor: pointer;
}

.login-links a:hover {
  color: #fff;
}

.login-lang-wrapper {
  display: flex;
  align-items: center;
}

.login-lang {
  border: 0;
  box-shadow: none;
  font-size: 12px;
  background: none;
  color: var(--text-on-dark-muted);
  cursor: pointer;
  padding-right: 4px;
  height: auto;
}

.login-lang:hover {
  color: #fff;
}

.login-colophon {
  font-size: 11px;
  color: var(--text-on-dark-dim);
  font-family: var(--font-mono);
}

.login-colophon a {
  color: var(--text-on-dark-dim); /* inherit the dim grey from the parent instead of browser blue */
  text-decoration: none;
}

.login-right {
  background: var(--bg-sidebar);

  @media (max-width: 900px) {
    display: none;
  }
}

@media (max-width: 900px) {
  .login-viewport {
    grid-template-columns: 1fr;
  }
}
</style>

<i18n>
{
  "en": {
    "title": "WB-MGE v3 interface",
    "sign_in": "Login",
    "documentation": "Documentation",
    "wrong_credentials": "Please enter correct login and password",
    "description": "Sign in to configure serial ports, inspect live traffic, and manage the register map that the gateway auto-discovers from the bus.",
    "heading": "MGE: Your&nbsp;<em>Swiss Army Knife</em>&nbsp;for Modbus"
  },
  "ru": {
    "title": "Интерфейс WB-MGE v3",
    "sign_in": "Войти",
    "documentation": "Документация",
    "wrong_credentials": "Введены неверные логин или пароль",
    "description": "Войдите, чтобы настроить последовательные порты, просматривать трафик и управлять картой регистров, автоматически обнаруживаемых шлюзом.",
    "heading": "MGE: &nbsp;<em>швейцарский нож</em>&nbsp;для Modbus"
  },
  "kk": {
    "title": "WB-MGE интерфейсі",
    "sign_in": "Кіру",
    "documentation": "Құжаттама",
    "wrong_credentials": "Логин немесе құпиясөз қате енгізілді",
    "description": "Сериялық порттарды конфигурациялау, тікелей трафикті тексеру және шлюз автоматты түрде анықтайтын регистрлер картасын басқару үшін кіріңіз.",
    "heading": "MGE: Modbus үшін сіздің&nbsp;<em>швейцариялық пышағыңыз</em>"
  },
  "it": {
    "title": "Interfaccia WB-MGE v3",
    "sign_in": "Accedi",
    "documentation": "Documentazione",
    "wrong_credentials": "Inserisci login e password corretti",
    "description": "Accedi per configurare le porte seriali, ispezionare il traffico in tempo reale e gestire la mappa dei registri rilevati automaticamente dal gateway.",
    "heading": "MGE: Il vostro&nbsp;<em>coltellino svizzero</em>&nbsp;per Modbus"
  },
  "de": {
    "title": "WB-MGEv3-Oberfläche",
    "sign_in": "Einloggen",
    "documentation": "Dokumentation",
    "wrong_credentials": "Bitte korrekten Benutzernamen und Passwort eingeben",
    "description": "Melden Sie sich an, um serielle Ports zu konfigurieren, den Live-Datenverkehr zu überwachen und die vom Gateway automatisch erkannte Registerkarte zu verwalten.",
    "heading": "MGE: Ihr&nbsp;<em>Schweizer Taschenmesser</em>&nbsp;für Modbus"
  }
}
</i18n>
