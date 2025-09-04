<script setup lang="ts">
import { reactive, ref } from 'vue';
import { useI18n } from 'vue-i18n';
import { useRoute, useRouter } from 'vue-router';
import Logo from '@/assets/logo.svg?component';
import { useAlerts } from '@/common/alert';
import { changeLang, Locale } from '@/i18n';
import { documentation } from '@/common/links';
import type { Auth } from '@/common/types';
import AlertsWrapper from '@/components/AlertsWrapper.vue';
import { api } from '@/utils/api';

const { t, locale } = useI18n();
const router = useRouter();
const route = useRoute();
const { showAlert } = useAlerts();
const data = reactive({ login: '', pass: '' });
const isLoading = ref(false);

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

    <fieldset class="login-wrapper">
      <form class="login-form" @submit.prevent="login">
        <div class="login-fields">
          <label for="username">{{ t('login') }}</label>
          <input id="username" v-model="data.login" name="username" type="text" autocomplete="username" required autofocus />

          <label for="password">{{ t('password') }}</label>
          <input id="password" v-model="data.pass" name="pass" type="password" autocomplete="current-password" required />
        </div>

        <div class="login-actions">
          <button type="submit" :disabled="isLoading">{{ t('sign_in') }}</button>
        </div>
      </form>
    </fieldset>
    <nav class="login-links">
      <a :href="documentation" target="_blank">{{ t('documentation') }}</a>

      <label class="login-languageWrapper">
        <span class="login-languageIcon" />
        <select v-model="locale" class="login-language" @change="changeLang(locale as Locale)">
          <option value="en">English</option>
          <option value="ru">Русский</option>
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
  margin-top: -60px;
  padding: 0 24px;

  @media( max-height: 600px ) {
    margin-top: 0;
  }
}

.login-wrapper {
  margin-top: 24px;
  padding: 0;
  max-width: 350px;
  width: 100%;
}

.login-fields {
  padding: 24px;
  gap: 12px;
  display: flex;
  flex-direction: column;
}

.login-actions {
  padding: 16px 24px;
  display: flex;
  align-items: center;
  justify-content: end;
  background: var(--sidebar-background);
}

.login-links {
  display: flex;
  gap: 24px;
  align-items: center;
  justify-content: space-between;
  width: 100%;
  max-width: 350px;
  box-sizing: border-box;
  padding: 0 12px 0 24px;
}

.login-links a {
  font-size: 14px;
}

.login-languageWrapper {
  display: flex;
  align-items: center;
}

.login-language {
  border: 0;
  box-shadow: none;
  font-size: 14px;
  background: none;
  color: var(--link-color);
  cursor: pointer;
  padding-right: 12px;
}

.login-languageIcon {
  background-image: url("@/assets/locale.svg");
  background-repeat: no-repeat;
  background-position:center;
  background-size: 14px;
  height: 14px;
  width: 14px;
}
</style>

<i18n>
{
  "en": {
    "sign_in": "Sign in",
    "documentation": "Documentation",
    "wrong_credentials": "Please enter correct login and password"
  },
  "ru": {
    "sign_in": "Войти",
    "documentation": "Документация",
    "wrong_credentials": "Введены неверные логин или пароль"
  }
}
</i18n>
