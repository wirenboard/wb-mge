/* Controllable mock implementation of httpd_ws_get_fd_info and httpd_ws_send_data.
 * All other esp_http_server stubs are static inline in esp_http_server.h.
 *
 * Tests call mock_esp_http_server_reset() in setUp() to restore defaults, then
 * set mock_httpd_ws_get_fd_info_return / mock_httpd_ws_send_data_return to
 * exercise different code paths in sniffer_ws_dispatch(). */

#include <string.h>
#include "sniffer.h"         /* provides httpd_handle_t in __unittest_env__ */
#include "esp_http_server.h" /* provides httpd_ws_client_info_t, httpd_ws_frame_t */
#include "esp_err.h"

/* ---- httpd_ws_get_fd_info state ---- */
httpd_ws_client_info_t mock_httpd_ws_get_fd_info_return = HTTPD_WS_CLIENT_WEBSOCKET;
int                    mock_httpd_ws_get_fd_info_called  = 0;
int                    mock_httpd_ws_get_fd_info_last_fd = -1;

/* ---- httpd_ws_send_data state ---- */
esp_err_t        mock_httpd_ws_send_data_return     = ESP_OK;
int              mock_httpd_ws_send_data_called      = 0;
httpd_ws_frame_t mock_httpd_ws_send_data_last_frame = {0};
int              mock_httpd_ws_send_data_last_fd     = -1;

/* ---- httpd_sess_trigger_close state ---- */
int            mock_httpd_sess_trigger_close_called     = 0;
int            mock_httpd_sess_trigger_close_last_fd     = -1;
httpd_handle_t mock_httpd_sess_trigger_close_last_handle = NULL;

void mock_esp_http_server_reset(void)
{
    mock_httpd_ws_get_fd_info_return = HTTPD_WS_CLIENT_WEBSOCKET;
    mock_httpd_ws_get_fd_info_called  = 0;
    mock_httpd_ws_get_fd_info_last_fd = -1;

    mock_httpd_ws_send_data_return = ESP_OK;
    mock_httpd_ws_send_data_called  = 0;
    memset(&mock_httpd_ws_send_data_last_frame, 0, sizeof(mock_httpd_ws_send_data_last_frame));
    mock_httpd_ws_send_data_last_fd = -1;

    mock_httpd_sess_trigger_close_called     = 0;
    mock_httpd_sess_trigger_close_last_fd     = -1;
    mock_httpd_sess_trigger_close_last_handle = NULL;
}

esp_err_t httpd_sess_trigger_close(httpd_handle_t handle, int sockfd)
{
    mock_httpd_sess_trigger_close_called++;
    mock_httpd_sess_trigger_close_last_fd     = sockfd;
    mock_httpd_sess_trigger_close_last_handle = handle;
    return ESP_OK;
}

httpd_ws_client_info_t httpd_ws_get_fd_info(httpd_handle_t hd, int fd)
{
    (void)hd;
    mock_httpd_ws_get_fd_info_called++;
    mock_httpd_ws_get_fd_info_last_fd = fd;
    return mock_httpd_ws_get_fd_info_return;
}

esp_err_t httpd_ws_send_data(httpd_handle_t handle, int fd, httpd_ws_frame_t *frame)
{
    (void)handle;
    mock_httpd_ws_send_data_called++;
    mock_httpd_ws_send_data_last_fd = fd;
    if (frame) {
        mock_httpd_ws_send_data_last_frame = *frame;
    }
    return mock_httpd_ws_send_data_return;
}
