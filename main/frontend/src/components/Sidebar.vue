<script setup lang="ts">
import { ref, watch, computed } from 'vue';
import type { Component } from 'vue';
import { useI18n } from 'vue-i18n';
import { useRoute, useRouter } from 'vue-router';
import type { RouteRecordRaw } from 'vue-router';
import Logo from '@/assets/logo.svg?component';
import MenuIcon from '@/assets/menu.svg?component';
import LogoutIcon from '@/assets/logout.svg?component';
import DocsIcon from '@/assets/docsIcon.svg?component';
import EmailIcon from '@/assets/emailIcon.svg?component';
import CartIcon from '@/assets/cartIcon.svg?component';
import GaugeIcon from '@/assets/gaugeIcon.svg?component';
import SlidersIcon from '@/assets/slidersIcon.svg?component';
import NetworkIcon from '@/assets/networkIcon.svg?component';
import CpuIcon from '@/assets/cpuIcon.svg?component';
import ActivityIcon from '@/assets/activityIcon.svg?component';
import PlugIcon from '@/assets/plugIcon.svg?component';
import GridSidebarIcon from '@/assets/gridSidebarIcon.svg?component';
import { useHostname } from '@/common/hostname';
import { useInfo } from '@/common/info';
import { useSettings } from '@/common/settings';
import { documentation, support, email, website } from '@/common/links';

// Map of menuIcon key → imported SVG component
const MENU_ICONS: Record<string, Component> = {
  gauge: GaugeIcon,
  sliders: SlidersIcon,
  network: NetworkIcon,
  cpu: CpuIcon,
  activity: ActivityIcon,
  plug: PlugIcon,
  grid: GridSidebarIcon,
};

// Helper: returns the icon component for a given menuIcon key
function getMenuIcon(key: unknown): Component | undefined {
  return typeof key === 'string' ? MENU_ICONS[key] : undefined;
}

// Helper: returns the i18n key as a string for menu link name
function getMenuName(name: unknown): string {
  return typeof name === 'string' ? name : '';
}

const { t, locale } = useI18n();
const route = useRoute();
const isShowMenu = ref(false);
const router = useRouter();
const { hostname } = useHostname();
const { info } = useInfo();
const { initData: savedSettings } = useSettings();

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
            <component :is="getMenuIcon(link.meta?.menuIcon)" class="sidebar-icon" />
            {{ t(getMenuName(link.meta?.menuName)) }}
          </RouterLink>
        </template>
      </div>
      <div v-if="info && savedSettings" class="sb-ports">
        <div class="sb-port">
          <div class="sb-port-head">
            <span class="sb-port-name">Port 1</span>
            <span :class="['sb-port-state', info.rs485_1.is_busy ? 'on' : 'off']">
              <span class="dot" />{{ info.rs485_1.is_busy ? 'ACTIVE' : 'IDLE' }}
            </span>
          </div>
          <div class="sb-port-row"><span class="sb-port-k">{{ t('mode') }}</span><span class="sb-port-v">{{ t(`port_mode_${info.rs485_1.port_mode}`, info.rs485_1.port_mode) }}</span></div>
          <div class="sb-port-row mono">
            <span class="sb-port-k">Line</span>
            <span class="sb-port-v">
              {{ savedSettings.rs485_1.baudrate }} · 8{{ savedSettings.rs485_1.parity === 'none' ? 'N' : savedSettings.rs485_1.parity === 'even' ? 'E' : 'O' }}{{ savedSettings.rs485_1.stopbits }}<template v-if="savedSettings.rs485_1.tx_disabled"> · <span class="sb-tx-off">TX</span></template>
            </span>
          </div>
        </div>
        <div class="sb-port">
          <div class="sb-port-head">
            <span class="sb-port-name">Port 2</span>
            <span :class="['sb-port-state', info.rs485_2.is_busy ? 'on' : 'off']">
              <span class="dot" />{{ info.rs485_2.is_busy ? 'ACTIVE' : 'IDLE' }}
            </span>
          </div>
          <div class="sb-port-row"><span class="sb-port-k">{{ t('mode') }}</span><span class="sb-port-v">{{ t(`port_mode_${info.rs485_2.port_mode}`, info.rs485_2.port_mode) }}</span></div>
          <div class="sb-port-row mono">
            <span class="sb-port-k">Line</span>
            <span class="sb-port-v">
              {{ savedSettings.rs485_2.baudrate }} · 8{{ savedSettings.rs485_2.parity === 'none' ? 'N' : savedSettings.rs485_2.parity === 'even' ? 'E' : 'O' }}{{ savedSettings.rs485_2.stopbits }}<template v-if="savedSettings.rs485_2.tx_disabled"> · <span class="sb-tx-off">TX</span></template>
            </span>
          </div>
        </div>
      </div>

      <div class="sb-links">
        <a :href="documentation" target="_blank" class="sb-link">
          <DocsIcon />
          {{ t('link_docs') }}
        </a>
        <a :href="locale === 'ru' ? support : `mailto:${email}`" target="_blank" class="sb-link">
          <EmailIcon />
          {{ t('link_support') }}
        </a>
        <a :href="website" target="_blank" class="sb-link">
          <CartIcon />
          {{ t('link_buy') }}
        </a>
      </div>
      <RouterLink to="/logout" class="sidebar-logout">
        <LogoutIcon class="sidebar-icon" />
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
  color: #6e7580;
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

.sb-ports {
  display: flex;
  flex-direction: column;
  padding: 4px 0 10px;
  border-bottom: 1px solid var(--border-sidebar);
}

.sb-port {
  padding: 8px 10px;
  display: flex;
  flex-direction: column;
  gap: 3px;
}

.sb-port + .sb-port {
  border-top: 1px solid var(--border-sidebar);
  margin-top: 4px;
  padding-top: 10px;
}

.sb-port-head {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 2px;
}

.sb-port-name {
  font-size: 12px;
  font-weight: 600;
  color: #fff;
  letter-spacing: 0.01em;
}

.sb-port-state {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  font-size: 10px;
  text-transform: uppercase;
  letter-spacing: 0.08em;
  font-weight: 500;
}

.sb-port-state .dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
}

.sb-port-state.on {
  color: var(--brand-on-dark);
}

.sb-port-state.on .dot {
  background: var(--brand-on-dark);
  box-shadow: 0 0 0 3px color-mix(in oklch, var(--brand-on-dark) 20%, transparent);
}

.sb-port-state.off {
  color: var(--text-on-dark-dim);
}

.sb-port-state.off .dot {
  background: var(--text-on-dark-dim);
}

.sb-port-row {
  display: flex;
  justify-content: space-between;
  font-size: 11px;
  color: var(--text-on-dark-muted);
}

.sb-port-row.mono {
  font-family: var(--font-mono);
  letter-spacing: 0.02em;
}

.sb-port-k {
  color: #6e7580;
}

.sb-port-v {
  color: var(--text-on-dark);
}

.sb-tx-off {
  text-decoration: line-through;
  color: var(--danger-color);
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
    "group_modbus_tools": "Modbus tools",
    "group_configuration": "Configuration",
    "link_docs": "Documentation",
    "link_support": "Support",
    "link_buy": "Buy devices",
    "logout": "Logout",
    "mode": "Mode",
    "port_mode_disabled": "Disabled",
    "port_mode_tcp_bridge": "TCP bridge",
    "port_mode_sniffer": "Sniffer",
    "port_mode_cache_bus": "Cache bus"
  },
  "ru": {
    "group_overview": "Обзор",
    "group_modbus_tools": "Инструменты Modbus",
    "group_configuration": "Конфигурация",
    "link_docs": "Документация",
    "link_support": "Техподдержка",
    "link_buy": "Купить устройства",
    "logout": "Выйти",
    "mode": "Режим",
    "port_mode_disabled": "Отключён",
    "port_mode_tcp_bridge": "TCP-мост",
    "port_mode_sniffer": "Сниффер",
    "port_mode_cache_bus": "Кэш шины"
  },
  "kk": {
    "group_overview": "Шолу",
    "group_modbus_tools": "Modbus құралдары",
    "group_configuration": "Конфигурация",
    "link_docs": "Құжаттама",
    "link_support": "Қолдау",
    "link_buy": "Құрылғыларды сатып алу",
    "logout": "Шығу",
    "mode": "Режим",
    "port_mode_disabled": "Өшірілген",
    "port_mode_tcp_bridge": "TCP көпір",
    "port_mode_sniffer": "Sniffer",
    "port_mode_cache_bus": "Кэш шина"
  },
  "it": {
    "group_overview": "Panoramica",
    "group_modbus_tools": "Strumenti Modbus",
    "group_configuration": "Configurazione",
    "link_docs": "Documentazione",
    "link_support": "Supporto",
    "link_buy": "Acquista dispositivi",
    "logout": "Esci",
    "mode": "Modalità",
    "port_mode_disabled": "Disabilitato",
    "port_mode_tcp_bridge": "Bridge TCP",
    "port_mode_sniffer": "Sniffer",
    "port_mode_cache_bus": "Cache bus"
  },
  "de": {
    "group_overview": "Übersicht",
    "group_modbus_tools": "Modbus-Werkzeuge",
    "group_configuration": "Konfiguration",
    "link_docs": "Dokumentation",
    "link_support": "Support",
    "link_buy": "Geräte kaufen",
    "logout": "Abmelden",
    "mode": "Modus",
    "port_mode_disabled": "Deaktiviert",
    "port_mode_tcp_bridge": "TCP-Bridge",
    "port_mode_sniffer": "Sniffer",
    "port_mode_cache_bus": "Cache-Bus"
  }
}
</i18n>
