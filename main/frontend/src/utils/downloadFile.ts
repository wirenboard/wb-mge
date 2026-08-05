/**
 * Single source of truth for the names of every file this UI hands to the browser.
 *
 * Scheme: `wb-mge-<what>-<timestamp>.<ext>`
 *   <what>      kebab-case description of the payload, including the port number where the
 *               export is per-port (e.g. 'sniffer-port1').
 *   <timestamp> LOCAL wall clock as `YYYY-MM-DDTHH-mm-ss`.
 *
 * The timestamp deliberately avoids colons. `Date#toISOString()` produces `12:34:56`, and a
 * colon is illegal in a Windows file name — the browser silently rewrites it, so the file the
 * user ends up with is not the one we asked for. Local time (not UTC) is used because these
 * files are read next to the device's own UI, where the operator thinks in local time.
 *
 * The firmware serves one more export of its own, the Modbus cache CSV, whose name comes from
 * a Content-Disposition header (main/bridge/cache_multimaster.c). It follows the same scheme
 * without the timestamp: the device has no guaranteed wall clock (SNTP is optional and the
 * board has no RTC), so a timestamp there would be a lie more often than not.
 */
export function exportTimestamp(now: Date = new Date()): string {
  const pad = (n: number) => String(n).padStart(2, '0');
  return (
    `${now.getFullYear()}-${pad(now.getMonth() + 1)}-${pad(now.getDate())}` +
    `T${pad(now.getHours())}-${pad(now.getMinutes())}-${pad(now.getSeconds())}`
  );
}

/** Build an export file name following the scheme documented on exportTimestamp(). */
export function exportFileName(what: string, ext: string, now: Date = new Date()): string {
  return `wb-mge-${what}-${exportTimestamp(now)}.${ext}`;
}

/**
 * Triggers a browser file download for the given Blob or File.
 * Creates a temporary anchor element, appends it to the DOM for Firefox
 * compatibility, clicks it, and cleans up immediately after.
 * @param fileName - Suggested file name for the download
 * @param data    - Blob or File to download
 */
export const downloadFile = (fileName: string, data: Blob | File): void => {
  const href = URL.createObjectURL(data);
  const link = document.createElement('a');
  Object.assign(link, { href, download: fileName });
  document.body.appendChild(link);
  link.click();
  link.remove();
  URL.revokeObjectURL(href);
};
