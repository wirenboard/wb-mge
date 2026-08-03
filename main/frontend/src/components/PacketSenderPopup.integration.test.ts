/**
 * Integration tests for PacketSenderPopup.vue.
 *
 * PSP-I-001 — switching the function code keeps a still-valid value:
 *   The FC watcher exists so an incompatible leftover value (e.g. "10" carried into a coil FC
 *   that only accepts 0/1) cannot silently disable the send button. It used to reset the field
 *   unconditionally, so every FC change — and every read/write mode change, which sets the FC —
 *   threw away what the user had typed. It must now reset ONLY when the new FC cannot accept
 *   the current value.
 *
 * PSP-I-002 — sending waits for the capture:
 *   The sniffer records only what arrives while its WebSocket is open, so the frame must not be
 *   transmitted until the capture is established. The popup awaits the `ensureCapture` prop
 *   BEFORE calling the send endpoint, and announces an auto-start so the UI does not change
 *   state behind the user's back. It reports each of the three outcomes truthfully — in
 *   particular it must NOT claim an auto-start when the capture never came up — and a capture
 *   that cannot be established makes the frame go out unrecorded, never cancels it.
 */

import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
import { createI18n } from 'vue-i18n';
import PacketSenderPopup from '@/components/PacketSenderPopup.vue';
import { senderState } from '@/utils/senderState';
import type { CaptureReadyOutcome } from '@/utils/snifferUtils';

const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });

/** The hint shown when the frame went out with no capture recording it. */
const NOT_RUNNING_HINT = 'Sent without a running capture, so the packets will not appear in the list';

/** Mount the popup with an `ensureCapture` that reports "already running" and resolves at once. */
function mountPopup(
  ensureCapture: () => Promise<CaptureReadyOutcome> = () => Promise.resolve('already-running'),
) {
  return mount(PacketSenderPopup, {
    props: { portNum: '1', txDisabled: false, ensureCapture },
    global: { plugins: [i18n] },
  });
}

/** The form state is a module-level singleton (it survives popup close/open) — reset it. */
function resetSenderState() {
  senderState.mode = 'read';
  senderState.slaveId = '01';
  senderState.fc = '03';
  senderState.address = '0x0000';
  senderState.value = '10';
}

/** Set the FC via the real <select>, so the component's watcher runs exactly as in the UI. */
async function selectFc(wrapper: ReturnType<typeof mountPopup>, fc: string) {
  await wrapper.find('select.form-field-input').setValue(fc);
  await flushPromises();
}

/** Click the Read / Write segmented buttons. */
async function setMode(wrapper: ReturnType<typeof mountPopup>, label: 'Read' | 'Write') {
  const btn = wrapper.findAll('.sniffer-sender-seg-btn').find(b => b.text() === label);
  expect(btn, `${label} mode button must be found`).toBeDefined();
  await btn!.trigger('click');
  await flushPromises();
}

describe('PSP-I-001: switching the function code keeps a still-valid value', () => {
  beforeEach(() => {
    resetSenderState();
  });

  it('read mode FC03 -> FC04: a valid count survives the switch', async () => {
    const wrapper = mountPopup();
    senderState.value = '25';
    await flushPromises();

    await selectFc(wrapper, '04');

    expect(senderState.fc).toBe('04');
    expect(senderState.value).toBe('25');
    wrapper.unmount();
  });

  it('read mode FC03 -> FC04: an invalid count is replaced by the default', async () => {
    const wrapper = mountPopup();
    // A read count must be 1..2000; 5000 cannot build a frame for FC04 either.
    senderState.value = '5000';
    await flushPromises();

    await selectFc(wrapper, '04');

    expect(senderState.value).toBe('10');
    wrapper.unmount();
  });

  it('write mode FC06 -> FC05: a register value a coil cannot hold is reset to 1', async () => {
    const wrapper = mountPopup();
    await setMode(wrapper, 'Write');
    senderState.value = '300';
    await flushPromises();

    await selectFc(wrapper, '05');

    expect(senderState.fc).toBe('05');
    // Note: the coil FCs reset to '1', not to '10'.
    expect(senderState.value).toBe('1');
    wrapper.unmount();
  });

  it('write mode FC05 -> FC06: a 0/1 value a register accepts is kept (the other direction)', async () => {
    const wrapper = mountPopup();
    await setMode(wrapper, 'Write');
    await selectFc(wrapper, '05');
    senderState.value = '1';
    await flushPromises();

    await selectFc(wrapper, '06');

    expect(senderState.fc).toBe('06');
    expect(senderState.value).toBe('1');
    wrapper.unmount();
  });

  it('write mode FC16 -> FC15: a register list a coil list cannot hold is reset', async () => {
    const wrapper = mountPopup();
    await setMode(wrapper, 'Write');
    await selectFc(wrapper, '10');
    senderState.value = '10, 20, 30';
    await flushPromises();

    await selectFc(wrapper, '0f');

    expect(senderState.value).toBe('1');
    wrapper.unmount();
  });

  it('write mode FC15 -> FC16: a 0/1 list a register list accepts is kept', async () => {
    const wrapper = mountPopup();
    await setMode(wrapper, 'Write');
    await selectFc(wrapper, '0f');
    senderState.value = '1, 0, 1';
    await flushPromises();

    await selectFc(wrapper, '10');

    expect(senderState.fc).toBe('10');
    expect(senderState.value).toBe('1, 0, 1');
    wrapper.unmount();
  });

  it('read -> write mode keeps a value both sides accept (mode switch changes the FC too)', async () => {
    const wrapper = mountPopup();
    senderState.value = '25';
    await flushPromises();

    await setMode(wrapper, 'Write'); // FC03 -> FC06, 25 is a valid register value

    expect(senderState.fc).toBe('06');
    expect(senderState.value).toBe('25');
    wrapper.unmount();
  });

  it('write -> read mode still resets a value the new FC cannot accept', async () => {
    const wrapper = mountPopup();
    // A register value may be up to 65535, a read count only up to 2000, so 5000 is valid for
    // FC06 and invalid for FC03 — the mode switch must replace it.
    await setMode(wrapper, 'Write');
    senderState.value = '5000'; // valid FC06 register value
    await flushPromises();

    await setMode(wrapper, 'Read'); // FC06 -> FC03, count must be 1..2000

    expect(senderState.fc).toBe('03');
    expect(senderState.value).toBe('10');
    wrapper.unmount();
  });
});

describe('PSP-I-002: sending waits for the capture', () => {
  const fetchMock = vi.fn();

  beforeEach(() => {
    resetSenderState();
    fetchMock.mockReset();
    fetchMock.mockResolvedValue({ ok: true, json: async () => ({ sent: 8 }) });
    vi.stubGlobal('fetch', fetchMock);
  });

  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it('does not transmit until ensureCapture resolves, then announces the auto-start', async () => {
    let releaseCapture: ((outcome: CaptureReadyOutcome) => void) | null = null;
    const ensureCapture = vi.fn(
      () => new Promise<CaptureReadyOutcome>((resolve) => {
        releaseCapture = resolve;
      }),
    );

    const wrapper = mountPopup(ensureCapture);
    await wrapper.find('.sniffer-sender-foot-send').trigger('click');
    await flushPromises();

    // The capture was asked for, and NOTHING has gone out on the wire yet.
    expect(ensureCapture).toHaveBeenCalledTimes(1);
    expect(fetchMock).not.toHaveBeenCalled();

    // The capture is now established (and it was this send that started it).
    releaseCapture!('started');
    await flushPromises();

    expect(fetchMock).toHaveBeenCalledTimes(1);
    expect(fetchMock.mock.calls[0][0]).toBe('/ports/1/send');
    // The auto-start is visible in the popup hint, replacing the CRC hint.
    expect(wrapper.find('.sniffer-sender-hint').text()).toBe(
      'Capture started automatically so the packets appear in the list',
    );

    wrapper.unmount();
  });

  it('capture already running: sends normally and shows no auto-start notice', async () => {
    const ensureCapture = vi.fn((): Promise<CaptureReadyOutcome> => Promise.resolve('already-running'));

    const wrapper = mountPopup(ensureCapture);
    await wrapper.find('.sniffer-sender-foot-send').trigger('click');
    await flushPromises();

    expect(ensureCapture).toHaveBeenCalledTimes(1);
    expect(fetchMock).toHaveBeenCalledTimes(1);
    // The unchanged hint proves nothing claimed to have been started.
    expect(wrapper.find('.sniffer-sender-hint').text()).toBe('CRC computed automatically');

    wrapper.unmount();
  });

  it('capture never came up: the frame goes out and the hint says so — no "started" claim', async () => {
    const ensureCapture = vi.fn((): Promise<CaptureReadyOutcome> => Promise.resolve('not-established'));

    const wrapper = mountPopup(ensureCapture);
    await wrapper.find('.sniffer-sender-foot-send').trigger('click');
    await flushPromises();

    // The frame is still transmitted — a capture that cannot be established must not block a send.
    expect(fetchMock).toHaveBeenCalledTimes(1);
    // And the user is told the packets will NOT show up, instead of the opposite.
    expect(wrapper.find('.sniffer-sender-hint').text()).toBe(NOT_RUNNING_HINT);

    wrapper.unmount();
  });

  it('ensureCapture throwing degrades to sending unrecorded, it does not cancel the send', async () => {
    // `new WebSocket(url)` throws synchronously on a malformed URL / mixed content, so the prop
    // can reject. Sending a frame never depended on the sniffer socket and must not start now.
    const ensureCapture = vi.fn((): Promise<CaptureReadyOutcome> => Promise.reject(new Error('SecurityError')));

    const wrapper = mountPopup(ensureCapture);
    await wrapper.find('.sniffer-sender-foot-send').trigger('click');
    await flushPromises();

    expect(fetchMock).toHaveBeenCalledTimes(1);
    expect(fetchMock.mock.calls[0][0]).toBe('/ports/1/send');
    // The WebSocket failure is not reported as a send failure; the send itself succeeded.
    expect(wrapper.find('.sniffer-sender-error').exists()).toBe(false);
    expect(wrapper.find('.sniffer-sender-success').text()).toBe('Sent 8 bytes to port 1');
    expect(wrapper.find('.sniffer-sender-hint').text()).toBe(NOT_RUNNING_HINT);

    wrapper.unmount();
  });
});
