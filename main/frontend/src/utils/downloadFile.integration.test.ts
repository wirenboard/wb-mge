import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { downloadFile, exportFileName, exportTimestamp } from './downloadFile';

describe('downloadFile', () => {
  // Reference to the captured anchor element created during the test
  let capturedAnchor: HTMLAnchorElement | null = null;

  beforeEach(() => {
    capturedAnchor = null;

    // Spy on URL.createObjectURL and revokeObjectURL without replacing the whole URL global
    vi.spyOn(URL, 'createObjectURL').mockReturnValue('blob:test-url');
    vi.spyOn(URL, 'revokeObjectURL').mockImplementation(() => {});

    // Save the original createElement before installing the spy
    const originalCreateElement = document.createElement.bind(document);

    // Intercept createElement to capture the anchor and spy on its click method
    vi.spyOn(document, 'createElement').mockImplementation((tag: string) => {
      const el = originalCreateElement(tag);
      if (tag === 'a') {
        capturedAnchor = el as HTMLAnchorElement;
        vi.spyOn(capturedAnchor, 'click');
      }
      return el;
    });
  });

  afterEach(() => {
    vi.restoreAllMocks();
    document.body.innerHTML = ''; // clean up any leftover DOM nodes between tests
  });

  // DF-U-01: Blob input → full DOM flow executed correctly
  it('DF-U-01: Blob input triggers correct DOM flow', () => {
    downloadFile('test.blob', new Blob(['hello'], { type: 'text/plain' }));

    // createObjectURL must be called exactly once with a Blob argument
    expect(URL.createObjectURL).toHaveBeenCalledTimes(1);
    expect(URL.createObjectURL).toHaveBeenCalledWith(expect.any(Blob));

    // Anchor must have been captured and click must have been called once
    expect(capturedAnchor).not.toBeNull();
    expect(capturedAnchor!.click).toHaveBeenCalledTimes(1);

    // download attribute must match the fileName argument
    expect(capturedAnchor!.download).toBe('test.blob');

    // revokeObjectURL must be called with the URL that createObjectURL returned
    expect(URL.revokeObjectURL).toHaveBeenCalledWith('blob:test-url');
  });

  // DF-U-02: File input (File extends Blob) → both paths work correctly
  it('DF-U-02: File input uses the provided fileName, not the File object name', () => {
    downloadFile(
      'test.txt',
      new File(['content'], 'original.txt', { type: 'text/plain' }),
    );

    // createObjectURL must be called (File is also a Blob)
    expect(URL.createObjectURL).toHaveBeenCalledTimes(1);

    // download attribute must be the fileName argument, NOT the File's own name
    expect(capturedAnchor).not.toBeNull();
    expect(capturedAnchor!.download).toBe('test.txt');

    // click must have been called — verifies File input follows the same flow as Blob input
    expect(capturedAnchor!.click).toHaveBeenCalledTimes(1);

    // revokeObjectURL must be called — cleanup is mandatory
    expect(URL.revokeObjectURL).toHaveBeenCalledTimes(1);
  });

  // DF-U-03: revokeObjectURL is called with the exact URL that createObjectURL returned
  it('DF-U-03: revokeObjectURL receives the same URL that createObjectURL returned', () => {
    // Use a unique URL to verify that the return value is forwarded, not a hardcoded constant
    vi.mocked(URL.createObjectURL).mockReturnValueOnce('blob:unique-url-for-this-test');
    downloadFile('report.csv', new Blob(['a,b,c']));

    expect(capturedAnchor).not.toBeNull();
    expect(URL.revokeObjectURL).toHaveBeenCalledWith('blob:unique-url-for-this-test');
  });

  // DF-U-04: anchor is removed from the DOM after the call (no DOM leak)
  it('DF-U-04: anchor element is not left in the DOM after download', () => {
    const initialChildCount = document.body.children.length;

    downloadFile('data.json', new Blob(['{}'], { type: 'application/json' }));

    // The number of children must be restored to the initial value
    expect(document.body.children.length).toBe(initialChildCount);

    // The captured anchor must not be present in document.body
    expect(capturedAnchor).not.toBeNull();
    expect(document.body.contains(capturedAnchor)).toBe(false);
  });
});

/**
 * The export naming scheme is the single place where every downloadable file gets its name;
 * these tests pin the shape the three call sites (settings, register map, sniffer) rely on.
 */
describe('exportFileName / exportTimestamp', () => {
  // Fixed local-time instant used by the deterministic cases below.
  const instant = new Date(2026, 7, 5, 9, 4, 3); // 2026-08-05 09:04:03 local

  // DF-U-05: the timestamp is local wall clock, zero-padded, and colon-free.
  it('DF-U-05: exportTimestamp is local time in colon-free YYYY-MM-DDTHH-mm-ss form', () => {
    expect(exportTimestamp(instant)).toBe('2026-08-05T09-04-03');
    // A colon is illegal in a Windows file name — it must never appear.
    expect(exportTimestamp(instant)).not.toContain(':');
  });

  // DF-U-06: full name follows wb-mge-<what>-<timestamp>.<ext>.
  it('DF-U-06: exportFileName builds wb-mge-<what>-<timestamp>.<ext>', () => {
    expect(exportFileName('settings', 'json', instant)).toBe('wb-mge-settings-2026-08-05T09-04-03.json');
    expect(exportFileName('register-map', 'json', instant)).toBe('wb-mge-register-map-2026-08-05T09-04-03.json');
    expect(exportFileName('sniffer-port2', 'csv', instant)).toBe('wb-mge-sniffer-port2-2026-08-05T09-04-03.csv');
  });

  // DF-U-07: with no explicit instant the name still matches the scheme.
  it('DF-U-07: default (now) argument still produces a scheme-conforming name', () => {
    expect(exportFileName('settings', 'json'))
      .toMatch(/^wb-mge-settings-\d{4}-\d{2}-\d{2}T\d{2}-\d{2}-\d{2}\.json$/);
  });
});
