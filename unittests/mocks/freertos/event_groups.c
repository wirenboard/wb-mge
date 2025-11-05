#include "event_groups.h"
#include <stdlib.h>

#define MOCK_EVENT_GROUP_HANDLE                 ((EventGroupHandle_t)0xEEEEEEEE)

int mock_xEventGroupCreate_called = 0;
EventGroupHandle_t mock_xEventGroupCreate_return_value = MOCK_EVENT_GROUP_HANDLE;

int mock_xEventGroupSetBits_called = 0;
EventGroupHandle_t mock_xEventGroupSetBits_xEventGroup = NULL;
EventBits_t mock_xEventGroupSetBits_uxBitsToSet = 0;

int mock_xEventGroupWaitBits_called = 0;
EventGroupHandle_t mock_xEventGroupWaitBits_xEventGroup = NULL;
EventBits_t mock_xEventGroupWaitBits_uxBitsToWaitFor = 0;
BaseType_t mock_xEventGroupWaitBits_xClearOnExit = 0;
BaseType_t mock_xEventGroupWaitBits_xWaitForAllBits = 0;
TickType_t mock_xEventGroupWaitBits_xTicksToWait = 0;
EventBits_t mock_xEventGroupWaitBits_return_value = 0;

int mock_vEventGroupDelete_called = 0;
EventGroupHandle_t mock_vEventGroupDelete_xEventGroup = NULL;

EventGroupHandle_t xEventGroupCreate(void)
{
    mock_xEventGroupCreate_called++;
    return mock_xEventGroupCreate_return_value;
}

EventBits_t xEventGroupSetBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet)
{
    mock_xEventGroupSetBits_xEventGroup = xEventGroup;
    mock_xEventGroupSetBits_uxBitsToSet = uxBitsToSet;
    mock_xEventGroupSetBits_called++;
    return 0;
}

EventBits_t xEventGroupWaitBits(EventGroupHandle_t xEventGroup,
                                const EventBits_t uxBitsToWaitFor,
                                const BaseType_t xClearOnExit,
                                const BaseType_t xWaitForAllBits,
                                TickType_t xTicksToWait)
{
    mock_xEventGroupWaitBits_xEventGroup = xEventGroup;
    mock_xEventGroupWaitBits_uxBitsToWaitFor = uxBitsToWaitFor;
    mock_xEventGroupWaitBits_xClearOnExit = xClearOnExit;
    mock_xEventGroupWaitBits_xWaitForAllBits = xWaitForAllBits;
    mock_xEventGroupWaitBits_xTicksToWait = xTicksToWait;
    mock_xEventGroupWaitBits_called++;
    return mock_xEventGroupWaitBits_return_value;
}

void vEventGroupDelete(EventGroupHandle_t xEventGroup)
{
    mock_vEventGroupDelete_xEventGroup = xEventGroup;
    mock_vEventGroupDelete_called++;
}

void mock_freertos_event_groups_reset(void)
{
    mock_xEventGroupCreate_called = 0;
    mock_xEventGroupCreate_return_value = MOCK_EVENT_GROUP_HANDLE;

    mock_xEventGroupSetBits_called = 0;
    mock_xEventGroupSetBits_xEventGroup = NULL;
    mock_xEventGroupSetBits_uxBitsToSet = 0;

    mock_xEventGroupWaitBits_called = 0;
    mock_xEventGroupWaitBits_xEventGroup = NULL;
    mock_xEventGroupWaitBits_uxBitsToWaitFor = 0;
    mock_xEventGroupWaitBits_xClearOnExit = 0;
    mock_xEventGroupWaitBits_xWaitForAllBits = 0;
    mock_xEventGroupWaitBits_xTicksToWait = 0;
    mock_xEventGroupWaitBits_return_value = 0;

    mock_vEventGroupDelete_called = 0;
    mock_vEventGroupDelete_xEventGroup = NULL;
}
