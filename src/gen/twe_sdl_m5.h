#pragma once

/* Copyright (C) 2019-2020 Mono Wireless Inc. All Rights Reserved.
 * Released under MW-OSSLA-1J,1E (MONO WIRELESS OPEN SOURCE SOFTWARE LICENSE AGREEMENT). */

#if defined(_MSC_VER) || defined(__APPLE__) || defined(__linux) || defined(__MINGW32__)
#include "twe_common.hpp"
#include "esp32/generic_lcd_screen.hpp"
#include "twe_serial.hpp"
#include "serial_ftdi.hpp"

#if defined(_MSC_VER) || defined(__MINGW32__)
#include "../win/msc_term.hpp"
#elif defined(__APPLE__)
#include "../mac/osx_term.hpp"
#elif defined(__linux)
#include "../linux/linux_term.hpp"
#endif

extern void setup();
extern void loop();

extern TWEARD::M5Stack M5;
extern TWE::SerialFtdi Serial, Serial2;

namespace TWE {
	/**
	 * @brief	Writer object implementing IStereamOut to command terminal
	 */
	extern TWE_PutChar_CONIO WrtCon;

	/**
	 * @brief	Writer object implementing IStereamOut to Serial2=TWE.
	 */
	extern TWE::TWE_PutChar_Serial<TWE::SerialFtdi> WrtTWE;
}

#define SERIAL_8N1 0x06
#endif //WIN/MAC