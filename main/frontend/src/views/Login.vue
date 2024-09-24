<script setup lang="ts">
import { reactive, ref } from 'vue';
import { useI18n } from 'vue-i18n';
import { useRoute, useRouter } from 'vue-router';
import Logo from '@/assets/logo.svg?component';
import { alertData } from '@/common/global';
import { Auth } from '@/common/types';
import Alert from '@/components/Alert.vue';
import { api } from '@/utils/api';

const { t } = useI18n();
const router = useRouter();
const route = useRoute();
const data = reactive({ login: '', pass: '' });
const isLoading = ref(false);
const login = async () => {
  isLoading.value = true;
  try {
    const { auth } = await api<Auth>('auth', data);
    if (auth) {
      await router.push(route.query.redirect ? `/${route.query.redirect}` : '/');
    } else {
      alertData.value = { type: 'error', message: t('wrong_credentials') };
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
          <label for="login">{{ t('login') }}</label>
          <input id="login" v-model="data.login" name="login" type="text" required autofocus />

          <label for="password">{{ t('password') }}</label>
          <input id="password" v-model="data.pass" name="pass" type="password" required />
        </div>

        <div class="login-actions">
          <button type="submit" :disabled="isLoading">{{ t('sign_in') }}</button>
        </div>
      </form>
    </fieldset>
    <nav class="login-links">
      <a href="https://wirenboard.com/wiki/WB-MGE_v.3_Modbus-Ethernet_Interface_Converter" target="_blank">{{ t('help') }}</a>
      <a href="https://wirenboard.com/" target="_blank">{{ t('website') }}</a>
    </nav>
  </section>
  <Alert />
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
}

.login-links a {
  color: var(--link-color);
  font-size: 14px;
  text-decoration: none;
}

.login-links a:hover {
  text-decoration: underline;
}
</style>

<i18n>
{
  "en": {
    "login": "Login",
    "password": "Password",
    "sign_in": "Sign in",
    "website": "Official website",
    "help": "Help",
    "wrong_credentials": "Please enter correct login and password"
  }
}
</i18n>
