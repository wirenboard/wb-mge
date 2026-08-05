/**
 * Integration tests for DeviceRegisterMapPopup.vue.
 *
 * DRM-I-001 — one table for both function codes:
 *   FC03 and FC04 read the same address space, so the popup must show a SINGLE register table
 *   ordered by address. The firmware signature (290–301) used to live in a separate
 *   "Holding registers" table, which read as a second, differently-addressed space; it now sits
 *   in the merged table between 268–271 and 320, and no second table may render. The heading
 *   must name both function codes rather than only FC04/input.
 */

import { describe, it, expect, afterEach } from 'vitest';
import { mount } from '@vue/test-utils';
import { createI18n } from 'vue-i18n';
import DeviceRegisterMapPopup from '@/components/DeviceRegisterMapPopup.vue';

/**
 * The popup teleports into <body> and opens a native <dialog>, so its markup is NOT inside
 * wrapper.element — every assertion has to read document.body instead.
 */
function mountPopup(locale: 'en' | 'ru' = 'en') {
  const i18n = createI18n({ legacy: false, locale, messages: { en: {}, ru: {} } });
  return mount(DeviceRegisterMapPopup, { global: { plugins: [i18n] } });
}

/** Section titles, in document order — the popup renders them as plain divs, not <h*>. */
function sectionTitles(): string[] {
  return Array.from(document.body.querySelectorAll('.drm-section-title')).map(
    el => el.textContent?.trim() ?? '',
  );
}

/** First-column (decimal address) text of every row of the given table. */
function decColumn(table: Element): string[] {
  return Array.from(table.querySelectorAll('tbody tr')).map(
    tr => tr.querySelector('td')?.textContent?.trim() ?? '',
  );
}

describe('DRM-I-001: one register table covers both FC03 and FC04', () => {
  afterEach(() => {
    document.body.innerHTML = '';
  });

  it('renders exactly one register table', () => {
    const wrapper = mountPopup();

    expect(document.body.querySelectorAll('table.drm-table')).toHaveLength(1);

    wrapper.unmount();
  });

  it('places the firmware signature row by address, between 268–271 and 320', () => {
    const wrapper = mountPopup();

    const table = document.body.querySelector('table.drm-table');
    expect(table, 'the register table must be rendered').not.toBeNull();
    const addresses = decColumn(table!);

    const signature = addresses.indexOf('290–301');
    expect(signature, 'the signature row must be in the merged table').toBeGreaterThan(-1);
    expect(addresses[signature - 1]).toBe('268–271');
    expect(addresses[signature + 1]).toBe('320');

    const signatureRow = table!.querySelectorAll('tbody tr')[signature];
    expect(signatureRow.textContent).toContain('0x0122–0x012D');
    expect(signatureRow.textContent).toContain('Firmware signature');

    wrapper.unmount();
  });

  it('heads the table with both function codes and keeps no holding section', () => {
    const wrapper = mountPopup();

    expect(sectionTitles()).toEqual(['Registers (FC03/FC04, read-only)', 'Notes']);

    wrapper.unmount();
  });

  it('heads the table with both function codes in Russian too', () => {
    const wrapper = mountPopup('ru');

    expect(sectionTitles()).toEqual(['Регистры (FC03/FC04, только чтение)', 'Примечания']);

    wrapper.unmount();
  });

  it('the intro no longer splits the read functions into input and holding', () => {
    const wrapper = mountPopup();

    const intro = document.body.querySelector('.drm-intro')?.textContent ?? '';
    expect(intro).toContain('Read functions FC03 and FC04 are supported');

    wrapper.unmount();
  });
});
