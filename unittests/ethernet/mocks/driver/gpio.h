#pragma once

// ethernet.c only needs the GPIO numbers it puts into the EMAC and PHY configurations; the
// shared mock of this header lists just the pins the RS-485 and expander code use.
#include "soc/gpio_num.h"
