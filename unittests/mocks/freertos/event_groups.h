#pragma once

#include "FreeRTOS.h"

typedef void *EventGroupHandle_t;
typedef TickType_t EventBits_t;

extern int mock_xEventGroupCreate_called;
extern EventGroupHandle_t mock_xEventGroupCreate_return_value;

extern int mock_xEventGroupSetBits_called;
extern EventGroupHandle_t mock_xEventGroupSetBits_xEventGroup;
extern EventBits_t mock_xEventGroupSetBits_uxBitsToSet;

extern int mock_xEventGroupWaitBits_called;
extern EventGroupHandle_t mock_xEventGroupWaitBits_xEventGroup;
extern EventBits_t mock_xEventGroupWaitBits_uxBitsToWaitFor;
extern BaseType_t mock_xEventGroupWaitBits_xClearOnExit;
extern BaseType_t mock_xEventGroupWaitBits_xWaitForAllBits;
extern TickType_t mock_xEventGroupWaitBits_xTicksToWait;
extern EventBits_t mock_xEventGroupWaitBits_return_value;

extern int mock_vEventGroupDelete_called;
extern EventGroupHandle_t mock_vEventGroupDelete_xEventGroup;

void mock_freertos_event_groups_reset(void);

EventGroupHandle_t xEventGroupCreate(void);
EventBits_t xEventGroupSetBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t xEventGroup,
                                const EventBits_t uxBitsToWaitFor,
                                const BaseType_t xClearOnExit,
                                const BaseType_t xWaitForAllBits,
                                TickType_t xTicksToWait);
void vEventGroupDelete(EventGroupHandle_t xEventGroup);
