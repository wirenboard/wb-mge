#pragma once

#include "FreeRTOS.h"

#include <stdbool.h>

#define MAX_SET_WAIT_CALLS              16

typedef TickType_t EventBits_t;

typedef void *EventGroupHandle_t;

typedef struct {
    bool should_fail;
    int called;
    EventGroupHandle_t return_value;
} mock_xEventGroupCreate_t;

typedef struct {
    int called;
    EventGroupHandle_t xEventGroup[MAX_SET_WAIT_CALLS];
    EventBits_t uxBitsToSet[MAX_SET_WAIT_CALLS];
} mock_xEventGroupSetBits_t;

typedef struct {
    int called;
    EventGroupHandle_t xEventGroup[MAX_SET_WAIT_CALLS];
    EventBits_t uxBitsToWaitFor[MAX_SET_WAIT_CALLS];
    BaseType_t xClearOnExit[MAX_SET_WAIT_CALLS];
    BaseType_t xWaitForAllBits[MAX_SET_WAIT_CALLS];
    TickType_t xTicksToWait[MAX_SET_WAIT_CALLS];
    EventBits_t return_value;
    int set_event_on_call;
    uint32_t events_to_set;
} mock_xEventGroupWaitBits_t;

typedef struct {
    int called;
    EventGroupHandle_t xEventGroup;
} mock_vEventGroupDelete_t;

extern mock_xEventGroupCreate_t mock_xEventGroupCreate_data;
extern mock_xEventGroupSetBits_t mock_xEventGroupSetBits_data;
extern mock_xEventGroupWaitBits_t mock_xEventGroupWaitBits_data;
extern mock_vEventGroupDelete_t mock_vEventGroupDelete_data;

EventGroupHandle_t xEventGroupCreate(void);
void vEventGroupDelete(EventGroupHandle_t xEventGroup);
EventBits_t xEventGroupSetBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t xEventGroup,
                                const EventBits_t uxBitsToWaitFor,
                                const BaseType_t xClearOnExit,
                                const BaseType_t xWaitForAllBits,
                                TickType_t xTicksToWait);

void mock_freertos_event_groups_reset(void);
