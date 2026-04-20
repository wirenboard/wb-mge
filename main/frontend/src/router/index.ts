import { createRouter, createWebHashHistory, } from 'vue-router';
import { useInfo } from '@/common/info';
import { hasSession } from '@/common/session';
import { useSettings } from '@/common/settings';
import type { LogoutResponse } from '@/common/types';
import Dashboard from '@/views/Dashboard.vue';
import Login from '@/views/Login.vue';
import Network from '@/views/Network.vue';
import SerialPorts from '@/views/SerialPorts.vue';
import System from '@/views/System.vue';
import TcpGateway from '@/views/TcpGateway.vue';
import { api } from '@/utils/api';
import { checkSession } from './checkSession';

const router = createRouter({
  history: createWebHashHistory(),
  routes: [
    {
      path: '/',
      name: 'dashboard',
      component: Dashboard,
      meta: { requiresAuth: true, menuName: 'dashboard', menuGroup: 'overview', menuIcon: 'gauge' },
      beforeEnter: [checkSession, async () => {
        const { fetchInfo } = useInfo();
        const { refresh } = useSettings();
        await Promise.all([
          fetchInfo(),
          refresh(),
        ]);
      }],
    },
    {
      path: '/sniffer',
      name: 'sniffer',
      component: Sniffer,
      meta: { requiresAuth: true, menuName: 'sniffer', menuGroup: 'modbus_tools', menuIcon: 'activity' },
      beforeEnter: [checkSession, async () => {
        const { fetchInfo } = useInfo();
        const { refresh } = useSettings();
        await Promise.all([
          fetchInfo(),
          refresh(),
        ]);
      }],
    },
    {
      path: '/tcp-gateway',
      name: 'tcp_gateway',
      component: TcpGateway,
      meta: { requiresAuth: true, menuName: 'tcp_gateway', menuGroup: 'modbus_tools', menuIcon: 'plug' },
      beforeEnter: [checkSession, async () => {
        const { fetchInfo } = useInfo();
        const { refresh } = useSettings();
        await Promise.all([
          fetchInfo(),
          refresh(),
        ]);
      }],
    },
    {
      path: '/network',
      name: 'network',
      component: Network,
      meta: { requiresAuth: true, menuName: 'network', menuGroup: 'configuration', menuIcon: 'network' },
      beforeEnter: [checkSession, async () => {
        const { fetchInfo } = useInfo();
        const { refresh } = useSettings();
        await Promise.all([
          fetchInfo(),
          refresh(),
        ]);
      }],
    },
    {
      path: '/settings',
      name: 'settings',
      component: SerialPorts,
      meta: { requiresAuth: true, menuName: 'serial_ports', menuGroup: 'configuration', menuIcon: 'sliders' },
      beforeEnter: [checkSession, async () => {
        const { fetchInfo } = useInfo();
        const { refresh } = useSettings();
        await Promise.all([
          fetchInfo(),
          refresh(),
        ]);
      }],
    },
    {
      path: '/system',
      name: 'system',
      component: System,
      meta: { requiresAuth: true, menuName: 'system', menuGroup: 'configuration', menuIcon: 'cpu' },
      beforeEnter: [checkSession, async () => {
        const { fetchInfo } = useInfo();
        const { refresh } = useSettings();
        await Promise.all([
          fetchInfo(),
          refresh(),
        ]);
      }],
    },
    {
      path: '/login',
      name: 'login',
      component: Login,
      beforeEnter: [checkSession],
    },
    {
      path: '/logout',
      redirect: () => {
        api<LogoutResponse>('logout', { method: 'POST' });
        hasSession.value = false;
        return { path: '/login' };
      },
    },
  ],
});

export default router;
