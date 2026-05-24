/* Extra stubs for FreeRTOS event group functions not present in the shared mock. */
#include "event_groups.h"

/* Clear event group bits — no-op stub for unit tests. */
EventBits_t xEventGroupClearBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToClear)
{
    (void)xEventGroup;
    (void)uxBitsToClear;
    return 0;
}
