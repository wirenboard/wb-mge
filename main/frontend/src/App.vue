<template>
  <RouterView v-slot="{ Component }">
    <template v-if="Component">
      <KeepAlive>
        <Suspense>
          <component :is="Component"></component>
        </Suspense>
      </KeepAlive>
    </template>
  </RouterView>
</template>
