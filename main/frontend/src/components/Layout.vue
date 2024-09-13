<template>
  <div class="container">
    <Sidebar />
    <main class="content">
      <slot />
    </main>
    <Alert />
  </div>
</template>

<script setup lang="ts">
import Sidebar from '@/components/Sidebar.vue';
import Alert from '@/components/Alert.vue';
</script>

<style scoped>
.container {
  display: flex;

  @media (max-width: 680px) {
    flex-direction: column;
    gap: 12px;
  }
}
.content {
  overflow-y: auto;
  width: 100vw;
  padding: 18px 24px 24px;
  height: 100%;
  max-height: calc(100dvh - 42px);

  @media (max-width: 680px) {
    width: calc(100% - 24px);
    padding: 0 12px 12px;
    max-height: calc(100dvh - 84px);
  }
}
</style>
