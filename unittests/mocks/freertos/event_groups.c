#include "unity.h"

#include "event_groups.h"
#include <stdlib.h>
#include <string.h>

mock_xEventGroupCreate_t mock_xEventGroupCreate_data;
mock_xEventGroupSetBits_t mock_xEventGroupSetBits_data;
mock_xEventGroupWaitBits_t mock_xEventGroupWaitBits_data;
mock_vEventGroupDelete_t mock_vEventGroupDelete_data;

EventGroupHandle_t xEventGroupCreate(void)
{
    mock_xEventGroupCreate_data.called++;

    if (mock_xEventGroupCreate_data.should_fail) {
        return NULL;
    }

    mock_xEventGroupCreate_data.return_value = (EventGroupHandle_t)0xABCD;

    return mock_xEventGroupCreate_data.return_value;
}

void vEventGroupDelete(EventGroupHandle_t xEventGroup)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(xEventGroup, "vEventGroupDelete called with NULL event group handle");

    mock_vEventGroupDelete_data.xEventGroup = xEventGroup;
    mock_vEventGroupDelete_data.called++;
}

EventBits_t xEventGroupSetBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(xEventGroup, "xEventGroupSetBits called with NULL event group handle");

    mock_xEventGroupSetBits_data.xEventGroup = xEventGroup;
    mock_xEventGroupSetBits_data.uxBitsToSet = uxBitsToSet;
    mock_xEventGroupSetBits_data.called++;

    return 0;
}

EventBits_t xEventGroupWaitBits(EventGroupHandle_t xEventGroup,
                                const EventBits_t uxBitsToWaitFor,
                                const BaseType_t xClearOnExit,
                                const BaseType_t xWaitForAllBits,
                                TickType_t xTicksToWait)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(xEventGroup, "xEventGroupWaitBits called with NULL event group handle");

    mock_xEventGroupWaitBits_data.xEventGroup = xEventGroup;
    mock_xEventGroupWaitBits_data.uxBitsToWaitFor = uxBitsToWaitFor;
    mock_xEventGroupWaitBits_data.xClearOnExit = xClearOnExit;
    mock_xEventGroupWaitBits_data.xWaitForAllBits = xWaitForAllBits;
    mock_xEventGroupWaitBits_data.xTicksToWait = xTicksToWait;
    mock_xEventGroupWaitBits_data.called++;

    if (mock_xEventGroupWaitBits_data.should_timeout) {
        return (EventBits_t)0;
    }

    return mock_xEventGroupWaitBits_data.return_value;
}

void mock_freertos_event_groups_reset(void)
{
    memset(&mock_xEventGroupCreate_data, 0, sizeof(mock_xEventGroupCreate_data));
    memset(&mock_xEventGroupSetBits_data, 0, sizeof(mock_xEventGroupSetBits_data));
    memset(&mock_xEventGroupWaitBits_data, 0, sizeof(mock_xEventGroupWaitBits_data));
    memset(&mock_vEventGroupDelete_data, 0, sizeof(mock_vEventGroupDelete_data));
}
