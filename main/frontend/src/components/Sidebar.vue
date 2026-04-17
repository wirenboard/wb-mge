<script setup lang="ts">
import { onMounted, ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';
import { useRoute, useRouter } from 'vue-router';
import Logo from '@/assets/logo.svg?component';
import LogoutIcon from '@/assets/logout.svg?component';
import MenuIcon from '@/assets/menu.svg?component';
import { useHostname } from '@/common/hostname';

const { t } = useI18n();
const route = useRoute();
const isShowMenu = ref(false);
const router = useRouter();
const { hostname, fetchHostname } = useHostname();
onMounted(() => fetchHostname());

watch(
  () => route.fullPath,
  () => {
    isShowMenu.value = false;
  }
);
</script>

<template>
  <aside class="sidebar">
    <RouterLink to="/" class="sidebar-logo">
      <Logo alt="Wiren Board" />
      <div v-if="hostname" class="sidebar-hostname">{{ hostname }}</div>
    </RouterLink>

    <MenuIcon class="sidebar-burger" @click="isShowMenu = !isShowMenu" />

    <nav
      :class="{
      'sidebar-navigation': true,
      'sidebar-navigationHide': !isShowMenu,
    }">
      <div class="sidebar-links">
        <RouterLink
          v-for="link in router.options.routes.filter(route => route.meta?.menuName)"
          :key="link.path"
          :to="link.path">
          {{ t(link.meta?.menuName as string) }}
        </RouterLink>
      </div>
      <RouterLink to="/logout">
        <LogoutIcon class="sidebar-logoutIcon" />
        {{ t('logout') }}
      </RouterLink>
    </nav>
  </aside>
</template>

<style scoped>
.sidebar {
  background: var(--sidebar-background);
  min-width: var(--sidebar-width);
  height: 100dvh;
  display: flex;
  flex-direction: column;

  @media (max-width: 680px) {
    flex-direction: row;
    justify-content: space-between;
    align-items: center;
    height: auto;
  }
}

.sidebar-logo {
  max-width: fit-content;
  margin: 24px auto;

  @media (max-width: 680px) {
    margin: 12px 12px 8px;
    flex-direction: row;
  }
}

.sidebar-navigation {
  padding-top: 24px;
  display: flex;
  flex-direction: column;
  height: 100%;
  z-index: 1;

  @media (max-width: 680px) {
    position: fixed;
    height: calc(100dvh - 84px);
    top: 60px;
    width: 200px;
    right: 0;
    background: var(--sidebar-background-mobile);
  }
}
.sidebar-navigation.sidebar-navigationHide {
  @media (max-width: 680px) {
    display: none;
  }
}

.sidebar-navigation a {
  color: var(--text-color);
  text-decoration: none;
  padding: 12px 24px;
  display: flex;
  align-items: center;
  cursor: pointer;
}

.sidebar-navigation a:hover,
.sidebar-navigation a:focus {
  background: var(--primary-color-hover);
  color: #fff;
}

.sidebar-navigation a.router-link-active {
  background: var(--primary-color) !important;
  color: #fff !important;
}

.sidebar-burger {
  width: 36px;
  margin-right: 6px;
  fill: var(--text-color);
  cursor: pointer;

  @media (min-width: 680px) {
    display: none;
  }
}

.sidebar-burger:hover {
  fill: var(--primary-color);
}

.sidebar-links {
  display: flex;
  flex-direction: column;
  flex-grow: 1;
}

.sidebar-logoutIcon {
  transform: scale(-1, 1);
  width: 20px;
  height: 20px;
  margin-right: 6px;
}

.sidebar-hostname {
  font-size: 11px;
  color: var(--text-color-secondary, #888);
  text-align: center;
  margin-top: 4px;
  word-break: break-all;
}
</style>

<i18n>
{
  "en": {
    "logout": "Logout"
  },
  "ru": {
    "logout": "Выйти"
  },
  "kk": {
    "logout": "Шығу"
  },
  "it": {
    "logout": "Esci"
  },
  "de": {
    "logout": "Abmelden"
  }
}
</i18n>
