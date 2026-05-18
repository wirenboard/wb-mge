<script setup lang="ts">
import { ref, watch, computed, type Component } from 'vue';
import { useI18n } from 'vue-i18n';
import { useRoute, useRouter } from 'vue-router';
import type { RouteRecordRaw } from 'vue-router';
import Logo from '@/assets/logo.svg?component';
import MenuIcon from '@/assets/menu.svg?component';
import DocsIcon from '@/assets/docs.svg?component';
import SupportIcon from '@/assets/support.svg?component';
import ShopIcon from '@/assets/shop.svg?component';
import LogoutIcon from '@/assets/logout.svg?component';
import GaugeIcon from '@/assets/gauge.svg?component';
import SlidersIcon from '@/assets/sliders.svg?component';
import NetworkIcon from '@/assets/network.svg?component';
import CpuIcon from '@/assets/cpu.svg?component';
import PlugIcon from '@/assets/plug.svg?component';
import { useHostname } from '@/common/hostname';
import { useInfo } from '@/common/info';
import { useSettings } from '@/common/settings';
import { documentation, support, email, website } from '@/common/links';

// Map from route meta.menuIcon string to the corresponding SVG component
const menuIconMap: Record<string, Component> = {
  gauge: GaugeIcon,
  sliders: SlidersIcon,
  network: NetworkIcon,
  cpu: CpuIcon,
  plug: PlugIcon,
};

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
      <a v-if="hostname" :href="`http://${hostname}.local`" target="_blank" rel="noopener noreferrer" class="sidebar-hostname">{{ hostname }}</a>
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
            <component
              v-if="menuIconMap[link.meta?.menuIcon as string]"
              :is="menuIconMap[link.meta?.menuIcon as string]"
              class="sidebar-icon"
            />
            {{ t(link.meta?.menuName as string) }}
          </RouterLink>
        </template>
      </div>
      <div v-if="info && savedSettings" class="sb-ports">
        <div class="sb-port">
          <div class="sb-port-head">
            <span class="sb-port-name">{{ t('port_1') }}</span>
            <span :class="['sb-port-state', info.rs485_1.is_busy ? 'on' : 'off']">
              <span class="dot" />{{ info.rs485_1.is_busy ? t('active') : t('idle') }}
            </span>
          </div>
          <div class="sb-port-row"><span class="sb-port-row-key">{{ t('mode') }}</span><span class="sb-port-row-value">{{ savedSettings.rs485_1.bridge.modbus ? t('modbus_tcp') : t('transparent') }}</span></div>
          <div class="sb-port-row mono"><span class="sb-port-row-key">{{ t('line') }}</span><span class="sb-port-row-value">{{ savedSettings.rs485_1.baudrate }} · 8{{ savedSettings.rs485_1.parity === 'none' ? 'N' : savedSettings.rs485_1.parity === 'even' ? 'E' : 'O' }}{{ savedSettings.rs485_1.stopbits }}</span></div>
        </div>
        <div class="sb-port">
          <div class="sb-port-head">
            <span class="sb-port-name">{{ t('port_2') }}</span>
            <span :class="['sb-port-state', info.rs485_2.is_busy ? 'on' : 'off']">
              <span class="dot" />{{ info.rs485_2.is_busy ? t('active') : t('idle') }}
            </span>
          </div>
          <div class="sb-port-row"><span class="sb-port-row-key">{{ t('mode') }}</span><span class="sb-port-row-value">{{ savedSettings.rs485_2.bridge.modbus ? t('modbus_tcp') : t('transparent') }}</span></div>
          <div class="sb-port-row mono"><span class="sb-port-row-key">{{ t('line') }}</span><span class="sb-port-row-value">{{ savedSettings.rs485_2.baudrate }} · 8{{ savedSettings.rs485_2.parity === 'none' ? 'N' : savedSettings.rs485_2.parity === 'even' ? 'E' : 'O' }}{{ savedSettings.rs485_2.stopbits }}</span></div>
        </div>
      </div>

      <div class="sb-links">
        <a :href="documentation" target="_blank" class="sb-link">
          <DocsIcon />
          {{ t('link_docs') }}
        </a>
        <a :href="locale === 'ru' ? support : `mailto:${email}`" target="_blank" class="sb-link">
          <SupportIcon />
          {{ t('link_support') }}
        </a>
        <a :href="website" target="_blank" class="sb-link">
          <ShopIcon />
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
  font-size: 11.8px; /* +0.8px for Roboto */
  color: var(--text-on-dark-muted);
  margin-top: 6px;
  word-break: break-all;
  text-decoration: none;

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
  font-size: 10.8px; /* +0.8px for Roboto */
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
  font-size: 13.8px; /* +0.8px for Roboto */
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
  font-size: 12px; /* original size — data display block */
  font-weight: 600;
  color: #fff;
  letter-spacing: 0.01em;
}

.sb-port-state {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  font-size: 10px; /* original size — data display block */
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
  font-size: 11px; /* original size — data display block */
  color: var(--text-on-dark-muted);
}

.sb-port-row.mono {
  font-family: var(--font-mono);
  letter-spacing: 0.02em;
}

.sb-port-row-key {
  color: #6e7580;
}

.sb-port-row-value {
  color: var(--text-on-dark);
}

.sb-links {
  display: flex;
  flex-direction: column;
  padding: 6px 0 2px;
}

.sidebar-navigation a.sb-link {
  padding: 6px 10px;
  color: var(--text-on-dark-muted);
  font-size: 12.8px; /* +0.8px for Roboto */
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
  font-size: 12.8px; /* +0.8px for Roboto */
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
    "active": "ACTIVE",
    "idle": "IDLE",
    "mode": "Mode",
    "line": "Line",
    "modbus_tcp": "Modbus TCP",
    "transparent": "Transparent bridge",
    "port_1": "Port 1",
    "port_2": "Port 2",
    "serial_ports": "Serial ports"
  },
  "ru": {
    "group_overview": "Обзор",
    "group_modbus_tools": "Инструменты Modbus",
    "group_configuration": "Конфигурация",
    "link_docs": "Документация",
    "link_support": "Техподдержка",
    "link_buy": "Купить устройства",
    "logout": "Выйти",
    "active": "ACTIVE",
    "idle": "IDLE",
    "mode": "Режим",
    "line": "Параметры",
    "modbus_tcp": "Modbus TCP",
    "transparent": "Прозрачный мост",
    "port_1": "Порт 1",
    "port_2": "Порт 2",
    "serial_ports": "Порты"
  },
  "kk": {
    "group_overview": "Шолу",
    "group_modbus_tools": "Modbus құралдары",
    "group_configuration": "Конфигурация",
    "link_docs": "Құжаттама",
    "link_support": "Қолдау",
    "link_buy": "Құрылғыларды сатып алу",
    "logout": "Шығу",
    "active": "ACTIVE",
    "idle": "IDLE",
    "mode": "Режим",
    "line": "Желі",
    "modbus_tcp": "Modbus TCP",
    "transparent": "Мөлдір көпір",
    "port_1": "Порт 1",
    "port_2": "Порт 2",
    "serial_ports": "Сериялық порттар"
  },
  "it": {
    "group_overview": "Panoramica",
    "group_modbus_tools": "Strumenti Modbus",
    "group_configuration": "Configurazione",
    "link_docs": "Documentazione",
    "link_support": "Supporto",
    "link_buy": "Acquista dispositivi",
    "logout": "Esci",
    "active": "ACTIVE",
    "idle": "IDLE",
    "mode": "Modalità",
    "line": "Linea",
    "modbus_tcp": "Modbus TCP",
    "transparent": "Bridge trasparente",
    "port_1": "Porta 1",
    "port_2": "Porta 2",
    "serial_ports": "Porte seriali"
  },
  "de": {
    "group_overview": "Übersicht",
    "group_modbus_tools": "Modbus-Werkzeuge",
    "group_configuration": "Konfiguration",
    "link_docs": "Dokumentation",
    "link_support": "Support",
    "link_buy": "Geräte kaufen",
    "logout": "Abmelden",
    "active": "ACTIVE",
    "idle": "IDLE",
    "mode": "Modus",
    "line": "Leitung",
    "modbus_tcp": "Modbus TCP",
    "transparent": "Transparente Brücke",
    "port_1": "Port 1",
    "port_2": "Port 2",
    "serial_ports": "Serielle Schnittst."
  }
}
</i18n>
