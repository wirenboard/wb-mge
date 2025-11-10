#include "unity.h"

#include "event_groups.h"
#include <stdlib.h>
#include <string.h>

#define MAX_EVENT_GROUPS                    10

static EventGroupDef_t event_groups[MAX_EVENT_GROUPS];
static int event_group_count = 0;

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

    if (event_group_count >= MAX_EVENT_GROUPS) {
        return NULL;
    }

    EventGroupHandle_t handle = &event_groups[event_group_count];
    handle->uxEventBits = 0;
    event_group_count++;

    mock_xEventGroupCreate_data.return_value = handle;
    return handle;
}

void vEventGroupDelete(EventGroupHandle_t xEventGroup)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(xEventGroup, "vEventGroupDelete called with NULL event group handle");

    mock_vEventGroupDelete_data.xEventGroup = xEventGroup;
    mock_vEventGroupDelete_data.called++;

    xEventGroup->uxEventBits = 0;
}

EventBits_t xEventGroupSetBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(xEventGroup, "xEventGroupSetBits called with NULL event group handle");

    mock_xEventGroupSetBits_data.xEventGroup = xEventGroup;
    mock_xEventGroupSetBits_data.uxBitsToSet = uxBitsToSet;
    mock_xEventGroupSetBits_data.called++;

    xEventGroup->uxEventBits |= uxBitsToSet;

    return xEventGroup->uxEventBits;
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
        return 0;
    }

    EventBits_t current_bits = xEventGroup->uxEventBits;

    if (xWaitForAllBits == pdTRUE) {
        TEST_ASSERT_TRUE_MESSAGE(
            (current_bits & uxBitsToWaitFor) == uxBitsToWaitFor,
            "xEventGroupWaitBits condition should be met before xEventGroupWaitBits"
        );
    } else {
        TEST_ASSERT_TRUE_MESSAGE(
            (current_bits & uxBitsToWaitFor) != 0,
            "xEventGroupWaitBits condition should be met before xEventGroupWaitBits"
        );
    }

    if (xClearOnExit == pdTRUE) {
        xEventGroup->uxEventBits &= ~uxBitsToWaitFor;
    }

    return current_bits;
}

void mock_freertos_event_groups_reset(void)
{
    memset(&mock_xEventGroupCreate_data, 0, sizeof(mock_xEventGroupCreate_data));
    memset(&mock_xEventGroupSetBits_data, 0, sizeof(mock_xEventGroupSetBits_data));
    memset(&mock_xEventGroupWaitBits_data, 0, sizeof(mock_xEventGroupWaitBits_data));
    memset(&mock_vEventGroupDelete_data, 0, sizeof(mock_vEventGroupDelete_data));

    memset(event_groups, 0, sizeof(event_groups));
    event_group_count = 0;
}
