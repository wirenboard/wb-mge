export const downloadFile = (fileName: string, data: File) => {
  const file = new File([data], fileName);
  const href = URL.createObjectURL(file);
  const link = document.createElement('a');
  Object.assign(link, {
    href,
    download: fileName,
    style: 'display: none'
  });
  document.body.appendChild(link);
  link.click();
  link.remove();
  return URL.revokeObjectURL(href);
};
