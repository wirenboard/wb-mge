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
