#include "unity.h"

#include "event_groups.h"
#include "esp_bit_defs.h"

#include <stdlib.h>
#include <string.h>

mock_xEventGroupCreate_t mock_xEventGroupCreate_data = {0};
mock_xEventGroupSetBits_t mock_xEventGroupSetBits_data = {0};
mock_xEventGroupWaitBits_t mock_xEventGroupWaitBits_data = {0};
mock_vEventGroupDelete_t mock_vEventGroupDelete_data = {0};

EventGroupHandle_t xEventGroupCreate(void)
{
    mock_xEventGroupCreate_data.called++;

    if (mock_xEventGroupCreate_data.should_fail) {
        return NULL;
    }

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
    TEST_ASSERT_LESS_THAN_MESSAGE(
        MAX_SET_WAIT_CALLS,
        mock_xEventGroupSetBits_data.called,
        "Exceeded maximum number of xEventGroupSetBits calls tracked in mock"
    );

    mock_xEventGroupSetBits_data.xEventGroup[mock_xEventGroupSetBits_data.called] = xEventGroup;
    mock_xEventGroupSetBits_data.uxBitsToSet[mock_xEventGroupSetBits_data.called] = uxBitsToSet;
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
    TEST_ASSERT_LESS_THAN_MESSAGE(
        MAX_SET_WAIT_CALLS,
        mock_xEventGroupWaitBits_data.called,
        "Exceeded maximum number of xEventGroupWaitBits calls tracked in mock"
    );

    mock_xEventGroupWaitBits_data.xEventGroup[mock_xEventGroupWaitBits_data.called] = xEventGroup;
    mock_xEventGroupWaitBits_data.uxBitsToWaitFor[mock_xEventGroupWaitBits_data.called] = uxBitsToWaitFor;
    mock_xEventGroupWaitBits_data.xClearOnExit[mock_xEventGroupWaitBits_data.called] = xClearOnExit;
    mock_xEventGroupWaitBits_data.xWaitForAllBits[mock_xEventGroupWaitBits_data.called] = xWaitForAllBits;
    mock_xEventGroupWaitBits_data.xTicksToWait[mock_xEventGroupWaitBits_data.called] = xTicksToWait;
    mock_xEventGroupWaitBits_data.called++;

    if (mock_xEventGroupWaitBits_data.set_event_on_call > 0) {
        if (mock_xEventGroupWaitBits_data.called > mock_xEventGroupWaitBits_data.set_event_on_call) {
            mock_xEventGroupWaitBits_data.return_value |= mock_xEventGroupWaitBits_data.events_to_set;
        }
    }

    return mock_xEventGroupWaitBits_data.return_value;
}

void mock_freertos_event_groups_reset(void)
{
    memset(&mock_xEventGroupCreate_data, 0, sizeof(mock_xEventGroupCreate_data));
    mock_xEventGroupCreate_data.return_value = (EventGroupHandle_t)0xDEADBEEF;
    memset(&mock_xEventGroupSetBits_data, 0, sizeof(mock_xEventGroupSetBits_data));
    memset(&mock_xEventGroupWaitBits_data, 0, sizeof(mock_xEventGroupWaitBits_data));
    memset(&mock_vEventGroupDelete_data, 0, sizeof(mock_vEventGroupDelete_data));
}
