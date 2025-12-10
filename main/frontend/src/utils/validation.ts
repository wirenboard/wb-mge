export const onCustomValidation = (ev: Event, message: string) => {
  const input = ev.target as HTMLInputElement;

  input.setCustomValidity('');
  if (!input.validity.valid && input.value) {
    input.setCustomValidity(message);
  }
};
