#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include <esp_http_server.h>

// This module implements API handlers for internal production test purposes

esp_err_t wb_test_get_handler(httpd_req_t *req);
esp_err_t wb_test_post_handler(httpd_req_t *req);

/**
 * @brief True while the factory clock-out test owns the RS-485 hardware.
 *
 * It is raised before the test stops the Modbus slave and lowered only after the slave has
 * been brought back, so it covers the WHOLE run, not just the window in which the LEDC
 * waveform is up. For that run the test owns both TX lines (through the LEDC), both DE
 * lines (as plain GPIOs: port 1 raised, port 2 held LOW) and the RS-485 bus V-out, which it
 * forces on regardless of the stored setting.
 *
 * settings_update.c is what reads it: a settings apply that ran anyway would call
 * uart_set_pin() on pins the test is driving and revert V-out from NVS mid-measurement.
 * Nothing is lost by deferring, only postponed — the test's exit path re-reads every serial
 * parameter from NVS and calls update_rs485_control() itself.
 *
 * This flag says WHAT is going on, it does not by itself keep anyone out: raising it and
 * stopping the Modbus stack is one step under that stack's ownership lock
 * (mb_slave_lock_take() in the slave role, mb_master_lock_take() in the master one), and
 * settings_update_task reads it under the same lock, which is what stops an apply slipping
 * in between a check and the act it licenses.
 *
 * Safe to call from any task.
 */
bool wb_test_clock_out_active(void);
