<script setup lang="ts">
defineProps<{ id: string; ariaLabel?: string; disabled?: boolean }>();
const value = defineModel<boolean>();
</script>

<template>
  <label
    :for="id"
    class="toggle-switchy"
  >
    <input
      :id="id"
      type="checkbox"
      :checked="value"
      :disabled="disabled"
      :aria-label="ariaLabel"
      @change="(ev: Event) => value = (ev.target as HTMLInputElement).checked"
    />
    <span class="toggle">
      <span class="switch" />
    </span>
  </label>
</template>

<style>
.toggle-switchy {
  --w: 32px;
  --h: 18px;
  position: relative;
  width: var(--w);
  height: var(--h);
  display: inline-block;
  cursor: pointer;
  flex-shrink: 0;
}

.toggle-switchy > input {
  appearance: none;
  position: absolute;
  inset: 0;
  margin: 0;
  cursor: pointer;
  opacity: 0;
}

.toggle-switchy > input[disabled] { cursor: not-allowed; }
.toggle-switchy > input[disabled] ~ .toggle { opacity: 0.5; cursor: not-allowed; }
/* Checked+disabled keeps full opacity — shows "always on" without looking broken */
.toggle-switchy > input[disabled]:checked ~ .toggle { opacity: 1; }

.toggle-switchy .toggle {
  position: absolute;
  inset: 0;
  background: #d5d8de;
  border-radius: 999px;
  transition: background 0.15s;
}

.toggle-switchy .switch {
  position: absolute;
  top: 2px;
  left: 2px;
  width: calc(var(--h) - 4px);
  height: calc(var(--h) - 4px);
  background: #fff;
  border-radius: 50%;
  box-shadow: 0 1px 2px rgba(0, 0, 0, 0.15);
  transition: transform 0.15s;
}

.toggle-switchy > input:checked + .toggle {
  background: var(--primary-color);
}

.toggle-switchy > input:checked + .toggle > .switch {
  transform: translateX(calc(var(--w) - var(--h)));
}

.toggle-switchy > input:focus + .toggle {
  box-shadow: var(--input-focus-shadow);
}
</style>
