/**
 * Integration tests for RsStatus.vue — the per-port status block.
 *
 * RSS-I-001 — every PortMode value renders a translated label in every supported locale.
 *             The template falls back to the raw enum value (t(`port_mode_${mode}`, mode)),
 *             so a missing key is invisible except that the raw wire value leaks into the UI:
 *             'repeater' was missing from all five locale blocks and printed literally.
 *             The component is mounted with an EMPTY global message table on purpose, so the
 *             labels can only come from RsStatus.vue's own <i18n> block.
 * RSS-I-002 — the same port modes are also listed by Sidebar.vue; the two <i18n> blocks must
 *             use identical wording, otherwise one port shows two different names for one mode.
 */

import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { describe, it, expect } from 'vitest';
import { mount } from '@vue/test-utils';
import { createI18n } from 'vue-i18n';
import type { PortMode, RsSettings, RsStatus as RsStatusType } from '@/common/types';
import RsStatus from '@/components/RsStatus.vue';

type Locale = 'en' | 'ru' | 'kk' | 'it' | 'de';
const LOCALES: Locale[] = ['en', 'ru', 'kk', 'it', 'de'];
const MODES: PortMode[] = ['disabled', 'tcp_bridge', 'passive', 'repeater'];

/** Expected label per mode per locale — the strings the five locale blocks must carry. */
const EXPECTED: Record<Locale, Record<PortMode, string>> = {
  en: { disabled: 'Disabled', tcp_bridge: 'TCP bridge', passive: 'Passive listen', repeater: 'Repeater' },
  ru: { disabled: 'Отключён', tcp_bridge: 'TCP-мост', passive: 'Пассивный (прослушка)', repeater: 'Повторитель' },
  kk: { disabled: 'Өшірілген', tcp_bridge: 'TCP көпір', passive: 'Пассивті тыңдау', repeater: 'Қайталағыш' },
  it: { disabled: 'Disabilitato', tcp_bridge: 'Bridge TCP', passive: 'Ascolto passivo', repeater: 'Ripetitore' },
  de: { disabled: 'Deaktiviert', tcp_bridge: 'TCP-Bridge', passive: 'Passives Mithören', repeater: 'Repeater' },
};

function makeInfo(mode: PortMode): RsStatusType {
  return {
    is_busy: false,
    error_percentage: 0,
    server_connections_count: 0,
    port_mode: mode,
    cache_enabled: false,
  };
}

function makeSettings(): RsSettings {
  return {
    term: false,
    fail_safe: false,
    tx_disabled: false,
    baudrate: 9600,
    stopbits: '1',
    parity: 'none',
    bridge: { modbus: true, mode: 'server', port: 502, host: '', enabled: true },
  } as unknown as RsSettings;
}

function mountWith(locale: Locale, mode: PortMode) {
  // Empty global messages: whatever renders must come from the component's own <i18n> block.
  const i18n = createI18n({ legacy: false, locale, fallbackLocale: 'en', messages: {} });
  return mount(RsStatus, {
    global: { plugins: [i18n], stubs: { InfoRow: { template: '<div><slot /></div>' } } },
    props: { title: 'RS-485-1', info: makeInfo(mode), settings: makeSettings() },
  });
}

describe('RSS-I-001: port operating mode is translated in every locale', () => {
  for (const locale of LOCALES) {
    for (const mode of MODES) {
      it(`${locale}/${mode} renders "${EXPECTED[locale][mode]}" and never the raw enum value`, () => {
        const text = mountWith(locale, mode).text();
        expect(text).toContain(EXPECTED[locale][mode]);
        // The raw wire value must not leak through the t() fallback. 'repeater' is the one
        // that used to: its key was missing from all five RsStatus locale blocks.
        expect(text).not.toContain(mode);
      });
    }
  }
});

describe('RSS-I-002: port mode wording agrees with Sidebar.vue', () => {
  /**
   * Read the <i18n> custom block out of a .vue source file.
   * Compiled component internals are not part of vue-i18n's public API, so the source of
   * truth is compared directly — that is also exactly what a reviewer would diff.
   */
  function localeBlock(vueFile: string): Record<Locale, Record<string, string>> {
    const src = readFileSync(fileURLToPath(new URL(vueFile, import.meta.url)), 'utf-8');
    const block = /<i18n>([\s\S]*?)<\/i18n>/.exec(src);
    expect(block, `${vueFile} must have an <i18n> block`).not.toBeNull();
    return JSON.parse(block![1]);
  }

  const status = localeBlock('./RsStatus.vue');
  const sidebar = localeBlock('./Sidebar.vue');

  for (const locale of LOCALES) {
    for (const mode of MODES) {
      it(`${locale}/${mode}: both components use "${EXPECTED[locale][mode]}"`, () => {
        expect(sidebar[locale][`port_mode_${mode}`]).toBe(EXPECTED[locale][mode]);
        expect(status[locale][`port_mode_${mode}`]).toBe(EXPECTED[locale][mode]);
      });
    }
  }
});
