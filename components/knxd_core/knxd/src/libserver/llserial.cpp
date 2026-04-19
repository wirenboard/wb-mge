/*
    EIBD eib bus access and management daemon
    Copyright (C) 2005-2011 Martin Koegler <mkoegler@auto.tuwien.ac.at>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "llserial.h"
#include "config.h"

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#ifdef ESP_PLATFORM
#include "driver/uart.h"
#endif

#ifndef ESP_PLATFORM
#include <sys/ioctl.h>
#include <termios.h>
#ifdef HAVE_LINUX_LOWLATENCY
#include <cstring> // memcpy
#include <sys/ioctl.h>
#endif

static speed_t getbaud(int baud)
{
  switch(baud)
    {
    case 9600:
      return B9600;
    case 19200:
      return B19200;
    case 38400:
      return B38400;
    case 115200:
      return B115200;
    default:
      return 0;
    }
}
#endif /* !ESP_PLATFORM */

bool
LLserial::setup()
{
  if(!FDdriver::setup())
    return false;

  dev = cfg->value("device","");
  if(dev.size() == 0)
    {
      ERRORPRINTF (t, E_ERROR | 22, "Missing device= config");
      return false;
    }
  baudrate = cfg->value("baudrate", (int)default_baudrate());
#ifndef ESP_PLATFORM
  if (getbaud(baudrate) == 0)
    {
      ERRORPRINTF (t, E_ERROR | 66, "Wrong baudrate= config");
      return false;
    }
#endif
  low_latency = cfg->value("low-latency",false);

  return true;
}

void
LLserial::start()
{
#ifndef ESP_PLATFORM
  /* Linux: open serial device with termios configuration */
  struct termios t1;
  int term_baudrate;

  fd = open (dev.c_str(), O_RDWR | O_NOCTTY | O_NDELAY | O_SYNC);
  if (fd == -1)
    {
      ERRORPRINTF (t, E_ERROR | 67, "Opening %s failed: %s", dev, strerror(errno));
      goto ex1;
    }

  if (!set_low_latency (fd, &sold, low_latency))
    {
      ERRORPRINTF (t, E_ERROR | 68, "low_latency %s failed: %s", dev, strerror(errno));
      goto ex2;
    }

  close (fd);

  fd = open (dev.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
  if (fd == -1)
    {
      ERRORPRINTF (t, E_ERROR | 69, "Opening %s failed: %s", dev, strerror(errno));
      goto ex1;
    }

  if (tcgetattr (fd, &old))
    {
      ERRORPRINTF (t, E_ERROR | 24, "tcgetattr %s failed: %s", dev, strerror(errno));
      goto ex3;
    }

  if (tcgetattr (fd, &t1))
    {
      ERRORPRINTF (t, E_ERROR | 70, "tcgetattr %s failed: %s", dev, strerror(errno));
      goto ex3;
    }

  termios_settings(t1);
  term_baudrate = getbaud(baudrate);
  if (term_baudrate == -1)
    {
      ERRORPRINTF (t, E_ERROR | 71, "baudrate %d not recognized", baudrate);
      goto ex3;
    }
  TRACEPRINTF(t, 0, "Opened %s with baud %d", dev, baudrate);
  cfsetospeed (&t1, term_baudrate);
  cfsetispeed (&t1, 0);

  if (tcsetattr (fd, TCSAFLUSH, &t1))
    {
      ERRORPRINTF (t, E_ERROR | 26, "tcsetattr %s failed: %s", dev, strerror(errno));
      goto ex3;
    }

  TRACEPRINTF (t, 2, "Opened");
  FDdriver::start();
  return;

ex3:
  restore_low_latency (fd, &sold, low_latency);
ex2:
  close (fd);
  fd = -1;
ex1:
  stopped(true);

#else
  /* ESP32: UART already configured via ESP-IDF driver + VFS.
   * The device path is expected to be either:
   *   /dev/uart/N — opened by platform_uart_init()
   *   /dev/fd/N   — an already-opened file descriptor
   */
  if (dev.substr(0, 8) == "/dev/fd/") {
    /* Pre-opened fd from platform init */
    fd = std::stoi(dev.substr(8));
  } else {
    fd = open(dev.c_str(), O_RDWR | O_NONBLOCK);
  }

  if (fd < 0) {
    ERRORPRINTF (t, E_ERROR | 67, "Opening %s failed: %s", dev, strerror(errno));
    stopped(true);
    return;
  }

  TRACEPRINTF(t, 0, "Opened %s (ESP32 UART, baud %d)", dev, baudrate);
  FDdriver::start();
#endif
}

void
LLserial::stop(bool err)
{
#ifndef ESP_PLATFORM
  if (fd >= 0)
    restore_low_latency (fd, &sold, low_latency);
#endif
  FDdriver::stop(err);
}

int
LLserial::enable_input_parity_check()
{
#ifndef ESP_PLATFORM
  struct termios t1;

  TRACEPRINTF (t, 8, "Enabling input parity check on fd %d\n", fd);

  if (tcgetattr (fd, &t1))
  {
    ERRORPRINTF (t, E_ERROR | 70, "tcgetattr failed: %s", strerror(errno));
    return -1;
  }

  t1.c_iflag = t1.c_iflag | INPCK;

  if (tcsetattr (fd, TCSANOW, &t1))
  {
    ERRORPRINTF (t, E_ERROR | 70, "tcsetattr failed: %s", strerror(errno));
    return -2;
  }
#else
  /* ESP32: NCN5120 parity check is an L2 feature, not UART framing.
   * The UART stays 8N1. The NCN5120 checks parity on the KNX TP bus,
   * not on the UART interface. */
  TRACEPRINTF (t, 8, "Input parity check: NCN5120 handles L2 parity internally");
#endif

  return 0;
}
