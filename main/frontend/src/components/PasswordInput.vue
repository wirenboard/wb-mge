<script setup lang="ts">
import { ref } from 'vue';
import { useI18n } from 'vue-i18n';

// Attributes (id, name, pattern, placeholder, autocomplete, @input, ...) fall through
// to the inner <input>, not the wrapper element.
defineOptions({ inheritAttrs: false });

const { t } = useI18n();
const model = defineModel<string>();
const revealed = ref(false);
</script>

<template>
  <div class="passwordInput">
    <input
      v-model="model"
      class="passwordInput-field"
      v-bind="$attrs"
      :type="revealed ? 'text' : 'password'" />
    <button
      type="button"
      class="passwordInput-toggle"
      :aria-label="revealed ? t('hide_password') : t('show_password')"
      @click="revealed = !revealed">
      <svg v-if="revealed" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
        <path d="M17.94 17.94A10.07 10.07 0 0 1 12 20c-7 0-11-8-11-8a18.45 18.45 0 0 1 5.06-5.94M9.9 4.24A9.12 9.12 0 0 1 12 4c7 0 11 8 11 8a18.5 18.5 0 0 1-2.16 3.19m-6.72-1.07a3 3 0 1 1-4.24-4.24" />
        <line x1="1" y1="1" x2="23" y2="23" />
      </svg>
      <svg v-else width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
        <path d="M1 12s4-7 11-7 11 7 11 7-4 7-11 7-11-7-11-7z" />
        <circle cx="12" cy="12" r="3" />
      </svg>
    </button>
  </div>
</template>

<style scoped>
.passwordInput {
  position: relative;
  width: 100%;
}

.passwordInput-field {
  padding-right: 36px;
}

.passwordInput-toggle {
  position: absolute;
  top: 0;
  right: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  width: 32px;
  height: 30px;
  padding: 0;
  border: 0;
  background: transparent;
  color: var(--text-muted);
  cursor: pointer;
}

.passwordInput-toggle:hover {
  color: var(--text-color);
}
</style>

<i18n>
{
  "en": {
    "show_password": "Show password",
    "hide_password": "Hide password"
  },
  "ru": {
    "show_password": "Показать пароль",
    "hide_password": "Скрыть пароль"
  },
  "kk": {
    "show_password": "Құпиясөзді көрсету",
    "hide_password": "Құпиясөзді жасыру"
  },
  "it": {
    "show_password": "Mostra password",
    "hide_password": "Nascondi password"
  },
  "de": {
    "show_password": "Passwort anzeigen",
    "hide_password": "Passwort verbergen"
  }
}
</i18n>
