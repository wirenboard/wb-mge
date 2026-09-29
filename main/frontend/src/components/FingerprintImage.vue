<script setup lang="ts">
import { onMounted, ref, watch } from 'vue';

// pixels: 8-bit grayscale, row-major from the top-left corner, width * height bytes.
const props = defineProps<{ pixels: Uint8Array; width: number; height: number; label: string }>();

const RGBA_BYTES = 4;
const OPAQUE = 255;

const canvas = ref<HTMLCanvasElement>();

const draw = () => {
  const context = canvas.value?.getContext('2d');
  if (!context) {
    return;
  }
  const image = context.createImageData(props.width, props.height);
  for (let i = 0; i < (props.width * props.height); i++) {
    const gray = props.pixels[i] ?? 0;
    const offset = i * RGBA_BYTES;
    image.data[offset] = gray;
    image.data[offset + 1] = gray;
    image.data[offset + 2] = gray;
    image.data[offset + 3] = OPAQUE;
  }
  context.putImageData(image, 0, 0);
};

onMounted(draw);
// After the DOM update: a new width or height attribute clears the canvas.
watch(() => [props.pixels, props.width, props.height], draw, { flush: 'post' });
</script>

<template>
  <canvas
    ref="canvas"
    class="fingerprintImage"
    role="img"
    :aria-label="label"
    :width="width"
    :height="height"
  />
</template>

<style scoped>
.fingerprintImage {
  display: block;
  max-width: 100%;
  height: auto;
  border: 1px solid var(--border-color);
  border-radius: var(--r-md);
  background: var(--bg-surface-subtle);
}
</style>
