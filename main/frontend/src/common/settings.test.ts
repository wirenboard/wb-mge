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
    update_channel: 'stable',
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
        // doMock registrations are NOT undone by resetModules(): they outlive the module registry.
        // Without these, a test that stubs api() keeps stubbing it for the tests below that need
        // the real api() running under a ky stub — and those would silently assert nothing.
        vi.doUnmock('@/utils/api');
        vi.doUnmock('ky');
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
        // Partial mock: only api() is replaced. ApiError has to stay real — settings.ts
        // narrows the POST rejection with `instanceof`, and a mock without it turns any
        // rejection into a TypeError that would quietly pass tests asserting a failure.
        vi.doMock('@/utils/api', async (importOriginal) => ({
            ...(await importOriginal<typeof import('@/utils/api')>()),
            api: apiMock,
        }));

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
        // Partial mock: only api() is replaced. ApiError has to stay real — settings.ts
        // narrows the POST rejection with `instanceof`, and a mock without it turns any
        // rejection into a TypeError that would quietly pass tests asserting a failure.
        vi.doMock('@/utils/api', async (importOriginal) => ({
            ...(await importOriginal<typeof import('@/utils/api')>()),
            api: apiMock,
        }));

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
        // Partial mock: only api() is replaced. ApiError has to stay real — settings.ts
        // narrows the POST rejection with `instanceof`, and a mock without it turns any
        // rejection into a TypeError that would quietly pass tests asserting a failure.
        vi.doMock('@/utils/api', async (importOriginal) => ({
            ...(await importOriginal<typeof import('@/utils/api')>()),
            api: apiMock,
        }));

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
        // Partial mock: only api() is replaced. ApiError has to stay real — settings.ts
        // narrows the POST rejection with `instanceof`, and a mock without it turns any
        // rejection into a TypeError that would quietly pass tests asserting a failure.
        vi.doMock('@/utils/api', async (importOriginal) => ({
            ...(await importOriginal<typeof import('@/utils/api')>()),
            api: apiMock,
        }));

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

    // POST /settings answers 2xx with a "warnings" array when it ACCEPTED the write but the
    // resulting configuration has a problem the user must know about — today: an inherited TCP
    // port collision, where one of the two listeners will not bind. A green "data updated" toast
    // would hide that; the only other trace is a line in the firmware log.
    const setupWarningTest = (warnings: unknown) => {
        const showAlertMock = vi.fn();
        vi.doMock('@/common/hostname', () => ({ setHostname: vi.fn() }));
        vi.doMock('@/common/alert', () => ({ useAlerts: () => ({ showAlert: showAlertMock }) }));

        const apiMock = vi.fn().mockImplementation((_url: string, options?: { method?: string }) => {
            if (options?.method === 'POST') {
                return Promise.resolve(warnings === undefined ? { success: true } : { success: true, warnings });
            }
            return Promise.resolve(makeInitialSettings());
        });
        // Partial mock: only api() is replaced. ApiError has to stay real — settings.ts
        // narrows the POST rejection with `instanceof`, and a mock without it turns any
        // rejection into a TypeError that would quietly pass tests asserting a failure.
        vi.doMock('@/utils/api', async (importOriginal) => ({
            ...(await importOriginal<typeof import('@/utils/api')>()),
            api: apiMock,
        }));

        return showAlertMock;
    };

    it('ST-005: a warning in the response is shown as a warning alert instead of the success alert', async () => {
        const showAlertMock = setupWarningTest([
            {
                code: 'port_collision',
                message: 'web_port and rs485_1 are both configured on TCP port 80 in the saved configuration',
            },
        ]);

        const { useSettings } = await import('@/common/settings');
        const { updateSettings, refresh } = useSettings();

        await refresh();
        await updateSettings({ web_port: 502 } as any);

        // The known code is translated, not shown as the firmware's raw English text.
        expect(showAlertMock).toHaveBeenCalledWith(
            'warning_port_collision',
            expect.objectContaining({ type: 'warning' }),
        );
        // And the green "saved" toast must NOT appear: it would read as a clean save.
        expect(showAlertMock).not.toHaveBeenCalledWith('data_updated', expect.anything());
    });

    it('ST-006: an unknown warning code falls back to the message the firmware sent', async () => {
        const message = 'something the UI has never heard of';
        const showAlertMock = setupWarningTest([{ code: 'from_a_newer_firmware', message }]);

        const { useSettings } = await import('@/common/settings');
        const { updateSettings, refresh } = useSettings();

        await refresh();
        await updateSettings({ web_port: 502 } as any);

        expect(showAlertMock).toHaveBeenCalledWith(message, expect.objectContaining({ type: 'warning' }));
    });

    it('ST-007: every warning gets its own alert', async () => {
        const showAlertMock = setupWarningTest([
            { code: 'port_collision', message: 'first' },
            { code: 'another_code', message: 'second' },
        ]);

        const { useSettings } = await import('@/common/settings');
        const { updateSettings, refresh } = useSettings();

        await refresh();
        await updateSettings({ web_port: 502 } as any);

        expect(showAlertMock).toHaveBeenCalledTimes(2);
        expect(showAlertMock).toHaveBeenCalledWith('warning_port_collision', expect.objectContaining({ type: 'warning' }));
        expect(showAlertMock).toHaveBeenCalledWith('second', expect.objectContaining({ type: 'warning' }));
    });

    it('ST-008: a response without warnings still shows the success alert', async () => {
        const showAlertMock = setupWarningTest(undefined);

        const { useSettings } = await import('@/common/settings');
        const { updateSettings, refresh } = useSettings();

        await refresh();
        await updateSettings({ web_port: 502 } as any);

        expect(showAlertMock).toHaveBeenCalledWith('data_updated', { type: 'success' });
    });

    it('ST-009: an empty warnings array is not a warning — the save was clean', async () => {
        const showAlertMock = setupWarningTest([]);

        const { useSettings } = await import('@/common/settings');
        const { updateSettings, refresh } = useSettings();

        await refresh();
        await updateSettings({ web_port: 502 } as any);

        expect(showAlertMock).toHaveBeenCalledWith('data_updated', { type: 'success' });
    });

    // updateSettings() re-reads /settings after the POST, and the two ways that pair can fail need
    // different words from the caller: a refused write must roll the input back, an accepted write
    // whose read-back failed must not — the value IS on the device. The distinction is carried by
    // the type of the rejection, so it is asserted through the real module, not a stub of it.
    const setupRefreshTest = (postFails: Error | null, readBackError: Error) => {
        vi.doMock('@/common/hostname', () => ({ setHostname: vi.fn() }));
        vi.doMock('@/common/alert', () => ({ useAlerts: () => ({ showAlert: vi.fn() }) }));

        // The first GET (refresh()) has to succeed: the failure under test is the one that follows
        // the POST, and without initial data there would be nothing to save.
        let readBackFails = false;
        const apiMock = vi.fn().mockImplementation((_url: string, options?: { method?: string }) => {
            if (options?.method === 'POST') {
                return postFails ? Promise.reject(postFails) : Promise.resolve({ success: true });
            }
            return readBackFails ? Promise.reject(readBackError) : Promise.resolve(makeInitialSettings());
        });
        // Partial mock: only api() is replaced. ApiError has to stay real — settings.ts
        // narrows the POST rejection with `instanceof`, and a mock without it turns any
        // rejection into a TypeError that would quietly pass tests asserting a failure.
        vi.doMock('@/utils/api', async (importOriginal) => ({
            ...(await importOriginal<typeof import('@/utils/api')>()),
            api: apiMock,
        }));

        const armReadBackFailure = () => {
            readBackFails = true;
        };

        return { armReadBackFailure };
    };

    it('ST-010: a POST the device accepted whose read-back failed rejects with SettingsRefreshError', async () => {
        const readBackError = new Error('connection_error');
        const { armReadBackFailure } = setupRefreshTest(null, readBackError);

        const { SettingsRefreshError, useSettings } = await import('@/common/settings');
        const { updateSettings, refresh } = useSettings();

        await refresh();
        armReadBackFailure();

        const rejection = await updateSettings({ update_channel: 'testing' } as any).catch((err) => err);

        expect(rejection).toBeInstanceOf(SettingsRefreshError);
        // The original rejection is kept as the cause: without it the stack and the response object
        // are gone, and a report about a failed read-back has nothing in it but a message string.
        expect((rejection as Error).cause).toBe(readBackError);
    });

    it('ST-011: when the POST itself failed the caller sees the POST error, not a refresh error', async () => {
        const postError = new Error('device refused the setting');
        // The re-read fails too — it almost always does, for the same reason the POST did. The POST
        // rejection is still the one that must come out: it is what makes the caller roll back.
        const { armReadBackFailure } = setupRefreshTest(postError, new Error('connection_error'));

        const { SettingsRefreshError, useSettings } = await import('@/common/settings');
        const { updateSettings, refresh } = useSettings();

        await refresh();
        armReadBackFailure();

        const rejection = await updateSettings({ update_channel: 'testing' } as any).catch((err) => err);

        expect(rejection).toBe(postError);
        expect(rejection).not.toBeInstanceOf(SettingsRefreshError);
    });

    // A refused write comes back as HTTP 200 with {"success": false, "error": "..."} — the firmware
    // reserves its status codes for malformed requests, so a rejected VALUE is a 200. ky resolves
    // that like any other 200, which is why the refusal is only visible if the response BODY is
    // inspected. These tests therefore stub ky and run the real api() underneath the real
    // updateSettings(): stubbing api() would only prove that a stub can reject.
    const setupDeviceResponseTest = (postBody: unknown) => {
        const showAlertMock = vi.fn();
        vi.doMock('@/common/hostname', () => ({ setHostname: vi.fn() }));
        vi.doMock('@/common/alert', () => ({ useAlerts: () => ({ showAlert: showAlertMock }) }));
        vi.doMock('ky', () => ({
            default: vi.fn((_url: string, options?: { method?: string }) => ({
                json: () => Promise.resolve(options?.method === 'POST' ? postBody : makeInitialSettings()),
            })),
        }));

        return showAlertMock;
    };

    it('ST-012: a write the device refused with HTTP 200 alerts the error instead of "data updated"', async () => {
        const showAlertMock = setupDeviceResponseTest({ success: false, error: 'Invalid settings value' });

        const { useSettings } = await import('@/common/settings');
        const { updateSettings, refresh } = useSettings();

        await refresh();
        await updateSettings({ wifi: { ap_ip_static: '999.1.1.1' } } as any).catch(() => undefined);

        // The known firmware string is translated, the same way a known warning code is.
        expect(showAlertMock).toHaveBeenCalledWith('error_invalid_settings_value', { type: 'error' });
        // And the green toast must NOT appear — that is the whole bug: nothing was saved.
        expect(showAlertMock).not.toHaveBeenCalledWith('data_updated', expect.anything());
    });

    it('ST-013: a refused write rejects so the caller can roll its input back', async () => {
        setupDeviceResponseTest({ success: false, error: 'Invalid settings value' });

        const { SettingsRefreshError, useSettings } = await import('@/common/settings');
        const { ApiError } = await import('@/utils/api');
        const { updateSettings, refresh } = useSettings();

        await refresh();
        const rejection = await updateSettings({ wifi: { ap_ip_static: '999.1.1.1' } } as any).catch((err) => err);

        expect(rejection).toBeInstanceOf(ApiError);
        // "The device refused it" and "it was saved but the re-read failed" need opposite reactions
        // from the caller, so a refusal must never arrive wearing the refresh error's type.
        expect(rejection).not.toBeInstanceOf(SettingsRefreshError);
    });

    it('ST-014: a refusal the UI has no wording for falls back to the generic message', async () => {
        const showAlertMock = setupDeviceResponseTest({ success: false, error: 'Failed to save WiFi settings' });

        const { useSettings } = await import('@/common/settings');
        const { updateSettings, refresh } = useSettings();

        await refresh();
        await updateSettings({ wifi: { ap_ssid: 'x' } } as any).catch(() => undefined);

        expect(showAlertMock).toHaveBeenCalledWith('settings_rejected', { type: 'error' });
        expect(showAlertMock).not.toHaveBeenCalledWith('data_updated', expect.anything());
    });

    it('ST-015: a clean save through the real api() still shows the success alert', async () => {
        const showAlertMock = setupDeviceResponseTest({ success: true });

        const { useSettings } = await import('@/common/settings');
        const { updateSettings, refresh } = useSettings();

        await refresh();
        await updateSettings({ web_port: 502 } as any);

        expect(showAlertMock).toHaveBeenCalledWith('data_updated', { type: 'success' });
    });

    it('ST-016: warnings through the real api() are still a warning, not a refusal', async () => {
        // success:true WITH warnings means the write was accepted. The envelope check must not
        // touch it: turning this into an error would tell the user to undo a change that is live.
        const showAlertMock = setupDeviceResponseTest({
            success: true,
            warnings: [{ code: 'port_collision', message: 'web_port and rs485_1 share TCP port 80' }],
        });

        const { useSettings } = await import('@/common/settings');
        const { updateSettings, refresh } = useSettings();

        await refresh();
        await updateSettings({ web_port: 502 } as any);

        expect(showAlertMock).toHaveBeenCalledWith(
            'warning_port_collision',
            expect.objectContaining({ type: 'warning' }),
        );
        expect(showAlertMock).not.toHaveBeenCalledWith('settings_rejected', expect.anything());
        expect(showAlertMock).not.toHaveBeenCalledWith('data_updated', expect.anything());
    });
});
