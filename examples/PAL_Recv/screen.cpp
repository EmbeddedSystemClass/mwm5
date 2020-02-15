/* Copyright (C) 2020 Mono Wireless Inc. All Rights Reserved.  *
 * Released under MW-OSSLA-*J,*E (MONO WIRELESS OPEN SOURCE    *
 * SOFTWARE LICENSE AGREEMENT).                                */

#include <mwm5.h>

#include <Arduino.h>
#include <M5Stack.h>

#include "screen.hpp"

using namespace TWEUTILS;
using namespace TWESERCMD;
using namespace TWEARD;
using namespace TWE;


// lcd console defs
namespace TWEM5 {
	// top bar
	TWETerm_M5_Console the_screen_t(64, 1, { 0, 0, 320, 18 }, M5); // init the screen.

	// main screen
	TWETerm_M5_Console the_screen(64, 20, { 0, 18, 320, 240 - 30 - 18 }, M5); // init the screen.

	// bottom area (for debug information)
	TWETerm_M5_Console the_screen_b(64, 4, { 0, 18 + 192, 320, 20 }, M5); // init the screen.

	// bottom area (for debug information)
	TWETerm_M5_Console the_screen_c(64, 1, { 0, 18 + 192 + 20, 320, 10 }, M5); // init the screen.

	// default color
	uint16_t default_bg_color = 0;
	uint16_t default_fg_color = 0;

	void setup_screen() {
		// LCD font color
		default_bg_color = M5.Lcd.color565(90, 0, 50); //(185, 0, 103); // (232, 0, 129); // (255, 0, 142);
		default_fg_color = ALMOST_WHITE;

		// font register (note: to save flash area, don't create too much!)
		TWEFONT::createFontMP10(1, 0, 0); // MP10 font
		// TWEFONT::createFontLcd8x6(1, 0, 0); // LCD fonts (no Japanese)

		TWEFONT::createFontShinonome16(10, 0, 0, TWEFONT::U32_OPT_FONT_TATEBAI); // shinonome 16 font (TATE BAIKAKU)
		TWEFONT::createFontShinonome16(11, 1, 0); // shinonome 16 font
		TWEFONT::createFontMP12(12, 0, 0, TWEFONT::U32_OPT_FONT_TATEBAI | TWEFONT::U32_OPT_FONT_YOKOBAI); // MP10 font
		TWEFONT::createFontMP12(13, 0, 0); // MP10 font
		// TWEFONT::createFontShinonome16(12, 0, 0, TWEFONT::U32_OPT_FONT_YOKOBAI | TWEFONT::U32_OPT_FONT_TATEBAI); // shinonome 16 font (TATE/YOKO BAIKAKU)

		// main screen area
		the_screen.set_font(10);
		the_screen.set_color(default_fg_color, default_bg_color);
		the_screen.set_cursor(2); // 0: no 1: curosr 2: blink cursor
		the_screen.force_refresh();

		// bottom area
		the_screen_b.set_font(1);
		the_screen_b.set_color(M5.Lcd.color565(80, 80, 80), M5.Lcd.color565(20, 20, 20));
		the_screen_b.set_cursor(0); 
		the_screen_b.force_refresh();

		// bottom area
		the_screen_c.set_font(1);
		the_screen_c.set_color(ORANGE, M5.Lcd.color565(20, 20, 20));
		the_screen_c.set_cursor(0);
		the_screen_c.force_refresh();

		// top area
		the_screen_t.set_font(11);
		the_screen_t.set_color(default_bg_color, default_fg_color);
		the_screen_t.set_cursor(0);
		the_screen_t.force_refresh();
	}

	// screen refresh timing (every 32ms)
	void check_for_refresh() {
		static uint32_t u32mills;

		uint32_t u32now = millis();
		if (u32now - u32mills > 32) {
			TWEM5::the_screen.refresh();
			TWEM5::the_screen_b.refresh();
			TWEM5::the_screen_c.refresh();
			TWEM5::the_screen_t.refresh();

			u32mills = u32now;
		}
	}

	// change screen font
	void change_screen_font() {
		static uint8_t idx = 1;
		uint8_t modes[] = { 10, 11, 12, 13 }; // toggle screen 10 .. 13

		TWEM5::the_screen.set_font(modes[idx & 0x3]);
		TWEM5::the_screen.clear_screen();
		TWEM5::the_screen.force_refresh();
		
		auto font = TWEFONT::queryFont(TWEM5::the_screen.font_id());
		TWEM5::the_screen_b.clear_screen();
		TWE::fPrintf(TWEM5::the_screen_b, "\nFont: %s\n      ID=%d H:%d W:%d W_CHRs:%d",
			font.font_name, font.get_font_code(), font.height, font.width, font.font_wide_count);

		idx++;
	}

	// change screen color
	void change_screen_color() {
		static bool i = false;
		i = !i;

		if (i) {
			TWEM5::the_screen.set_color(ORANGE, M5.Lcd.color565(0x20, 0x20, 0x20));
			TWEM5::the_screen.clear_screen();
		}
		else {
			TWEM5::the_screen.set_color(TWEM5::default_fg_color, TWEM5::default_bg_color);
			TWEM5::the_screen.clear_screen();
		}

		TWEM5::the_screen.force_refresh();
	}
}