<script setup lang="ts">
import { ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';
import { useRoute, useRouter } from 'vue-router';

const { t } = useI18n();
const route = useRoute();
const isShowMenu = ref(false);
const router = useRouter();

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
      <img src="/logo.svg" alt="Wiren Board">
    </RouterLink>

    <svg
      class="sidebar-burger"
      width="46.08"
      height="46.08"
      viewBox="0 0 20 20"
      fill="#222F3D"
      @click="isShowMenu = !isShowMenu"
    >
      <path fill-rule="evenodd" d="M3 5a1 1 0 011-1h12a1 1 0 110 2H4a1 1 0 01-1-1zm0 5a1 1 0 011-1h12a1 1 0 110 2H4a1 1 0 01-1-1zm6 5a1 1 0 011-1h6a1 1 0 110 2h-6a1 1 0 01-1-1z" />
    </svg>

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
        <svg class="sidebar-logoutIcon" width="46.08" height="46.08" viewBox="0 0 512 512"><path fill="none" stroke="currentColor" stroke-width="32" d="M320 176v-40a40 40 0 00-40-40H88a40 40 0 00-40 40v240a40 40 0 0040 40h192a40 40 0 0040-40v-40M384 176l80 80-80 80M191 256h273"></path></svg>
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

.sidebar-navigation a:hover {
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
</style>

<i18n>
{
  "en": {
    "logout": "Logout"
  }
}
</i18n>
