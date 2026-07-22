/**
 * Integration tests for Mqtt.vue — MGU RS485-2 port restriction.
 *
 * MQ-I-001 — MGU board: RS485-2 option is absent from the port select
 * MQ-I-002 — MGU board: a stored mqts.port === 2 is coerced to 1 after render
 * MQ-I-003 — MGE board: RS485-2 option is present and mqts.port is untouched
 */

import { describe, it, expect, vi } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
import { ref, shallowRef } from 'vue';
import { createI18n } from 'vue-i18n';
import { createRouter, createMemoryHistory, matchedRouteKey } from 'vue-router';
import { messages } from '@/i18n/messages';

// Mocks — hoisted before any component import by Vitest's vi.mock hoisting.

vi.mock('@/common/settings', () => ({
    useSettings: vi.fn(),
}));

vi.mock('@/common/info', () => ({
    useInfo: vi.fn(),
}));

vi.mock('@/utils/api', () => ({
    api: vi.fn().mockResolvedValue({}),
}));

// Stub @unhead/vue so Heading.vue's injectHead() / useHead() do not throw.
vi.mock('@unhead/vue', () => ({
    injectHead: () => ({}),
    useHead: vi.fn(),
    createUnhead: vi.fn(() => ({})),
    headSymbol: Symbol('head'),
}));

/** Build a minimal router required by Sidebar.vue (useRoute/useRouter). */
function makeRouter() {
    return createRouter({
        history: createMemoryHistory(),
        routes: [
            { path: '/', component: { template: '<div/>' } },
            { path: '/logout', component: { template: '<div/>' } },
            { path: '/mqtt', component: { template: '<div/>' } },
        ],
    });
}

/**
 * i18n instance with real messages and warn suppression to prevent fallback
 * warnings when Mqtt.vue looks up global keys that live in the root scope.
 */
const i18n = createI18n({ legacy: false, locale: 'en', messages, missingWarn: false, fallbackWarn: false });

/** Mount Mqtt.vue with the given useSettings/useInfo mock return values. */
async function mountMqtt(settingsReturn: unknown, infoReturn: unknown) {
    const { useSettings } = await import('@/common/settings');
    const { useInfo } = await import('@/common/info');
    vi.mocked(useSettings).mockReturnValue(settingsReturn as never);
    vi.mocked(useInfo).mockReturnValue(infoReturn as never);

    const { default: Mqtt } = await import('@/views/Mqtt.vue');
    const router = makeRouter();
    await router.push('/mqtt');
    await router.isReady();
    const wrapper = mount(Mqtt, {
        global: {
            plugins: [i18n, router],
            provide: { [matchedRouteKey as symbol]: shallowRef({ leaveGuards: new Set(), updateGuards: new Set() }) },
        },
    });
    await flushPromises();
    return wrapper;
}

function makeSettings(port: number) {
    return {
        data: ref({
            mqtt: { enabled: false, host: '', port: 1883, user: '', pass: '', prefix: '' },
            mqts: { enabled: true, port, slave_id: 1 },
        }),
        initData: ref(null),
        isChanged: () => false,
        isLoading: ref(false),
        updateSettings: vi.fn(),
    };
}

describe('MQ-I-001: MGU hides the RS485-2 option', () => {
    it('RS485-2 option is absent from the port select on WB-MGU', async () => {
        const settings = makeSettings(1);
        const wrapper = await mountMqtt(settings, { info: ref({ device_name: 'WB-MGU v.1' }) });

        const options = wrapper.findAll('#mqts_port option');
        expect(options).toHaveLength(1);
        expect(options[0].text()).toBe('RS485-1');

        wrapper.unmount();
    });
});

describe('MQ-I-002: MGU coerces a stored port 2 to 1', () => {
    it('mqts.port is coerced from 2 to 1 after render on WB-MGU', async () => {
        const settings = makeSettings(2);
        const wrapper = await mountMqtt(settings, { info: ref({ device_name: 'WB-MGU v.1' }) });

        expect(settings.data.value.mqts.port).toBe(1);

        wrapper.unmount();
    });
});

describe('MQ-I-003: MGE keeps the RS485-2 option', () => {
    it('RS485-2 option is present and mqts.port is untouched on WB-MGE', async () => {
        const settings = makeSettings(2);
        const wrapper = await mountMqtt(settings, { info: ref({ device_name: 'WB-MGE v.3' }) });

        const options = wrapper.findAll('#mqts_port option');
        expect(options).toHaveLength(2);
        expect(options[1].text()).toBe('RS485-2');
        expect(settings.data.value.mqts.port).toBe(2);

        wrapper.unmount();
    });
});
