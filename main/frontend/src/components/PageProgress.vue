<script setup lang="ts">
import { ref } from 'vue';
import { useRouter } from 'vue-router';

const progress = ref(0);
const isActive = ref(false);
let timer: number | null = null;

const router = useRouter();

const start = () => {
  isActive.value = true;
  progress.value = 0;
  timer = window.setInterval(() => {
    if (progress.value < 95) {
      progress.value += 1;
    }
  }, 100);
};

const finish = () => {
  if (timer !== null) {
    clearInterval(timer);
    timer = null;
  }
  progress.value = 100;
  setTimeout(() => {
    isActive.value = false;
    progress.value = 0;
  }, 300);
};

router.beforeEach(() => {
  start();
});

router.afterEach(async () => {
  await router.isReady();
  finish();
});
</script>

<template>
  <div
    v-if="isActive"
    class="page-progress"
    :style="{ width: progress + '%' }"
  />
</template>

<style scoped>
.page-progress {
  position: fixed;
  top: 0;
  left: 0;
  height: 4px;
  background: var(--primary-color);
  transition: width 0.2s ease;
  z-index: 9999;

  @media (max-width: 680px) {
    top: 58px;
  }
}
</style>
