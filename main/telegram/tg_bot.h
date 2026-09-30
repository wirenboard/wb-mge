#pragma once

/*
 * The Telegram bot of the fingerprint access control, with the behaviour of the pp-lock
 * controller it replaces.
 *
 * One task long-polls getUpdates over HTTP(S) on the Bot API server tg_api_url and answers with
 * sendMessage. The server, the token (tg_token) and the admins (tg_admins: numeric Telegram user
 * ids separated by commas) are read from the settings on every poll, so a change applies to the
 * next poll without a restart; a change of server or token drops the pending updates, as a boot
 * does. An empty token leaves the bot idle. The first successful poll sends "Lock door controller
 * booted" to every admin.
 *
 * Commands:
 *   /activate [chat_id [name]]   admins: let a chat (default: this one) open the door
 *   /deactivate [chat_id]        admins: take that back
 *   /list, /reset                admins: list / forget every allowed chat
 *   /help                        what this sender may use
 *   open, Open                   open the door. A sender whose Telegram username belongs to a
 *                                user is decided by that user's own permission (enabled and
 *                                allow_tg), in any chat; anyone else only in an allowed chat.
 *                                Every attempt is reported as a Telegram event of fp_acs.
 */

#include <stdbool.h>
#include <stddef.h>

// The longest reason tg_bot_get_status() gives, NUL included.
#define TG_BOT_ERROR_MAX    48

// Starts the bot task. Call once, after network_init(); the task waits for a token and for the
// network by itself.
void tg_bot_start(void);

// The bot as GET /fp/status reports it. Callable from any task.
//   enabled  a token is set (tg_token not empty).
//   online   the last getUpdates got HTTP 200 with "ok": true, and no sendMessage since failed to
//            reach the server (one the server refuses does not count). False after boot and after
//            a change of server or token until a getUpdates succeeds, and whenever the bot is not
//            enabled. The bot takes a change up once the request under way and the pause after a
//            failed one - or, while no usable token is set, the 5 s idle wait - is over: until then
//            the status still describes the old settings.
//   error    why the last request failed ("ESP_ERR_HTTP_CONNECT", "HTTP 401", ...), or "invalid
//            token" for a token with characters no bot token has (it is never sent), cut to
//            error_size; empty when online, when not enabled, and when the token has only characters
//            a bot token may have and no request has failed since boot or the last change of server
//            or token.
void tg_bot_get_status(bool *enabled, bool *online, char *error, size_t error_size);
