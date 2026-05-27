/**
 * Integration tests for Network.vue — wifi_perm_disable behaviour.
 *
 * NW-I-001 — renders WiFi card when wifi_perm_disable is false
 * NW-I-002 — hides WiFi card when wifi_perm_disable is true
 * NW-I-003 — watch does not throw when wifi is undefined (optional-chaining guard)
 */

import { describe, it, expect, vi } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
import { ref } from 'vue';
import { createI18n } from 'vue-i18n';
import { createRouter, createMemoryHistory } from 'vue-router';

// ---------------------------------------------------------------------------
// Mocks — hoisted before any component import by Vitest's vi.mock hoisting.
// ---------------------------------------------------------------------------

vi.mock('@/common/network', () => ({
    useWifi: () => ({
        wifi: ref([]),
        isPolling: ref(false),
        startPolling: vi.fn(),
        stopPolling: vi.fn(),
    }),
}));

vi.mock('@/common/settings', () => ({
    useSettings: vi.fn(),
}));

vi.mock('@/utils/api', () => ({
    api: vi.fn().mockResolvedValue({}),
}));

vi.mock('@/utils/validation', () => ({
    onCustomValidation: vi.fn(),
}));

// Stub @unhead/vue so Heading.vue's injectHead() / useHead() do not throw.
vi.mock('@unhead/vue', () => ({
    injectHead: () => ({}),
    useHead: vi.fn(),
    createUnhead: vi.fn(() => ({})),
    headSymbol: Symbol('head'),
}));

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/** Build a minimal router required by Sidebar.vue (useRoute/useRouter). */
function makeRouter() {
    return createRouter({
        history: createMemoryHistory(),
        routes: [
            { path: '/', component: { template: '<div/>' } },
            { path: '/logout', component: { template: '<div/>' } },
        ],
    });
}

/** Minimal i18n instance — returns the key itself for any missing translation. */
const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });

// ---------------------------------------------------------------------------
// Test suite
// ---------------------------------------------------------------------------

describe('NW-I-001: renders WiFi card when wifi_perm_disable is false', () => {
    it('WiFi settings form is present in DOM when wifi_perm_disable=false', async () => {
        const { useSettings } = await import('@/common/settings');
        vi.mocked(useSettings).mockReturnValue({
            data: ref({
                wifi_perm_disable: false,
                wifi: {
                    mode: 'ap',
                    ap_ssid: 'TestAP',
                    ap_pass: '',
                    ap_auth: 'wpa2_psk',
                    ap_ip_static: '192.168.5.1',
                    ap_mask_static: '255.255.255.0',
                    ap_gw_static: '192.168.5.1',
                    sta_ssid: '',
                    sta_pass: '',
                    sta_auth: 'wpa2_psk',
                    sta_dhcpc: true,
                    sta_ip_static: '192.168.1.7',
                    sta_mask_static: '255.255.255.0',
                    sta_gw_static: '192.168.1.1',
                },
                ethernet: {
                    dhcpc: true,
                    ip_static: '192.168.0.7',
                    mask_static: '255.255.255.0',
                    gw_static: '192.168.0.1',
                },
            } as never),
            initData: ref(null),
            isChanged: () => false,
            isLoading: ref(false),
            updateSettings: vi.fn(),
        } as never);

        const { default: Network } = await import('@/views/Network.vue');
        const wrapper = mount(Network, {
            global: { plugins: [i18n, makeRouter()] },
        });
        await flushPromises();

        // When wifi_perm_disable=false the WiFi card section is rendered.
        // Use the specific #wifi_mode id to avoid matching unrelated selects.
        expect(wrapper.find('#wifi_mode').exists()).toBe(true);

        wrapper.unmount();
    });
});

describe('NW-I-002: hides WiFi card when wifi_perm_disable is true', () => {
    it('WiFi settings form is absent from DOM when wifi_perm_disable=true', async () => {
        const { useSettings } = await import('@/common/settings');
        vi.mocked(useSettings).mockReturnValue({
            data: ref({
                wifi_perm_disable: true,
                wifi: undefined,
                ethernet: {
                    dhcpc: true,
                    ip_static: '192.168.0.7',
                    mask_static: '255.255.255.0',
                    gw_static: '192.168.0.1',
                },
            } as never),
            initData: ref(null),
            isChanged: () => false,
            isLoading: ref(false),
            updateSettings: vi.fn(),
        } as never);

        const { default: Network } = await import('@/views/Network.vue');
        const wrapper = mount(Network, {
            global: { plugins: [i18n, makeRouter()] },
        });
        await flushPromises();

        // When wifi_perm_disable=true the WiFi card is hidden via v-if.
        // Use the specific #wifi_mode id to avoid false-negatives from other selects.
        expect(wrapper.find('#wifi_mode').exists()).toBe(false);

        wrapper.unmount();
    });
});

describe('NW-I-003: watch does not throw when wifi is undefined', () => {
    it('NW-I-003 — watch does not throw when wifi is undefined (perm_disabled=true)', async () => {
        const { useSettings } = await import('@/common/settings');
        vi.mocked(useSettings).mockReturnValue({
            data: ref({
                wifi_perm_disable: true,
                wifi: undefined,
                ethernet: {
                    dhcpc: true,
                    ip_static: '192.168.0.7',
                    mask_static: '255.255.255.0',
                    gw_static: '192.168.0.1',
                },
            } as never),
            initData: ref({
                wifi: undefined,
            } as never),
            isChanged: () => false,
            isLoading: ref(false),
            updateSettings: vi.fn(),
        } as never);

        const { default: Network } = await import('@/views/Network.vue');

        // Vue 3 routes watch errors through its internal error handler — they do NOT
        // propagate out of mount() or flushPromises(). Use errorHandler to capture them.
        const vueErrors: unknown[] = [];

        const wrapper = mount(Network, {
            global: {
                plugins: [i18n, makeRouter()],
                config: {
                    errorHandler: (err: unknown) => {
                        vueErrors.push(err);
                    },
                },
            },
        });

        await flushPromises();
        wrapper.unmount();

        expect(vueErrors).toHaveLength(0);
    });
});
