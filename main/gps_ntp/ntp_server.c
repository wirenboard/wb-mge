#include "ntp_server.h"

#include "gps_ntp.h"

#if GPS_NTP_ENABLED

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "sdkconfig.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include "gps_time.h"

#define NTP_SERVER_PORT                     123
#define NTP_SERVER_PACKET_LEN               48      // the NTP header; nothing else is sent
#define NTP_SERVER_RX_BUFFER_LEN            128     // room for extension fields, which are ignored
#define NTP_SERVER_RX_ERROR_DELAY_MS        1000

#define NTP_SERVER_TASK_STACK_SIZE          (4 * 1024)
// Above lwIP's tcpip task, so the task runs - and takes the receive timestamp - the moment
// that task hands a datagram over, instead of after whatever else tcpip has queued.
#define NTP_SERVER_TASK_PRIORITY            (CONFIG_LWIP_TCPIP_TASK_PRIO + 1)

// Byte 0: LI (2 bits), VN (3 bits), mode (3 bits).
#define NTP_SERVER_LI_SHIFT                 6
#define NTP_SERVER_VN_SHIFT                 3
#define NTP_SERVER_VN_MASK                  0x07
#define NTP_SERVER_MODE_MASK                0x07
#define NTP_SERVER_LI_NO_WARNING            0
#define NTP_SERVER_LI_UNSYNCHRONIZED        3
#define NTP_SERVER_MODE_CLIENT              3
#define NTP_SERVER_MODE_SERVER              4

#define NTP_SERVER_STRATUM_PRIMARY          1
#define NTP_SERVER_STRATUM_UNSPECIFIED      0
#define NTP_SERVER_PRECISION_LOG2_S         (-20)   // about 1 us, the esp_timer resolution

#define NTP_SERVER_REFID_LEN                4
#define NTP_SERVER_REFID_SYNCED             "PPS"   // with its NUL, exactly NTP_SERVER_REFID_LEN bytes
#define NTP_SERVER_REFID_UNSYNCED           "INIT"  // kiss code: not synchronised yet

#define NTP_SERVER_OFFSET_FLAGS             0
#define NTP_SERVER_OFFSET_STRATUM           1
#define NTP_SERVER_OFFSET_POLL              2
#define NTP_SERVER_OFFSET_PRECISION         3
#define NTP_SERVER_OFFSET_REFID             12
#define NTP_SERVER_OFFSET_REFERENCE_TS      16
#define NTP_SERVER_OFFSET_ORIGINATE_TS      24
#define NTP_SERVER_OFFSET_RECEIVE_TS        32
#define NTP_SERVER_OFFSET_TRANSMIT_TS       40
#define NTP_SERVER_TIMESTAMP_LEN            8

#define NTP_SERVER_UNIX_EPOCH_OFFSET_S      2208988800LL    // 1900-01-01 to 1970-01-01
#define NTP_SERVER_US_PER_S                 1000000LL
#define NTP_SERVER_FRACTION_SHIFT           32


static const char *TAG = "ntp_server";


static void ntp_server_put_u32(uint8_t *dst, uint32_t value)
{
    uint32_t value_be = htonl(value);
    memcpy(dst, &value_be, sizeof(value_be));
}


// NTP timestamp: seconds since 1900 (wrapping in 2036, as NTP era 0 does) and a 32-bit
// binary fraction of a second, both big-endian.
static void ntp_server_put_timestamp(uint8_t *dst, int64_t unix_us)
{
    int64_t unix_sec = unix_us / NTP_SERVER_US_PER_S;
    uint64_t sub_us = (uint64_t)(unix_us % NTP_SERVER_US_PER_S);

    ntp_server_put_u32(dst, (uint32_t)(unix_sec + NTP_SERVER_UNIX_EPOCH_OFFSET_S));
    ntp_server_put_u32(dst + sizeof(uint32_t), (uint32_t)((sub_us << NTP_SERVER_FRACTION_SHIFT) / NTP_SERVER_US_PER_S));
}


static void ntp_server_task(void *arg)
{
    int sock = (int)(intptr_t)arg;
    uint8_t request[NTP_SERVER_RX_BUFFER_LEN];
    uint8_t reply[NTP_SERVER_PACKET_LEN];

    while (1) {
        struct sockaddr_in peer;
        socklen_t peer_len = sizeof(peer);
        int received = recvfrom(sock, request, sizeof(request), 0, (struct sockaddr *)&peer, &peer_len);

        int64_t rx_unix_us = 0;
        int64_t ref_unix_us = 0;
        bool synced = gps_time_now(&rx_unix_us, &ref_unix_us);

        if (received < 0) {
            ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(NTP_SERVER_RX_ERROR_DELAY_MS));
            continue;
        }
        if (received < NTP_SERVER_PACKET_LEN) {
            continue;
        }
        if ((request[NTP_SERVER_OFFSET_FLAGS] & NTP_SERVER_MODE_MASK) != NTP_SERVER_MODE_CLIENT) {
            continue;
        }

        unsigned version = (request[NTP_SERVER_OFFSET_FLAGS] >> NTP_SERVER_VN_SHIFT) & NTP_SERVER_VN_MASK;
        unsigned leap = synced ? NTP_SERVER_LI_NO_WARNING : NTP_SERVER_LI_UNSYNCHRONIZED;

        // Root delay and root dispersion stay zero.
        memset(reply, 0, sizeof(reply));
        reply[NTP_SERVER_OFFSET_FLAGS] = (uint8_t)((leap << NTP_SERVER_LI_SHIFT) | (version << NTP_SERVER_VN_SHIFT) |
                                                   NTP_SERVER_MODE_SERVER);
        reply[NTP_SERVER_OFFSET_STRATUM] = synced ? NTP_SERVER_STRATUM_PRIMARY : NTP_SERVER_STRATUM_UNSPECIFIED;
        reply[NTP_SERVER_OFFSET_POLL] = request[NTP_SERVER_OFFSET_POLL];
        reply[NTP_SERVER_OFFSET_PRECISION] = (uint8_t)NTP_SERVER_PRECISION_LOG2_S;
        memcpy(reply + NTP_SERVER_OFFSET_REFID, synced ? NTP_SERVER_REFID_SYNCED : NTP_SERVER_REFID_UNSYNCED,
               NTP_SERVER_REFID_LEN);
        ntp_server_put_timestamp(reply + NTP_SERVER_OFFSET_REFERENCE_TS, ref_unix_us);
        memcpy(reply + NTP_SERVER_OFFSET_ORIGINATE_TS, request + NTP_SERVER_OFFSET_TRANSMIT_TS,
               NTP_SERVER_TIMESTAMP_LEN);
        ntp_server_put_timestamp(reply + NTP_SERVER_OFFSET_RECEIVE_TS, rx_unix_us);

        // Only the time matters here; the flags above describe the moment of reception.
        int64_t tx_unix_us = 0;
        int64_t tx_ref_unix_us = 0;
        (void)gps_time_now(&tx_unix_us, &tx_ref_unix_us);
        ntp_server_put_timestamp(reply + NTP_SERVER_OFFSET_TRANSMIT_TS, tx_unix_us);

        if (sendto(sock, reply, sizeof(reply), 0, (struct sockaddr *)&peer, peer_len) < 0) {
            ESP_LOGW(TAG, "sendto failed: errno %d", errno);
        }
    }
}


void ntp_server_start(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create UDP socket: errno %d", errno);
        return;
    }

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_addr.sin_port = htons(NTP_SERVER_PORT);

    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) != 0) {
        ESP_LOGE(TAG, "Unable to bind UDP port %d: errno %d", NTP_SERVER_PORT, errno);
        close(sock);
        return;
    }

    BaseType_t ret = xTaskCreate(ntp_server_task, "ntp_server_task", NTP_SERVER_TASK_STACK_SIZE, (void *)(intptr_t)sock,
                                 NTP_SERVER_TASK_PRIORITY, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create the NTP server task");
        close(sock);
        return;
    }

    ESP_LOGI(TAG, "Listening on UDP port %d", NTP_SERVER_PORT);
}

#endif // GPS_NTP_ENABLED
