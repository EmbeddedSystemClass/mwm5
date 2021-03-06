﻿/* Copyright (C) 2019-2020 Mono Wireless Inc. All Rights Reserved.
 * Released under MW-OSSLA-1J,1E (MONO WIRELESS OPEN SOURCE SOFTWARE LICENSE AGREEMENT). */

#include <mwm5.h>
#include <M5Stack.h>

#include "App_RootMenu.hpp"
#include "App_Twelite.hpp"
#include "App_PAL.hpp"
#include "App_Glancer.hpp"
#include "App_FirmProg.hpp"
#include "App_Interactive.hpp"
#include "App_Console.hpp"
#include "App_Settings.hpp"

#ifdef ESP32
# define IDF_UART
# include <EEPROM.h>
# include <driver/dac.h>
# include <PS2Keyboard.h> // https://www.pjrc.com/teensy/td_libs_PS2Keyboard.html
#elif defined(_MSC_VER) || defined(__APPLE__) || defined(__linux) || defined(__MINGW32__)
#include "App_SelectPort.hpp"
#endif

#include "common.h"

static int s_change_app(TWE::APP_MGR& the_app, int n_appsel, int prev_app, int exit_id);
static void s_check_serial();
static void s_check_other_input();

#ifndef ESP32
static void s_check_clipboard();
#endif

void setup() {
	// init lcd, serial, not init sd card
	M5.begin(true, true, true, true); // bool LCDEnable, bool SDEnable, bool SerialEnable, bool I2CEnable

	// Power Control
	M5.Power.begin(); // power control enabled

#ifdef ESP32
	// shut up the speaker!
	dac_output_disable(DAC_CHANNEL_1);
	ledcDetachPin(25);

	// set Digital pins
	pinMode(2, INPUT_PULLUP);

	// EEPROM
	EEPROM.begin(4096);

#else
	the_clip.begin(); // setup clipboard.
#endif

	// this procedure should follow after M5.begin();
#ifdef IDF_UART
	Serial2_IDF.begin(115200, 1024, 512);
#else
	Serial2.begin(115200, SERIAL_8N1, 16, 17);
	Serial2.setRxBufferSize(1536);
#endif

	// the preferences 
	the_settings_menu.begin(0);

#ifndef ESP32
	push_window_event(
		SDL2_USERCODE_CREATE_BYTE(SDL2_USERCODE_CHANGE_SCREEN_SIZE, sAppData.u8_TWESTG_STAGE_SCREEN_MODE >> 4),
								  nullptr, nullptr);
	push_window_event(
		SDL2_USERCODE_CREATE_BYTE(SDL2_USERCODE_CHANGE_SCREEN_RENDER, sAppData.u8_TWESTG_STAGE_SCREEN_MODE & 0xF),
								  nullptr, nullptr);
#endif

	// init the keyboard
	{
		void* keymap = nullptr;
#ifdef ESP32
		if (sAppData.u8_TWESTG_STAGE_KEYBOARD_LAYOUT == 0) keymap = (void*)&PS2Keymap_US;
		if (sAppData.u8_TWESTG_STAGE_KEYBOARD_LAYOUT == 1) keymap = (void*)&PS2Keymap_JP;
#endif
		the_keyboard.setup(keymap);
	}

	// setup uart input queue (reserve 512bytes)
	the_uart_queue.setup(512);

	// init the firmware programmer.
	twe_prog.setup();

	// launch menu app
	the_app.setup(s_change_app);

#if defined(ESP32)
	the_app.new_app<App_RootMenu>();
#else
	the_app.new_app<App_SelectPort>(int(E_APP_ID::SELECT_PORT)); // firstly, load the FTDI port selection.
#endif
}

// the main loop.
void loop() {
	// update M5
	M5.update();

	// update serial queue
	s_check_serial();

	// update hardware button
	s_check_other_input();

	// update keyboard
	the_keyboard.update();

	// call app's loop()
	the_app.loop();

#ifndef ESP32
	// clipboard check
	s_check_clipboard();
#endif
}

/**
 * @fn	static int s_change_app(TWE::APP_MGR& the_app, int n, int prev_app, int exit_id)
 *
 * @brief	Change application
 *
 * @param [in,out]	the_app 	The application management class object.
 * @param 		  	n_appsel	The requested app id.
 * @param 		  	prev_app	The previous(exiting) app id.
 * @param 		  	exit_id 	The exit code of the previous(exiting) app.
 *
 * @returns	        app id of switched app.
 */
static int s_change_app(TWE::APP_MGR& the_app, int n_appsel, int prev_app, int exit_id) {
	switch (n_appsel) {
	case App_TweLite::APP_ID:
		the_app.new_app<App_TweLite>();
		break;

	case App_PAL::APP_ID:
		the_app.new_app<App_PAL>();
		break;
		
	case App_Glancer::APP_ID:
		the_app.new_app<App_Glancer>();
		break;


	case App_FirmProg::APP_ID:
		the_app.new_app<App_FirmProg>();
		break;

	case App_Interactive::APP_ID:
		the_app.new_app<App_Interactive>();
		break;

	case App_Console::APP_ID:
		the_app.new_app<App_Console>();
		break;

	case App_Settings::APP_ID:
		the_app.new_app<App_Settings>();
		break;

#if defined(_MSC_VER) || defined(__APPLE__) || defined(__linux) || defined(__MINGW32__)
	case App_SelectPort::APP_ID:
		the_app.new_app<App_SelectPort>(prev_app);
		break;
#endif

	case APP_MGR::NEXT_APP_DEFAULT:
	case App_RootMenu::APP_ID: // should be 0
	default:
		the_app.new_app<App_RootMenu>(exit_id);

		return 0;
		break;
	}

	return n_appsel;
}

const wchar_t* query_app_launch_message(int n_appsel) {
	switch (n_appsel) {
	case App_TweLite::APP_ID:
		return App_TweLite::LAUNCH_MSG;
		break;

	case App_PAL::APP_ID:
		return App_PAL::LAUNCH_MSG;
		break;

	case App_Glancer::APP_ID:
		return App_Glancer::LAUNCH_MSG;
		break;

	case App_Console::APP_ID:
		return App_Console::LAUNCH_MSG;
		break;

	default:
		return L"";
	}
}

static void s_check_serial() {
	while(the_sys_keyboard.available()) {
		int c = the_sys_keyboard.get_a_byte();
		if (c >= 0) {
#ifndef ESP32
#if 1

			// sys console key input -> TWELITE via Serial2
			WrtTWE << char_t(c);
#else
			// sys console key input -> input buffer of Serial2
			the_uart_queue.push(c);
#endif
#else
			// for ESP32, system message will displayed on Serial.
			; // just discard it!
#endif
		}
	}
	
#ifdef IDF_UART
	// UART2 : connected to TWE
	while (Serial2_IDF.available()) {
		int c = Serial2_IDF.read();
		if (c >= 0) the_uart_queue.push(c);
	}
#else
	// UART2 : connected to TWE
	while (Serial2.available()) {
		int c = Serial2.read();
		if (c >= 0) the_uart_queue.push(c);
	}
#endif
}

static void s_check_other_input() {
	// button operation
	if (M5.BtnA.wasReleased()) {
		the_keyboard.push(TWECUI::KeyInput::KEY_BUTTON_A);
	}
	else if (M5.BtnB.wasReleased()) {
		the_keyboard.push(TWECUI::KeyInput::KEY_BUTTON_B);
	}
	else if (M5.BtnC.wasReleased()) {
		the_keyboard.push(TWECUI::KeyInput::KEY_BUTTON_C);
	}
	else if (M5.BtnA.wasReleasefor(700)) {
		the_keyboard.push(TWECUI::KeyInput::KEY_BUTTON_A_LONG);
	}
	else if (M5.BtnB.wasReleasefor(700)) {
		the_keyboard.push(TWECUI::KeyInput::KEY_BUTTON_B_LONG);
	}
	else if (M5.BtnC.wasReleasefor(700)) {
		the_keyboard.push(TWECUI::KeyInput::KEY_BUTTON_C_LONG);
	}
}

#ifndef ESP32
static void s_check_clipboard() {
	// clip board
	TWEUTILS::Unicode_UTF8Converter uc;
	while (the_clip.paste.available()) {
		int c = the_clip.paste.read();
		wchar_t u = (c != -1) ? uc(uint8_t(c)) : 0;

		// limited to ASCII chars
		if (u <= 0xFF) {
			the_keyboard.push(c);
		}
	}

	// check clip board copy request
	if (the_clip.copy.available()) {
		auto p = the_app.query_appobj();
		if (p) {
			ITerm* ptrm = reinterpret_cast<ITerm*>(p->get_appobj());

			if (ptrm) {
				ITerm& trm = *ptrm;

				SmplBuf_ByteSL<3072> l_buff;

				trm >> l_buff;
				the_clip.copy.copy_to_clip((const char*)l_buff.c_str());
			}
		}
	}
}
#endif