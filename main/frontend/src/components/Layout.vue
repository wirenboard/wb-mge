<template>
  <div class="container">
    <Sidebar />
    <main class="content">
      <slot />
    </main>
    <AlertsWrapper />
  </div>
</template>

<script setup lang="ts">
import AlertsWrapper from '@/components/AlertsWrapper.vue';
import Sidebar from '@/components/Sidebar.vue';
</script>

<style scoped>
.container {
  display: grid;
  grid-template-columns: var(--sidebar-width) 1fr;
  height: 100vh;
  overflow: hidden;

  @media (max-width: 680px) {
    display: flex;
    flex-direction: column;
    height: 100dvh;
  }
}

.content {
  overflow-y: auto;
  display: flex;
  flex-direction: column;
  min-width: 0;

  @media (max-width: 680px) {
    flex: 1;
  }
}
</style>
