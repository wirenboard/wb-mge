#pragma once

// This module implements a simple call sequence counter
// to make it possible to check the order of function calls
// in the unittests

unsigned call_sequence_get_call_id(void);
