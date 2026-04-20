<script setup lang="ts">
import { ref, watch, computed } from 'vue';
import { useI18n } from 'vue-i18n';
import { useRoute, useRouter } from 'vue-router';
import type { RouteRecordRaw } from 'vue-router';
import Logo from '@/assets/logo.svg?component';
import MenuIcon from '@/assets/menu.svg?component';
import { useHostname } from '@/common/hostname';
import { documentation, support, email, website } from '@/common/links';

const { t, locale } = useI18n();
const route = useRoute();
const isShowMenu = ref(false);
const router = useRouter();
const { hostname } = useHostname();

const menuGroups = computed(() => {
  const routes = router.options.routes.filter(r => r.meta?.menuName) as RouteRecordRaw[];
  const groups: Record<string, RouteRecordRaw[]> = {};
  for (const r of routes) {
    const group = (r.meta?.menuGroup as string) || 'default';
    if (!groups[group]) groups[group] = [];
    groups[group].push(r);
  }
  return groups;
});

watch(
  () => route.fullPath,
  () => {
    isShowMenu.value = false;
  }
);
</script>

<template>
  <aside class="sidebar">
    <div class="sb-brand">
      <RouterLink to="/" class="sidebar-logo">
        <Logo alt="Wiren Board" />
      </RouterLink>
      <div v-if="hostname" class="sidebar-hostname">{{ hostname }}</div>
    </div>

    <MenuIcon class="sidebar-burger" @click="isShowMenu = !isShowMenu" />

    <nav
      :class="{
      'sidebar-navigation': true,
      'sidebar-navigationHide': !isShowMenu,
    }">
      <div class="sidebar-links">
        <template v-for="(routes, group) in menuGroups" :key="group">
          <div class="group-label">{{ t(`group_${group}`) }}</div>
          <RouterLink
            v-for="link in routes"
            :key="link.path"
            :to="link.path">
            <svg class="sidebar-icon" width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">
              <template v-if="link.meta?.menuIcon === 'gauge'">
                <path d="M2 11a6 6 0 0 1 12 0" />
                <path d="M8 11l3-3" />
                <circle cx="8" cy="11" r="0.6" fill="currentColor" stroke="none" />
              </template>
              <template v-else-if="link.meta?.menuIcon === 'sliders'">
                <path d="M3 4h10M3 8h6M3 12h10" />
                <circle cx="11" cy="4" r="1.2" />
                <circle cx="7" cy="8" r="1.2" />
                <circle cx="11" cy="12" r="1.2" />
              </template>
              <template v-else-if="link.meta?.menuIcon === 'network'">
                <rect x="2" y="10" width="12" height="4" rx="1" />
                <path d="M8 10V6M4 6h8M4 6V3M12 6V3" />
              </template>
              <template v-else-if="link.meta?.menuIcon === 'cpu'">
                <rect x="4" y="4" width="8" height="8" rx="1" />
                <rect x="6" y="6" width="4" height="4" />
                <path d="M6 2v2M10 2v2M6 12v2M10 12v2M2 6h2M2 10h2M12 6h2M12 10h2" />
              </template>
            </svg>
            {{ t(link.meta?.menuName as string) }}
          </RouterLink>
        </template>
      </div>
      <div class="sb-links">
        <a :href="documentation" target="_blank" class="sb-link">
          <svg width="12" height="12" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M4 2h6l2 2v10a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1V3a1 1 0 0 1 1-1zM5 6h5M5 9h5M5 12h3"/></svg>
          {{ t('link_docs') }}
        </a>
        <a :href="locale === 'ru' ? support : `mailto:${email}`" target="_blank" class="sb-link">
          <svg width="12" height="12" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><rect x="2" y="4" width="12" height="9" rx="1"/><path d="M2 5l6 4 6-4"/></svg>
          {{ t('link_support') }}
        </a>
        <a :href="website" target="_blank" class="sb-link">
          <svg width="12" height="12" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M1 2h2l1.5 8.5a1 1 0 0 0 1 .8h6.3a1 1 0 0 0 1-.78L14 5H4"/><circle cx="6" cy="13.5" r="0.7"/><circle cx="12" cy="13.5" r="0.7"/></svg>
          {{ t('link_buy') }}
        </a>
      </div>
      <RouterLink to="/logout" class="sidebar-logout">
        <svg class="sidebar-icon" width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">
          <path d="M10 11v1a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v1M7 8h7m0 0l-2-2m2 2l-2 2" />
        </svg>
        {{ t('logout') }}
      </RouterLink>
    </nav>
  </aside>
</template>

<style scoped>
.sidebar {
  background: var(--bg-sidebar);
  color: var(--text-on-dark);
  min-width: var(--sidebar-width);
  height: 100dvh;
  display: flex;
  flex-direction: column;
  border-right: 1px solid var(--border-sidebar);

  @media (max-width: 680px) {
    flex-direction: row;
    justify-content: space-between;
    align-items: center;
    height: auto;
    border-right: none;
    border-bottom: 1px solid var(--border-sidebar);
  }
}

.sb-brand {
  padding: 18px 20px 16px;
  border-bottom: 1px solid var(--border-sidebar);

  @media (max-width: 680px) {
    padding: 8px 12px;
    border-bottom: none;
    display: flex;
    align-items: center;
    gap: 8px;
  }
}

.sidebar-logo {
  display: block;

  @media (max-width: 680px) {
    display: flex;
    align-items: center;
  }
}

.sidebar-logo :deep(svg) {
  height: 22px;
  width: auto;
  display: block;
}

.sidebar-hostname {
  font-size: 11px;
  color: var(--text-on-dark-muted);
  margin-top: 6px;
  word-break: break-all;

  @media (max-width: 680px) {
    margin-top: 0;
  }
}

.sidebar-navigation {
  padding: 8px 10px;
  display: flex;
  flex-direction: column;
  flex: 1;
  overflow-y: auto;
  z-index: 1;

  @media (max-width: 680px) {
    position: fixed;
    height: calc(100dvh - 60px);
    top: 60px;
    width: 220px;
    right: 0;
    background: var(--bg-sidebar);
    padding: 12px 10px;
  }
}

.sidebar-navigation.sidebar-navigationHide {
  @media (max-width: 680px) {
    display: none;
  }
}

.group-label {
  font-size: 10px;
  text-transform: uppercase;
  letter-spacing: 0.1em;
  color: var(--text-on-dark-dim);
  padding: 12px 10px 6px;
}

.sidebar-navigation a {
  color: var(--text-on-dark);
  text-decoration: none;
  padding: 8px 10px;
  border-radius: var(--r-md);
  display: flex;
  align-items: center;
  gap: 10px;
  cursor: pointer;
  font-size: 13px;
  line-height: 1.2;
  transition: background 0.1s;
}

.sidebar-navigation a:hover,
.sidebar-navigation a:focus {
  background: var(--bg-sidebar-hover);
  color: #fff;
  text-decoration: none;
}

.sidebar-navigation a.router-link-active {
  background: var(--bg-sidebar-hover) !important;
  color: #fff !important;
  box-shadow: inset 2px 0 0 var(--brand-on-dark);
}

.sidebar-navigation a.router-link-active .sidebar-icon {
  color: var(--brand-on-dark);
  opacity: 1;
}

.sidebar-icon {
  flex-shrink: 0;
  opacity: 0.7;
}

.sidebar-burger {
  width: 36px;
  margin-right: 6px;
  fill: var(--text-on-dark);
  cursor: pointer;

  @media (min-width: 680px) {
    display: none;
  }
}

.sidebar-burger:hover {
  fill: var(--brand-on-dark);
}

.sidebar-links {
  display: flex;
  flex-direction: column;
  flex-grow: 1;
}

.sb-links {
  display: flex;
  flex-direction: column;
  padding: 6px 0 2px;
}

.sidebar-navigation a.sb-link {
  padding: 6px 10px;
  color: var(--text-on-dark-muted);
  font-size: 12px;
  gap: 9px;
}

.sidebar-navigation a.sb-link:hover,
.sidebar-navigation a.sb-link:focus {
  color: #fff;
  background: var(--bg-sidebar-hover);
}

.sidebar-navigation a.sidebar-logout {
  padding: 8px 10px;
  margin-top: 2px;
  border-top: 1px solid var(--border-sidebar);
  border-radius: 0;
  color: var(--text-on-dark-muted);
  text-decoration: none;
  font-size: 12px;
  display: flex;
  align-items: center;
  gap: 9px;
  cursor: pointer;
}

.sidebar-navigation a.sidebar-logout:hover,
.sidebar-navigation a.sidebar-logout:focus {
  color: #fff;
  background: none;
  text-decoration: none;
}
</style>

<i18n>
{
  "en": {
    "group_overview": "Overview",
    "group_configuration": "Configuration",
    "link_docs": "Documentation",
    "link_support": "Support",
    "link_buy": "Buy devices",
    "logout": "Logout"
  },
  "ru": {
    "group_overview": "Обзор",
    "group_configuration": "Конфигурация",
    "link_docs": "Документация",
    "link_support": "Техподдержка",
    "link_buy": "Купить устройства",
    "logout": "Выйти"
  },
  "kk": {
    "group_overview": "Шолу",
    "group_configuration": "Конфигурация",
    "link_docs": "Құжаттама",
    "link_support": "Қолдау",
    "link_buy": "Құрылғыларды сатып алу",
    "logout": "Шығу"
  },
  "it": {
    "group_overview": "Panoramica",
    "group_configuration": "Configurazione",
    "link_docs": "Documentazione",
    "link_support": "Supporto",
    "link_buy": "Acquista dispositivi",
    "logout": "Esci"
  },
  "de": {
    "group_overview": "Übersicht",
    "group_configuration": "Konfiguration",
    "link_docs": "Dokumentation",
    "link_support": "Support",
    "link_buy": "Geräte kaufen",
    "logout": "Abmelden"
  }
}
</i18n>
