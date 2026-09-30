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

// Starts the bot task. Call once, after network_init(); the task waits for a token and for the
// network by itself.
void tg_bot_start(void);
