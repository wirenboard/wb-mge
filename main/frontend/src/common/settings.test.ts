import { beforeEach, describe, expect, it, vi } from 'vitest';

// Factory for minimal valid Settings objects used across all tests.
// Returns a new deep copy each call to avoid shared state between tests.
const makeInitialSettings = () => ({
    hostname: 'wb-mge',
    login: 'admin',
    pass: '',
    web_port: 80,
    io_bus: false,
    vout: true,
    cache_modbus_port: 502,
    cache_modbus_server_enabled: false,
    cache_value_timeout_s: 10,
    ethernet: { dhcpc: true, ip_static: '', mask_static: '', gw_static: '' },
    rs485_1: {
        baudrate: 9600,
        parity: 'none',
        stopbits: '1',
        databits: '8',
        term: false,
        fail_safe: false,
        tx_disabled: false,
        bridge: { mode: 'server', ip: '', port: 502, modbus: true },
    },
    rs485_2: {
        baudrate: 9600,
        parity: 'none',
        stopbits: '1',
        databits: '8',
        term: false,
        fail_safe: false,
        tx_disabled: false,
        bridge: { mode: 'server', ip: '', port: 503, modbus: true },
    },
});

describe('useSettings', () => {
    // Reset module registry before each test so that module-level refs
    // (data, initData) are re-created fresh and cannot bleed state between tests.
    beforeEach(() => {
        vi.resetModules();
    });

    it('ST-001: saving one card does not reset dirty state in adjacent cards', async () => {
        const setHostnameMock = vi.fn();
        vi.doMock('@/common/hostname', () => ({ setHostname: setHostnameMock }));
        vi.doMock('@/common/alert', () => ({ useAlerts: () => ({ showAlert: vi.fn() }) }));

        // Settings returned by the server after saving rs485_1 (baudrate updated to 19200).
        // rs485_2 is unchanged on the server — parity is still 'none'.
        const serverSettingsAfterSave = {
            ...makeInitialSettings(),
            rs485_1: { ...makeInitialSettings().rs485_1, baudrate: 19200 },
        };

        // Default: every GET returns post-save server state; POST returns success.
        const apiMock = vi.fn().mockImplementation((_url: string, options?: { method?: string }) => {
            if (options?.method === 'POST') {
                return Promise.resolve({ success: true });
            }
            return Promise.resolve(serverSettingsAfterSave);
        });
        // First GET (called by refresh()) must return the initial state (baudrate=9600).
        apiMock.mockImplementationOnce(() => Promise.resolve(makeInitialSettings()));
        vi.doMock('@/utils/api', () => ({ api: apiMock }));

        const { useSettings } = await import('@/common/settings');
        const { data, isChanged, updateSettings, refresh } = useSettings();

        // Step 1: load initial settings from the server.
        await refresh();
        expect(data.value!.rs485_1.baudrate).toBe(9600);
        expect(data.value!.rs485_2.parity).toBe('none');

        // Step 2: simulate a user edit in Port 2 — change parity to 'even'.
        data.value!.rs485_2.parity = 'even';

        // Step 3: the Save button for Port 2 should now be active.
        expect(isChanged(['rs485_2'])).toBe(true);

        // Step 4: user saves Port 1 (baudrate changed to 19200).
        // POST returns success; subsequent GET (partialRefresh) returns serverSettingsAfterSave.
        await updateSettings({ rs485_1: { ...data.value!.rs485_1, baudrate: 19200 } } as any);

        // Server value for the saved key must be applied to data.
        expect(data.value!.rs485_1.baudrate).toBe(19200); // server value applied to saved key

        // Step 5: dirty user edit on Port 2 must NOT have been overwritten by partialRefresh.
        expect(data.value!.rs485_2.parity).toBe('even');

        // Step 6: Save button for Port 2 must still be active.
        expect(isChanged(['rs485_2'])).toBe(true);

        // Step 7: Save button for Port 1 must now be disabled (server value matches initData).
        expect(isChanged(['rs485_1'])).toBe(false);
    });

    it('ST-002: partialRefresh stores a deep clone so data and initData do not share objects', async () => {
        const setHostnameMock = vi.fn();
        vi.doMock('@/common/hostname', () => ({ setHostname: setHostnameMock }));
        vi.doMock('@/common/alert', () => ({ useAlerts: () => ({ showAlert: vi.fn() }) }));

        // Use a factory-based mock so every GET returns a fresh object, preventing shared
        // reference corruption when the test later mutates data.value!.rs485_1.baudrate.
        const apiMock = vi.fn().mockImplementation((_url: string, options?: { method?: string }) => {
            if (options?.method === 'POST') {
                return Promise.resolve({ success: true });
            }
            // Return a fresh copy on every call to avoid shared reference corruption.
            return Promise.resolve({ ...makeInitialSettings(), rs485_1: { ...makeInitialSettings().rs485_1, baudrate: 19200 } });
        });
        // First GET returns initial state (baudrate=9600).
        apiMock.mockImplementationOnce(() => Promise.resolve(makeInitialSettings()));
        vi.doMock('@/utils/api', () => ({ api: apiMock }));

        const { useSettings } = await import('@/common/settings');
        const { data, initData, updateSettings, refresh } = useSettings();

        // Step 1: load initial settings, then save Port 1 with baudrate=19200.
        await refresh();

        // Capture rs485_2 reference before updateSettings — partialRefresh must NOT replace it
        // because rs485_2 was NOT in savedKeys. A full refresh() would replace it with a new object.
        const originalRs485_2 = data.value!.rs485_2;

        await updateSettings({ rs485_1: { ...data.value!.rs485_1, baudrate: 19200 } } as any);

        // After partialRefresh, initData.rs485_1.baudrate should reflect the server value.
        expect(initData.value!.rs485_1.baudrate).toBe(19200);

        // rs485_2 was NOT in savedKeys, so partialRefresh must not have replaced its object reference.
        // This assertion fails if partialRefresh is replaced with a full refresh().
        expect(data.value!.rs485_2).toBe(originalRs485_2);

        // Step 2: mutate data directly — simulates a user typing a new value.
        data.value!.rs485_1.baudrate = 57600;

        // Step 3: initData must not be affected because it holds a deep clone, not a reference.
        expect(initData.value!.rs485_1.baudrate).toBe(19200);
    });

    it('ST-003: partialRefresh calls setHostname when hostname is in savedKeys', async () => {
        const setHostnameMock = vi.fn();
        vi.doMock('@/common/hostname', () => ({ setHostname: setHostnameMock }));
        vi.doMock('@/common/alert', () => ({ useAlerts: () => ({ showAlert: vi.fn() }) }));

        const serverSettingsWithNewHostname = {
            ...makeInitialSettings(),
            hostname: 'new-host',
        };

        const apiMock = vi.fn().mockImplementation((_url: string, options?: { method?: string }) => {
            if (options?.method === 'POST') {
                return Promise.resolve({ success: true });
            }
            return Promise.resolve(serverSettingsWithNewHostname);
        });
        // First GET returns initial state (hostname='wb-mge').
        apiMock.mockImplementationOnce(() => Promise.resolve(makeInitialSettings()));
        vi.doMock('@/utils/api', () => ({ api: apiMock }));

        const { useSettings } = await import('@/common/settings');
        const { data, updateSettings, refresh } = useSettings();

        // Step 1: load initial settings.
        await refresh();

        // Clear the call record from refresh() so we can isolate calls from partialRefresh.
        setHostnameMock.mockClear();

        // Step 2: save the hostname field.
        await updateSettings({ hostname: 'new-host' } as any);

        // Step 3: setHostname must have been called with the new hostname value.
        expect(setHostnameMock).toHaveBeenCalledWith('new-host');

        // Also assert the data ref was updated correctly.
        expect(data.value!.hostname).toBe('new-host');
    });

    it('ST-004: partialRefresh does NOT call setHostname when hostname is not in savedKeys', async () => {
        const setHostnameMock = vi.fn();
        vi.doMock('@/common/hostname', () => ({ setHostname: setHostnameMock }));
        vi.doMock('@/common/alert', () => ({ useAlerts: () => ({ showAlert: vi.fn() }) }));

        const serverSettingsAfterSave = {
            ...makeInitialSettings(),
            rs485_1: { ...makeInitialSettings().rs485_1, baudrate: 19200 },
        };

        const apiMock = vi.fn().mockImplementation((_url: string, options?: { method?: string }) => {
            if (options?.method === 'POST') {
                return Promise.resolve({ success: true });
            }
            return Promise.resolve(serverSettingsAfterSave);
        });
        // First GET returns initial state.
        apiMock.mockImplementationOnce(() => Promise.resolve(makeInitialSettings()));
        vi.doMock('@/utils/api', () => ({ api: apiMock }));

        const { useSettings } = await import('@/common/settings');
        const { updateSettings, refresh } = useSettings();

        // Step 1: load initial settings; setHostname is called once here.
        await refresh();

        // Clear the call record from refresh() so counts below only reflect partialRefresh.
        setHostnameMock.mockClear();

        // Step 2: save Port 1 — hostname is NOT among the saved keys.
        await updateSettings({ rs485_1: { ...makeInitialSettings().rs485_1, baudrate: 19200 } } as any);

        // Step 3: partialRefresh must not have called setHostname because hostname was not saved.
        expect(setHostnameMock).not.toHaveBeenCalled();
    });
});
