/*
 * THIS FILE IS WHERE A DIY USER PUTS THEIR OWN LOGIC.
 *
 * This is the master-role twin of main/mb_slave/user_app.c: the firmware was built with
 * MB_ROLE=master, so the device drives both RS-485 buses instead of answering them.
 * mb_master.c is plumbing - the two RTU master instances, the UART pins, the RS-485
 * direction control. This file is the application on top of it.
 *
 * What it starts here is the slip mixer: a Modbus RTU master that drives a T13-750W-12-H
 * frequency converter through a program of steps. The engine lives in main/mixer/ and is
 * three pieces -
 *     program.c  - the step list and the base64 setting it is stored in,
 *     vfd_t13.c  - the drive, on top of the three calls mb_master.h offers,
 *     mixer.c    - the task that walks the steps and publishes what it is doing,
 * and mixer_start() below is the whole of the wiring between them and the firmware.
 *
 * The contract that engine keeps with the Modbus master is the one this file used to
 * demonstrate with an example poll loop, and it is still worth stating:
 *
 *   - poll from a task of YOUR OWN. mb_master_read_holding() and its two siblings block
 *     until the slave answers or the request times out, so the waiting happens on your task
 *     and on nobody else's;
 *   - a device that is not there comes back as ESP_ERR_TIMEOUT, distinct from every other
 *     failure. Poll loops live or die on telling "silent" apart from "broken";
 *   - nothing stops two tasks polling the same bus: mb_master.c holds one mutex per port,
 *     so concurrent requests queue instead of interleaving.
 *
 * The three calls available are declared in mb_master.h:
 *     mb_master_read_holding (port, slave_id, addr, count, out)
 *     mb_master_read_input   (port, slave_id, addr, count, out)
 *     mb_master_write_holding(port, slave_id, addr, value)
 * with port 0 = RS-485 port 1 and port 1 = RS-485 port 2.
 */

#include "user_app.h"

#include "mixer.h"

void user_app_start(void)
{
    mixer_start();
}
