#include "ssdp.h"

#include <lwip/netdb.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"

static const char *TAG = "ssdp";

/*
 * Defines
 */
#define SSDP_PORT 1900
#define SSDP_METHOD_SIZE 10
#define SSDP_URI_SIZE 2
#define SSDP_BUFFER_SIZE 64
#define SSDP_MULTICAST_TTL 2
#define SSDP_UUID_ROOT "38323636-4558-4dda-9188-cda0e6"
#define SSDP_MULTICAST_ADDR "239.255.255.250"

/*
 * Sizes
 */
#define SSDP_UUID_SIZE 37
#define SSDP_SCHEMA_URL_SIZE 64
#define SSDP_DEVICE_TYPE_SIZE 64
#define SSDP_USN_SUFFIX_SIZE 64
#define SSDP_FRIENDLY_NAME_SIZE 64
#define SSDP_SERIAL_NUMBER_SIZE 32
#define SSDP_PRESENTATION_URL_SIZE 128
#define SSDP_MODEL_NAME_SIZE 64
#define SSDP_MODEL_URL_SIZE 128
#define SSDP_MODEL_NUMBER_SIZE 32
#define SSDP_MODEL_DESCRIPTION_SIZE 64
#define SSDP_SERVER_NAME_SIZE 64
#define SSDP_MANUFACTURER_NAME_SIZE 64
#define SSDP_MANUFACTURER_URL_SIZE 128
#define SSDP_SERVICES_DESCRIPTION_SIZE 256
#define SSDP_ICONS_DESCRIPTION_SIZE 256
#define SSDP_DATAGRAM_SIZE 1401

/*
 * Templates messages
 */

static const char SSDP_RESPONSE_TEMPLATE[] =
    "HTTP/1.1 200 OK\r\n"
    "EXT:\r\n";

static const char SSDP_NOTIFY_TEMPLATE[] =
    "NOTIFY * HTTP/1.1\r\n"
    "HOST: 239.255.255.250:1900\r\n"
    "NTS: ssdp:alive\r\n";

static const char SSDP_PACKET_TEMPLATE[] =
    "%s"  // Message Notification or Response
    "CACHE-CONTROL: max-age=%u\r\n"
    "SERVER: %s UPNP/1.1 %s/%s\r\n"  // server_name, model_name, model_number
    "USN: uuid:%s%s\r\n"             // uuid, usn_suffix
    "%s: %s\r\n"                     // "NT" or "ST", device_type
    "LOCATION: http://%s:%u/%s\r\n"  // LocalIP, port, schemaURL
    "\r\n";

static const char SSDP_SCHEMA_TEMPLATE[] =
    "<?xml version=\"1.0\"?>"
    "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">"
    "<specVersion>"
    "<major>1</major>"
    "<minor>0</minor>"
    "</specVersion>"
    "<URLBase>http://%s:%u/</URLBase>"  // LocalIPStr, port
    "<device>"
    "<deviceType>urn:schemas-upnp-org:device:%s:1</deviceType>"  // device_type
    "<friendlyName>%s</friendlyName>"          // friendly_name
    "<presentationURL>%s</presentationURL>"    // presentation_url
    "<serialNumber>%s</serialNumber>"          // serial_number
    "<modelName>%s</modelName>"                // model_name
    "<modelDescription>%s</modelDescription>"  // model_description
    "<modelNumber>%s</modelNumber>"            // model_number
    "<modelURL>%s</modelURL>"                  // model_url
    "<manufacturer>%s</manufacturer>"          // manufacturer_name
    "<manufacturerURL>%s</manufacturerURL>"    // manufacturer_url
    "<UDN>uuid:%s</UDN>"                       // uuid
    "<serviceList>%s</serviceList>"            // service_list
    "<iconList>%s</iconList>"                  // icon_list
    "</device>"
    "</root>\r\n"
    "\r\n";

/*
 * Enums
 */

typedef enum { NONE, SEARCH, NOTIFY } ssdp_method_t;

/*
 * Struct definitions
 */

typedef struct {
  // Configuration
  uint8_t ttl;
  uint16_t port;
  uint32_t interval;
  uint16_t mx_max_delay;
  char *uuid;
  char *schema_url;
  char *device_type;
  char *friendly_name;
  char *serial_number;
  char *presentation_url;
  char *manufacturer_name;
  char *manufacturer_url;
  char *model_name;
  char *model_url;
  char *model_number;
  char *model_description;
  char *server_name;
  char *services_description;
  char *icons_description;
  // Task handle
  TaskHandle_t xHandle;
  // variables
  char *datagram_buffer;
  char respond_type[SSDP_DEVICE_TYPE_SIZE + 1];
  char usn_suffix[SSDP_USN_SUFFIX_SIZE + 1];
  char *schema;
  int delay;
  uint64_t notify_time;

} ssdp_task_config_t;

/*
 * Global variables
 */
static ssdp_task_config_t *ssdp_task_config = NULL;
volatile bool ssdp_running = false;
static int multicast_socket = -1;
static SemaphoreHandle_t ssdp_send_xSemaphore = NULL;
static SemaphoreHandle_t ssdp_on_packet_xSemaphore = NULL;

/*
 * Prototypes
 */

static void ssdp_set_UUID(char **uuid, const char *root_uid);
static void ssdp_running_task(void *pvParameters);
static char *ssdp_get_LocalIP();
static void onPacket(int sock, in_addr_t remote_addr, uint16_t remote_port,
                     char *buf, int len);
static void ssdp_send(int sock, ssdp_method_t method, in_addr_t remote_addr,
                      uint16_t remote_port);
static uint64_t ssdp_millis();
static int ssdp_random(int lowval, int highval);

/*
 * Local Functions
 */

int ssdp_random(int lowval, int highval) {
  srand(esp_timer_get_time() / 1000);
  return lowval + rand() % (highval - lowval + 1);
}

uint64_t ssdp_millis() { return esp_timer_get_time() / 1000; }

char *ssdp_get_LocalIP() {
  esp_err_t err;
  esp_netif_ip_info_t ip_info = {0};
  esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (netif == NULL) {
    netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  }
  if (netif == NULL) {
    netif = esp_netif_get_handle_from_ifkey("ETH_DEF");
  }
  if (netif == NULL) {
    return "0.0.0.0";
  }
  err = esp_netif_get_ip_info(netif, &ip_info);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get IP address info. Error 0x%x", err);
    return "0.0.0.0";
  }
  return ip4addr_ntoa((const ip4_addr_t *)&ip_info.ip);
}

/* Add a socket, either IPV4-only or IPV6 dual mode, to the IPV4
   multicast group */
static int socket_add_ipv4_multicast_group(int sock, bool assign_source_if) {
  if (!ssdp_task_config) {
    ESP_LOGE(TAG, "SSDP is not started.");
    return -1;
  }
  struct ip_mreq imreq = {0};
  struct in_addr iaddr = {0};
  int err = 0;
  // Configure source interface
  imreq.imr_interface.s_addr = IPADDR_ANY;
  // Configure multicast address to listen to
  err = inet_aton(SSDP_MULTICAST_ADDR, &imreq.imr_multiaddr.s_addr);
  if (err != 1) {
    ESP_LOGE(TAG, "Configured IPV4 multicast address '%s' is invalid.",
             SSDP_MULTICAST_ADDR);
    // Errors in the return value have to be negative
    err = -1;
    goto err;
  }
  ESP_LOGD(TAG, "Configured IPV4 Multicast address %s",
           inet_ntoa(imreq.imr_multiaddr.s_addr));
  if (!IP_MULTICAST(ntohl(imreq.imr_multiaddr.s_addr))) {
    ESP_LOGW(TAG,
             "Configured IPV4 multicast address '%s' is not a valid multicast "
             "address. This will probably not work.",
             SSDP_MULTICAST_ADDR);
  }

  if (assign_source_if) {
    // Assign the IPv4 multicast source interface, via its IP
    // (only necessary if this socket is IPV4 only)
    err = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &iaddr,
                     sizeof(struct in_addr));
    if (err < 0) {
      ESP_LOGE(TAG, "Failed to set IP_MULTICAST_IF. Error %d", errno);
      goto err;
    }
  }

  err = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &imreq,
                   sizeof(struct ip_mreq));
  if (err < 0) {
    ESP_LOGE(TAG, "Failed to set IP_ADD_MEMBERSHIP. Error %d", errno);
    goto err;
  }

err:
  return err;
}

static int create_multicast_ipv4_socket(void) {
  if (!ssdp_task_config) {
    ESP_LOGE(TAG, "SSDP is not started.");
    return -1;
  }
  struct sockaddr_in saddr = {0};
  int sock = -1;
  int err = 0;
  // Receive full datagram at once in
  sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (sock < 0) {
    ESP_LOGE(TAG, "Failed to create socket. Error %d", errno);
    return -1;
  }

  // Bind the socket to any address
  saddr.sin_family = PF_INET;
  saddr.sin_port = htons(SSDP_PORT);
  saddr.sin_addr.s_addr = htonl(INADDR_ANY);
  err = bind(sock, (struct sockaddr *)&saddr, sizeof(struct sockaddr_in));
  if (err < 0) {
    ESP_LOGE(TAG, "Failed to bind socket. Error %d", errno);
    goto err;
  }

  // Assign multicast TTL (set separately from normal interface TTL)
  setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ssdp_task_config->ttl,
             sizeof(uint8_t));
  if (err < 0) {
    ESP_LOGE(TAG, "Failed to set IP_MULTICAST_TTL. Error %d", errno);
    goto err;
  }

  // select whether multicast traffic should be received by this device, too
  // (if setsockopt() is not called, the default is no)
  uint8_t loopback_val = 1;
  err = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loopback_val,
                   sizeof(uint8_t));
  if (err < 0) {
    ESP_LOGE(TAG, "Failed to set IP_MULTICAST_LOOP. Error %d", errno);
    goto err;
  }

  // this is also a listening socket, so add it to the multicast
  // group for listening...
  err = socket_add_ipv4_multicast_group(sock, true);
  if (err < 0) {
    goto err;
  }

  // All set, socket is configured for sending and receiving
  return sock;

err:
  close(sock);
  return -1;
}

static void onPacket(int sock, in_addr_t remote_addr, uint16_t remote_port,
                     char *buf, int len) {
  ESP_LOGD(TAG, "received %d bytes from %s:%d", len,
           ip4addr_ntoa((const ip4_addr_t *)&remote_addr), remote_port);
  ESP_LOGD(TAG, "%s", buf);

  if (len == 0) {
    return;
  }
  if (xSemaphoreTake(ssdp_on_packet_xSemaphore, (TickType_t)10) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to take on packet semaphore");
    return;
  }
  ESP_LOGD(TAG, "Success to get on packet semaphore");
  ssdp_method_t method = NONE;
  typedef enum { METHOD, URI, PROTO, KEY, VALUE, ABORT } states;
  states state = METHOD;
  typedef enum { STRIP, START, SKIP, MAN, ST, MX } headers;
  headers header = STRIP;
  bool pending = false;
  bool stmatch = false;
  uint8_t cursor = 0;
  uint8_t cr = 0;

  char buffer[SSDP_BUFFER_SIZE] = {0};

  for (uint i = 0; i < len; i++) {
    char c = buf[i];

    if (c == '\r' || c == '\n') {
      cr++;
    } else {
      cr = 0;
    }

    switch (state) {
      case METHOD:
        if (c == ' ') {
          if (strcmp(buffer, "M-SEARCH") == 0) {
            method = SEARCH;
          }

          if (method == NONE) {
            state = ABORT;
          } else {
            state = URI;
          }
          cursor = 0;

        } else if (cursor < SSDP_METHOD_SIZE - 1) {
          buffer[cursor++] = c;
          buffer[cursor] = '\0';
        }
        break;
      case URI:
        if (c == ' ') {
          if (strcmp(buffer, "*")) {
            state = ABORT;
          } else {
            state = PROTO;
          }
          cursor = 0;
        } else if (cursor < SSDP_URI_SIZE - 1) {
          buffer[cursor++] = c;
          buffer[cursor] = '\0';
        }
        break;
      case PROTO:
        if (cr == 2) {
          state = KEY;
          cursor = 0;
        }
        break;
      case KEY:
        if (cr == 4) {
          if (stmatch) {
            pending = true;
          }
        } else if (c == ':') {
          cursor = 0;
          state = VALUE;
        } else if (c != '\r' && c != '\n' && c != ' ' &&
                   cursor < SSDP_BUFFER_SIZE - 1) {
          buffer[cursor++] = c;
          buffer[cursor] = '\0';
        }
        break;
      case VALUE:
        if (cr == 2) {
          switch (header) {
            case START:
              ESP_LOGD(TAG, "***********************\n");
            case SKIP:
            case STRIP:
              break;
            case MAN:
              ESP_LOGD(TAG, "MAN: %s\n", (char *)buffer);
              break;
            case ST:
              // save the search term for the reply and clear usn suffix.
              strlcpy(ssdp_task_config->respond_type, buffer,
                      sizeof(ssdp_task_config->respond_type));
              ssdp_task_config->usn_suffix[0] = '\0';

              ESP_LOGD(TAG, "ST: '%s'\n", buffer);

              // if looking for all or root reply with upnp:rootdevice
              if (strcmp(buffer, "ssdp:all") == 0 ||
                  strcmp(buffer, "upnp:rootdevice") == 0) {
                stmatch = true;
                // set USN suffix
                strlcpy(ssdp_task_config->usn_suffix, "::upnp:rootdevice",
                        sizeof(ssdp_task_config->usn_suffix));
                ESP_LOGD(TAG, "the search type matches all and root\n");
                state = KEY;
              } else {
                // if the search type matches our type, we should respond
                // instead of ABORT
                if (strcasecmp(buffer, ssdp_task_config->device_type) == 0) {
                  stmatch = true;
                  // set USN suffix to the device type
                  strlcpy(ssdp_task_config->usn_suffix,
                          "::", sizeof(ssdp_task_config->usn_suffix));
                  strlcat(ssdp_task_config->usn_suffix,
                          ssdp_task_config->device_type,
                          sizeof(ssdp_task_config->usn_suffix));
                  // ESP_LOGD(TAG, "the search type matches our type %s\n");
                  state = KEY;
                } else {
                  state = ABORT;
                  ESP_LOGD(
                      TAG,
                      "REJECT. The search type %s does not match our type %s\n",
                      buffer, ssdp_task_config->device_type);
                  ESP_LOGD(TAG, "***********************\n");
                }
              }
              break;
            case MX:
              ssdp_task_config->delay = ssdp_random(0, atoi(buffer)) * 1000L;
              if (ssdp_task_config->delay > ssdp_task_config->mx_max_delay) {
                ssdp_task_config->delay = ssdp_task_config->mx_max_delay;
              }
              break;
          }

          if (state != ABORT) {
            state = KEY;
            header = STRIP;
            cursor = 0;
          }
        } else if (c != '\r' && c != '\n') {
          if (header == STRIP) {
            if (c == ' ') {
              break;
            } else {
              header = START;
            }
          }
          if (header == START) {
            if (strncmp(buffer, "MA", 2) == 0) {
              header = MAN;
            } else if (strcmp(buffer, "ST") == 0) {
              header = ST;
            } else if (strcmp(buffer, "MX") == 0) {
              header = MX;
            } else {
              header = SKIP;
            }
          }

          if (cursor < SSDP_BUFFER_SIZE - 1) {
            buffer[cursor++] = c;
            buffer[cursor] = '\0';
          }
        }
        break;
      case ABORT:
        pending = false;
        ssdp_task_config->delay = 0;
        break;
    }
  }

  if (pending) {
    pending = false;
    ssdp_task_config->delay = 0;
    ssdp_send(sock, NONE, remote_addr, remote_port);
    ESP_LOGD(TAG, "SSDP: respond...\n");
  } else {
    ESP_LOGD(TAG, "SSDP: ignore...\n");
  }
  xSemaphoreGive(ssdp_on_packet_xSemaphore);
}

void ssdp_send(int sock, ssdp_method_t method, in_addr_t remote_addr,
               uint16_t remote_port) {
  if (xSemaphoreTake(ssdp_send_xSemaphore, (TickType_t)10) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to take send semaphore");
    return;
  }
  ESP_LOGD(TAG, "Success to get send semaphore");
  int err = 0;
  if (method == NONE) {
    ESP_LOGD(TAG, "Sending Response to %s:%d",
             ip4addr_ntoa((const ip4_addr_t *)&remote_addr), remote_port);

  } else {
    // send notify with our root device type
    strlcpy(ssdp_task_config->respond_type, "upnp:rootdevice",
            sizeof(ssdp_task_config->respond_type));
    strlcpy(ssdp_task_config->usn_suffix, "::upnp:rootdevice",
            sizeof(ssdp_task_config->usn_suffix));
    ESP_LOGD(TAG, "Sending Notify to %s:%d", SSDP_MULTICAST_ADDR, SSDP_PORT);
  }
  size_t len_msg_template = (method == NONE) ? strlen(SSDP_RESPONSE_TEMPLATE)
                                             : strlen(SSDP_NOTIFY_TEMPLATE);
  size_t msg_buffer_size =
      strlen(SSDP_PACKET_TEMPLATE) + len_msg_template + 5 +
      (ssdp_task_config->server_name ? strlen(ssdp_task_config->server_name)
                                     : 1) +
      (ssdp_task_config->model_name ? strlen(ssdp_task_config->model_name)
                                    : 1) +
      (ssdp_task_config->model_number ? strlen(ssdp_task_config->model_number)
                                      : 1) +
      (SSDP_UUID_SIZE) + (SSDP_USN_SUFFIX_SIZE) + 2  // "NT" or "ST"
      + strlen(ssdp_task_config->respond_type) + 16 + 5 +
      (ssdp_task_config->schema_url ? strlen(ssdp_task_config->schema_url) : 1);
  char *msg_buffer = (char *)calloc(msg_buffer_size + 1, sizeof(char));

  if (!msg_buffer) {
    ESP_LOGE(TAG, "Error not enough memory for valueBuffer creation");
    xSemaphoreGive(ssdp_send_xSemaphore);
    return;
  }

  int result = snprintf(
      msg_buffer, msg_buffer_size, SSDP_PACKET_TEMPLATE,
      ((method == NONE) ? SSDP_RESPONSE_TEMPLATE : SSDP_NOTIFY_TEMPLATE),
      (unsigned int)ssdp_task_config->interval,
      ssdp_task_config->server_name ? ssdp_task_config->server_name : "",
      ssdp_task_config->model_name ? ssdp_task_config->model_name : "",
      ssdp_task_config->model_number ? ssdp_task_config->model_number : "",
      ssdp_task_config->uuid ? ssdp_task_config->uuid : "",
      ssdp_task_config->usn_suffix, (method == NONE) ? "ST" : "NT",
      ssdp_task_config->respond_type, ssdp_get_LocalIP(),
      ssdp_task_config->port,
      ssdp_task_config->schema_url ? ssdp_task_config->schema_url : "");
  ESP_LOGD(TAG, "sprintf result: %d", result);
  if (result < 0) {
    ESP_LOGE(TAG, "Error not enough memory for msg_buffer creation");
  }

  ESP_LOGD(TAG, "*************************TX*************************");
  ESP_LOGD(TAG, "%s", msg_buffer);
  ESP_LOGD(TAG, "****************************************************");

  struct addrinfo hints = {
      .ai_flags = AI_PASSIVE,
      .ai_socktype = SOCK_DGRAM,
  };
  struct addrinfo *res;

  hints.ai_family = AF_INET;  // For an IPv4 socket
  if (method == NONE) {
    err = getaddrinfo(ip4addr_ntoa((const ip4_addr_t *)&remote_addr), NULL,
                      &hints, &res);
  } else {
    err = getaddrinfo(SSDP_MULTICAST_ADDR, NULL, &hints, &res);
  }
  if (res == 0 || err < 0) {
    if (err < 0) {
      ESP_LOGE(TAG,
               "getaddrinfo() failed for IPV4 destination address. error: %d",
               err);
    } else {
      ESP_LOGE(TAG, "getaddrinfo() did not return any addresses");
    }
    free(msg_buffer);
    xSemaphoreGive(ssdp_send_xSemaphore);
    return;
  }
  if (method == NONE) {
    ((struct sockaddr_in *)res->ai_addr)->sin_port = remote_port;
  } else {
    ((struct sockaddr_in *)res->ai_addr)->sin_port = htons(SSDP_PORT);
  }

  ESP_LOGD(TAG, "Sending to IPV4 multicast address %s:%d...",
           ip4addr_ntoa((const ip4_addr_t *)&remote_addr), remote_port);

  err = sendto(sock, msg_buffer, strlen(msg_buffer), 0, res->ai_addr,
               res->ai_addrlen);
  freeaddrinfo(res);
  if (err < 0) {
    ESP_LOGE(TAG, "IPV4 sendto failed. errno: %d", errno);
  } else {
  }

  free(msg_buffer);
  xSemaphoreGive(ssdp_send_xSemaphore);
}

void ssdp_running_task(void *pvParameters) {
  ESP_LOGI(TAG, "Starting ssdp_running_task");
  ssdp_running = true;
  while (ssdp_running) {
    multicast_socket = create_multicast_ipv4_socket();
    if (multicast_socket < 0) {
      ESP_LOGE(TAG, "Failed to create IPv4 multicast socket");
    }
    if (multicast_socket < 0) {
      // Nothing to do!
      vTaskDelay(5 / portTICK_PERIOD_MS);
      continue;
    }

    // set destination multicast addresses for sending from these sockets
    struct sockaddr_in sdestv4 = {
        .sin_family = PF_INET,
        .sin_port = htons(SSDP_PORT),
    };
    // We know this inet_aton will pass because we did it above already
    inet_aton(SSDP_MULTICAST_ADDR, &sdestv4.sin_addr.s_addr);  // s_addr = u32
    // Loop waiting for UDP received, and sending UDP packets if we don't
    // see any.
    int err = 1;
    while (err > 0) {
      struct timeval tv = {
          .tv_sec = 2,
          .tv_usec = 0,
      };
      fd_set rfds;
      FD_ZERO(&rfds);
      FD_SET(multicast_socket, &rfds);

      int s = select(multicast_socket + 1, &rfds, NULL, NULL, &tv);
      if (s < 0) {
        ESP_LOGE(TAG, "Select failed: errno %d", errno);
        err = -1;
        break;
      } else if (s > 0) {
        if (FD_ISSET(multicast_socket, &rfds)) {
          // Incoming datagram received
          struct sockaddr_storage raddr;
          socklen_t socklen = sizeof(raddr);
          // Read all the datagram at once, if over buffer the data will
          // be discarded
          int len = recvfrom(
              multicast_socket, ssdp_task_config->datagram_buffer,
              SSDP_DATAGRAM_SIZE - 1, 0, (struct sockaddr *)&raddr, &socklen);
          if (len < 0) {
            ESP_LOGE(TAG, "multicast recvfrom failed: errno %d", errno);
            err = -1;
            break;
          }
          if (raddr.ss_family == PF_INET) {
            ssdp_task_config->datagram_buffer[len] =
                0;  // Null-terminate whatever we received and treat
                    // like a string...
            uint16_t remote_port = ((struct sockaddr_in *)&raddr)->sin_port;
            in_addr_t remote_addr =
                ((struct sockaddr_in *)&raddr)->sin_addr.s_addr;
            onPacket(multicast_socket, remote_addr, remote_port,
                     ssdp_task_config->datagram_buffer, len);
          }
        }
      }
      if (ssdp_task_config) {
        if ((ssdp_task_config->notify_time == 0) ||
            (ssdp_millis() - ssdp_task_config->notify_time) >
                (ssdp_task_config->interval * 1000L)) {
          ssdp_task_config->notify_time = ssdp_millis();
          ESP_LOGD(TAG, "SSDP: notify...\n");
          ssdp_send(multicast_socket, NOTIFY, 0, 0);
        }
      }
    }
    ESP_LOGE(TAG, "Shutting down socket and restarting...");
    shutdown(multicast_socket, 0);
    close(multicast_socket);
    multicast_socket = -1;
  }

  vTaskDelete(NULL);
}

void ssdp_set_UUID(char **uuid, const char *root_uid) {
  uint8_t mac[6];
  esp_err_t err = esp_efuse_mac_get_default((uint8_t *)&mac);
  if (ESP_OK != err) {
    memset(mac, 0, 6);
    ESP_LOGW(TAG, "Not able to read MAC address, use 000000");
  }

  sprintf(*uuid, "%s%02x%02x%02x", root_uid, mac[2], mac[1], mac[0]);
}

/*
 * Global Functions
 */
esp_err_t ssdp_init() {
  if (!ssdp_send_xSemaphore) {
    ssdp_send_xSemaphore = xSemaphoreCreateBinary();
    if (!ssdp_send_xSemaphore) {
      ESP_LOGE(TAG, "Error creating the send semaphore");
      return ESP_ERR_NO_MEM;
    }
  }
  if (!ssdp_on_packet_xSemaphore) {
    ssdp_on_packet_xSemaphore = xSemaphoreCreateBinary();
    if (!ssdp_on_packet_xSemaphore) {
      ESP_LOGE(TAG, "Error creating the on packet semaphore");
      return ESP_ERR_NO_MEM;
    }
  }

  xSemaphoreGive(ssdp_send_xSemaphore);
  xSemaphoreGive(ssdp_on_packet_xSemaphore);
  return ESP_OK;
}

esp_err_t ssdp_start(ssdp_config_t *configuration) {
  esp_err_t err_start = ESP_OK;
  if (!ssdp_send_xSemaphore) {
    ESP_LOGE(TAG, "SSDP not initialized");
    return ESP_ERR_INVALID_STATE;
  }
  if (!configuration) {
    ESP_LOGE(TAG, "Missing configuration parameter");
    return ESP_ERR_INVALID_ARG;
  }
  // if already have a config task or a socket it means it was not cleaned
  if (ssdp_task_config || multicast_socket != -1) {
    ESP_LOGE(TAG, "SSDP already started");
    return ESP_ERR_INVALID_STATE;
  }
  ESP_LOGI(TAG, "SSDP basic sanity check done");

  // Create task configuration workplace
  ssdp_task_config =
      (ssdp_task_config_t *)calloc(1, sizeof(ssdp_task_config_t));
  if (!ssdp_task_config) {
    ESP_LOGE(TAG, "No enough memory for ssdp task configuration");
    err_start = ESP_ERR_NO_MEM;
  }
  if (err_start == ESP_OK) {
    // Buffer for udp packet
    ssdp_task_config->datagram_buffer =
        (char *)calloc(SSDP_DATAGRAM_SIZE, sizeof(uint8_t));
    if (!ssdp_task_config->datagram_buffer) {
      ESP_LOGE(TAG, "No enough memory for ssdp datagram buffer");
      err_start = ESP_ERR_NO_MEM;
    }
  }
  if (err_start == ESP_OK) {
    // Task configuration
    ssdp_task_config->port = configuration->port;
    ssdp_task_config->ttl = configuration->ttl;
    ssdp_task_config->interval = configuration->interval;
    ssdp_task_config->mx_max_delay = configuration->mx_max_delay;
    // Working variables
    ssdp_task_config->delay = 0;
    ssdp_task_config->respond_type[0] = 0x0;
    ssdp_task_config->notify_time = 0;

    // UUID
    ssdp_task_config->uuid = (char *)calloc(SSDP_UUID_SIZE + 1, sizeof(char));

    if (!ssdp_task_config->uuid) {
      ESP_LOGE(TAG, "No enough memory for ssdp user task configuration");
      err_start = ESP_ERR_NO_MEM;
    }
  }

  if (err_start == ESP_OK) {
    // Nothing is configured use default root and mac
    if ((!configuration->uuid_root || strlen(configuration->uuid_root) == 0) &&
        (!configuration->uuid || strlen(configuration->uuid) == 0)) {
      ssdp_set_UUID(&ssdp_task_config->uuid, SSDP_UUID_ROOT);
    } else {
      // if no full UID is configured but has root
      if ((!configuration->uuid || strlen(configuration->uuid) == 0)) {
        if (strlen(configuration->uuid_root) == strlen(SSDP_UUID_ROOT)) {
          ssdp_set_UUID(&ssdp_task_config->uuid, configuration->uuid_root);
        } else {
          ESP_LOGE(TAG, "Wrong size of uuid root parameter");
          err_start = ESP_ERR_INVALID_ARG;
        }
      } else {
        if (strlen(configuration->uuid) == SSDP_UUID_SIZE) {
          strcpy(ssdp_task_config->uuid, configuration->uuid);
        } else {
          ESP_LOGE(TAG, "Invalid uuid parameter");
          err_start = ESP_ERR_INVALID_ARG;
        }
      }
    }
  }

  if (err_start == ESP_OK) {
    // Schema_ url
    if (configuration->schema_url) {
      if (strlen(configuration->schema_url) > SSDP_SCHEMA_URL_SIZE) {
        ESP_LOGE(TAG, "schema_url too long");
        err_start = ESP_ERR_INVALID_ARG;
      }
      if (err_start == ESP_OK) {
        ssdp_task_config->schema_url =
            (char *)calloc(strlen(configuration->schema_url) + 1, sizeof(char));
        if (!ssdp_task_config->schema_url) {
          ESP_LOGE(TAG, "No enough memory for ssdp user task configuration");
          err_start = ESP_ERR_NO_MEM;
        }
        if (err_start == ESP_OK) {
          strcpy(ssdp_task_config->schema_url, configuration->schema_url);
        }
      }
    }
  }

  if (err_start == ESP_OK) {
    // Device type
    if (configuration->device_type) {
      if (strlen(configuration->device_type) > SSDP_DEVICE_TYPE_SIZE) {
        ESP_LOGE(TAG, "Device type too long");
        err_start = ESP_ERR_INVALID_ARG;
      }
      if (err_start == ESP_OK) {
        ssdp_task_config->device_type = (char *)calloc(
            strlen(configuration->device_type) + 1, sizeof(char));
        if (!ssdp_task_config->device_type) {
          ESP_LOGE(TAG, "No enough memory for ssdp user task configuration");
          err_start = ESP_ERR_NO_MEM;
        }
        if (err_start == ESP_OK) {
          strcpy(ssdp_task_config->device_type, configuration->device_type);
        }
      }
    }
  }

  if (err_start == ESP_OK) {
    // Friendly name
    if (configuration->friendly_name) {
      if (strlen(configuration->friendly_name) > SSDP_FRIENDLY_NAME_SIZE) {
        ESP_LOGE(TAG, "Friendly name too long");
        err_start = ESP_ERR_INVALID_ARG;
      }
      if (err_start == ESP_OK) {
        ssdp_task_config->friendly_name = (char *)calloc(
            strlen(configuration->friendly_name) + 1, sizeof(char));
        if (!ssdp_task_config->friendly_name) {
          ESP_LOGE(TAG, "No enough memory for ssdp user task configuration");
          err_start = ESP_ERR_NO_MEM;
        }
        if (err_start == ESP_OK) {
          strcpy(ssdp_task_config->friendly_name, configuration->friendly_name);
        }
      }
    }
  }

  if (err_start == ESP_OK) {
    // Serial Number
    if (configuration->serial_number) {
      if (strlen(configuration->serial_number) > SSDP_SERIAL_NUMBER_SIZE) {
        ESP_LOGE(TAG, "Serial number too long");
        err_start = ESP_ERR_INVALID_ARG;
      }
      if (err_start == ESP_OK) {
        ssdp_task_config->serial_number = (char *)calloc(
            strlen(configuration->serial_number) + 1, sizeof(char));
        if (!ssdp_task_config->serial_number) {
          ESP_LOGE(TAG, "No enough memory for ssdp user task configuration");
          err_start = ESP_ERR_NO_MEM;
        }
        if (err_start == ESP_OK) {
          strcpy(ssdp_task_config->serial_number, configuration->serial_number);
        }
      }
    }
  }

  if (err_start == ESP_OK) {
    // Presentation url
    if (configuration->presentation_url) {
      if (strlen(configuration->presentation_url) >
          SSDP_PRESENTATION_URL_SIZE) {
        ESP_LOGE(TAG, "Presentation url too long");
        err_start = ESP_ERR_INVALID_ARG;
      }
      if (err_start == ESP_OK) {
        ssdp_task_config->presentation_url = (char *)calloc(
            strlen(configuration->presentation_url) + 1, sizeof(char));
        if (!ssdp_task_config->presentation_url) {
          ESP_LOGE(TAG, "No enough memory for ssdp user task configuration");
          err_start = ESP_ERR_NO_MEM;
        }
        if (err_start == ESP_OK) {
          strcpy(ssdp_task_config->presentation_url,
                 configuration->presentation_url);
        }
      }
    }
  }

  if (err_start == ESP_OK) {
    // Manufacturer name
    if (configuration->manufacturer_name) {
      if (strlen(configuration->manufacturer_name) >
          SSDP_MANUFACTURER_NAME_SIZE) {
        ESP_LOGE(TAG, "Manufacturer name too long");
        err_start = ESP_ERR_INVALID_ARG;
      }
      if (err_start == ESP_OK) {
        ssdp_task_config->manufacturer_name = (char *)calloc(
            strlen(configuration->manufacturer_name) + 1, sizeof(char));
        if (!ssdp_task_config->manufacturer_name) {
          ESP_LOGE(TAG, "No enough memory for ssdp user task configuration");
          err_start = ESP_ERR_NO_MEM;
        }
        if (err_start == ESP_OK) {
          strcpy(ssdp_task_config->manufacturer_name,
                 configuration->manufacturer_name);
        }
      }
    }
  }
  if (err_start == ESP_OK) {
    // Manufacturer url
    if (configuration->manufacturer_url) {
      if (strlen(configuration->manufacturer_url) >
          SSDP_MANUFACTURER_URL_SIZE) {
        ESP_LOGE(TAG, "Manufacturer url too long");
        err_start = ESP_ERR_INVALID_ARG;
      }
      if (err_start == ESP_OK) {
        ssdp_task_config->manufacturer_url = (char *)calloc(
            strlen(configuration->manufacturer_url) + 1, sizeof(char));
        if (!ssdp_task_config->manufacturer_url) {
          ESP_LOGE(TAG, "No enough memory for ssdp user task configuration");
          err_start = ESP_ERR_NO_MEM;
        }
        if (err_start == ESP_OK) {
          strcpy(ssdp_task_config->manufacturer_url,
                 configuration->manufacturer_url);
        }
      }
    }
  }

  if (err_start == ESP_OK) {
    // Model name
    if (configuration->model_name) {
      if (strlen(configuration->model_name) > SSDP_MODEL_NAME_SIZE) {
        ESP_LOGE(TAG, "Model name too long");
        err_start = ESP_ERR_INVALID_ARG;
      }
      if (err_start == ESP_OK) {
        ssdp_task_config->model_name =
            (char *)calloc(strlen(configuration->model_name) + 1, sizeof(char));
        if (!ssdp_task_config->model_name) {
          ESP_LOGE(TAG, "No enough memory for ssdp user task configuration");
          err_start = ESP_ERR_NO_MEM;
        }
        if (err_start == ESP_OK) {
          strcpy(ssdp_task_config->model_name, configuration->model_name);
        }
      }
    }
  }

  if (err_start == ESP_OK) {
    // Model url
    if (configuration->model_url) {
      if (strlen(configuration->model_url) > SSDP_MODEL_URL_SIZE) {
        ESP_LOGE(TAG, "Model url too long");
        err_start = ESP_ERR_INVALID_ARG;
      }
      if (err_start == ESP_OK) {
        ssdp_task_config->model_url =
            (char *)calloc(strlen(configuration->model_url) + 1, sizeof(char));
        if (!ssdp_task_config->model_url) {
          ESP_LOGE(TAG, "No enough memory for ssdp user task configuration");
          err_start = ESP_ERR_NO_MEM;
        }
        if (err_start == ESP_OK) {
          strcpy(ssdp_task_config->model_url, configuration->model_url);
        }
      }
    }
  }

  if (err_start == ESP_OK) {
    // Model number
    if (configuration->model_number) {
      if (strlen(configuration->model_number) > SSDP_MODEL_NUMBER_SIZE) {
        ESP_LOGE(TAG, "Model number too long");
        err_start = ESP_ERR_INVALID_ARG;
      }
      if (err_start == ESP_OK) {
        ssdp_task_config->model_number = (char *)calloc(
            strlen(configuration->model_number) + 1, sizeof(char));
        if (!ssdp_task_config->model_number) {
          ESP_LOGE(TAG, "No enough memory for ssdp user task configuration");
          err_start = ESP_ERR_NO_MEM;
        }
        if (err_start == ESP_OK) {
          strcpy(ssdp_task_config->model_number, configuration->model_number);
        }
      }
    }
  }

  if (err_start == ESP_OK) {
    // Model description
    if (configuration->model_description) {
      if (strlen(configuration->model_description) >
          SSDP_MODEL_DESCRIPTION_SIZE) {
        ESP_LOGE(TAG, "Model description too long");
        err_start = ESP_ERR_INVALID_ARG;
      }
      if (err_start == ESP_OK) {
        ssdp_task_config->model_description = (char *)calloc(
            strlen(configuration->model_description) + 1, sizeof(char));
        if (!ssdp_task_config->model_description) {
          ESP_LOGE(TAG, "No enough memory for ssdp user task configuration");
          err_start = ESP_ERR_NO_MEM;
        }
        if (err_start == ESP_OK) {
          strcpy(ssdp_task_config->model_description,
                 configuration->model_description);
        }
      }
    }
  }
  if (err_start == ESP_OK) {
    // Server name
    if (configuration->server_name) {
      if (strlen(configuration->server_name) > SSDP_SERVER_NAME_SIZE) {
        ESP_LOGE(TAG, "Server name too long");
        err_start = ESP_ERR_INVALID_ARG;
      }
      if (err_start == ESP_OK) {
        ssdp_task_config->server_name = (char *)calloc(
            strlen(configuration->server_name) + 1, sizeof(char));
        if (!ssdp_task_config->server_name) {
          ESP_LOGE(TAG, "No enough memory for ssdp user task configuration");
          err_start = ESP_ERR_NO_MEM;
        }
        if (err_start == ESP_OK) {
          strcpy(ssdp_task_config->server_name, configuration->server_name);
        }
      }
    }
  }

  if (err_start == ESP_OK) {
    // Services description
    if (configuration->services_description) {
      if (strlen(configuration->services_description) >
          SSDP_SERVICES_DESCRIPTION_SIZE) {
        ESP_LOGE(TAG, "Services description too long");
        err_start = ESP_ERR_INVALID_ARG;
      }
      if (err_start == ESP_OK) {
        ssdp_task_config->services_description = (char *)calloc(
            strlen(configuration->services_description) + 1, sizeof(char));
        if (!ssdp_task_config->services_description) {
          ESP_LOGE(TAG, "No enough memory for ssdp user task configuration");
          err_start = ESP_ERR_NO_MEM;
        }
        if (err_start == ESP_OK) {
          strcpy(ssdp_task_config->services_description,
                 configuration->services_description);
        }
      }
    }
  }

  if (err_start == ESP_OK) {
    // Icons description
    if (configuration->icons_description) {
      if (strlen(configuration->icons_description) >
          SSDP_ICONS_DESCRIPTION_SIZE) {
        ESP_LOGE(TAG, "Icons description too long");
        err_start = ESP_ERR_INVALID_ARG;
      }
      if (err_start == ESP_OK) {
        ssdp_task_config->icons_description = (char *)calloc(
            strlen(configuration->icons_description) + 1, sizeof(char));
        if (!ssdp_task_config->icons_description) {
          ESP_LOGE(TAG, "No enough memory for ssdp user task configuration");
          err_start = ESP_ERR_NO_MEM;
        }
        if (err_start == ESP_OK) {
          strcpy(ssdp_task_config->icons_description,
                 configuration->icons_description);
        }
      }
    }
  }

  if (err_start == ESP_OK) {
    ESP_LOGI(TAG, "Task creation core %d, stack:  %d, priotity %d",
             configuration->core_id, configuration->stack_size,
             configuration->task_priority);

    // Task creation
    BaseType_t res = xTaskCreatePinnedToCore(
        ssdp_running_task, "ssdp_running_task", configuration->stack_size, NULL,
        configuration->task_priority, &ssdp_task_config->xHandle,
        configuration->core_id);
    if (!(res == pdPASS && ssdp_task_config->xHandle)) {
      ESP_LOGE(TAG, "Failed to create task");
      err_start = ESP_FAIL;
    }
  }

  return err_start;
}

esp_err_t ssdp_stop() {
  ESP_LOGD(TAG, "Stopping SSDP");
  // to close properly let's just the loop to stop
  ssdp_running = false;
  vTaskDelay(100 / portTICK_PERIOD_MS);
  if (ssdp_task_config) {
    // Delete the Task
    if (ssdp_task_config->xHandle) {
      // No need as stopping loop make task auto delete
      //  vTaskDelete(ssdp_task_config->xHandle);
      ssdp_task_config->xHandle = NULL;
      ESP_LOGD(TAG, "Deleting SSDP Task");
    }
    if (multicast_socket != -1) {
      shutdown(multicast_socket, 0);
      close(multicast_socket);
      multicast_socket = -1;
    }
    // Free memory
    free(ssdp_task_config->device_type);
    free(ssdp_task_config->friendly_name);
    free(ssdp_task_config->serial_number);
    free(ssdp_task_config->presentation_url);
    free(ssdp_task_config->manufacturer_name);
    free(ssdp_task_config->manufacturer_url);
    free(ssdp_task_config->model_name);
    free(ssdp_task_config->model_url);
    free(ssdp_task_config->model_number);
    free(ssdp_task_config->model_description);
    free(ssdp_task_config->server_name);
    free(ssdp_task_config->services_description);
    free(ssdp_task_config->icons_description);
    free(ssdp_task_config->datagram_buffer);
    free(ssdp_task_config->schema);
    free(ssdp_task_config);
    ssdp_task_config = NULL;
  }

  if (multicast_socket != -1) {
    shutdown(multicast_socket, 0);
    close(multicast_socket);
    multicast_socket = -1;
  }
  return ESP_OK;
}

const char *get_ssdp_schema_str() {
  if (!ssdp_task_config) {
    ESP_LOGE(TAG, "SSDP not started");
    return NULL;
  }
  if (ssdp_task_config->schema) {
    free(ssdp_task_config->schema);
    ssdp_task_config->schema = NULL;
  }

  size_t template_size =
      sizeof(SSDP_SCHEMA_TEMPLATE) + 15  // IP
      + 5                                // port
      + (ssdp_task_config->device_type ? strlen(ssdp_task_config->device_type)
                                       : 1) +
      (ssdp_task_config->friendly_name ? strlen(ssdp_task_config->friendly_name)
                                       : 1) +
      (ssdp_task_config->presentation_url
           ? strlen(ssdp_task_config->presentation_url)
           : 1) +
      (ssdp_task_config->serial_number ? strlen(ssdp_task_config->serial_number)
                                       : 1) +
      (ssdp_task_config->model_name ? strlen(ssdp_task_config->model_name)
                                    : 1) +
      (ssdp_task_config->model_description
           ? strlen(ssdp_task_config->model_description)
           : 1) +
      (ssdp_task_config->model_number ? strlen(ssdp_task_config->model_number)
                                      : 1) +
      (ssdp_task_config->model_url ? strlen(ssdp_task_config->model_url) : 1) +
      (ssdp_task_config->manufacturer_name
           ? strlen(ssdp_task_config->manufacturer_name)
           : 1) +
      (ssdp_task_config->manufacturer_url
           ? strlen(ssdp_task_config->manufacturer_url)
           : 1) +
      (ssdp_task_config->uuid ? strlen(ssdp_task_config->uuid) : 1) +
      (ssdp_task_config->services_description
           ? strlen(ssdp_task_config->services_description)
           : 1) +
      (ssdp_task_config->icons_description
           ? strlen(ssdp_task_config->icons_description)
           : 1);

  ssdp_task_config->schema = (char *)calloc(template_size + 1, sizeof(char));
  if (ssdp_task_config->schema) {
    if (sprintf(
            ssdp_task_config->schema, SSDP_SCHEMA_TEMPLATE, ssdp_get_LocalIP(),
            ssdp_task_config->port,
            ssdp_task_config->device_type ? ssdp_task_config->device_type : "",
            ssdp_task_config->friendly_name ? ssdp_task_config->friendly_name
                                            : "",
            ssdp_task_config->presentation_url
                ? ssdp_task_config->presentation_url
                : "",
            ssdp_task_config->serial_number ? ssdp_task_config->serial_number
                                            : "",
            ssdp_task_config->model_name ? ssdp_task_config->model_name : "",
            ssdp_task_config->model_description
                ? ssdp_task_config->model_description
                : "",
            ssdp_task_config->model_number ? ssdp_task_config->model_number
                                           : "",
            ssdp_task_config->model_url ? ssdp_task_config->model_url : "",
            ssdp_task_config->manufacturer_name
                ? ssdp_task_config->manufacturer_name
                : "",
            ssdp_task_config->manufacturer_url
                ? ssdp_task_config->manufacturer_url
                : "",
            ssdp_task_config->uuid ? ssdp_task_config->uuid : "",
            ssdp_task_config->services_description
                ? ssdp_task_config->services_description
                : "",
            ssdp_task_config->icons_description
                ? ssdp_task_config->icons_description
                : "") < 0) {
      ESP_LOGE(TAG, "sprintf error for schema");
      free(ssdp_task_config->schema);
      ssdp_task_config->schema = NULL;
    }
  } else {
    ESP_LOGE(TAG, "Memory allocation error for schema");
  }
  return ssdp_task_config->schema;
}
