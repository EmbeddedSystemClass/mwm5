﻿/* Copyright (C) 2019-2020 Mono Wireless Inc. All Rights Reserved.
 * Released under MW-OSSLA-1J,1E (MONO WIRELESS OPEN SOURCE SOFTWARE LICENSE AGREEMENT). */

#if (defined(_MSC_VER) || defined(__APPLE__) || defined(__linux) || defined(__MINGW32__))

 // version
#include "version_weak.h"

/*****************************************************************
 * CONFIGURATION
 *****************************************************************/
// output debug message
#ifdef _DEBUG
# define _DEBUG_MESSAGE
#endif


/*****************************************************************
 * HEADER FILES
 *****************************************************************/
#include <string>

#if defined(_MSC_VER) || defined(__MINGW32__)
#include <windows.h>
#include <conio.h>
#include <signal.h>
#elif defined(__APPLE__)
#include <signal.h>
#include <unistd.h>
#include <mach-o/dyld.h>
#include <limits.h>
#elif defined(__linux)
#include <signal.h>
#include <unistd.h>
#include <limits.h>
#endif

#include "mwm50.h"
#include "M5Stack.h"
#include "twe_sdl_m5.h"

#if defined(_MSC_VER) || defined(__MINGW32__)
#include "../win/msc_term.hpp"
#elif defined(__APPLE__)
#include "../mac/osx_term.hpp"
#elif defined(__linux)
#include "../linux/linux_term.hpp"
#endif

#include "modctrl_ftdi.hpp"
#include "serial_ftdi.hpp"
#include "esp32/esp32_lcd_color.h"

#include "twe_sys.hpp"

//Using SDL
#include "sdl2_common.h"
#include "sdl2_button.hpp"
#include "sdl2_keyb.hpp"
#include "sdl2_icon.h"

// include getopt.c
#include "../oss/oss_getopt.h"

/***********************************************************
 * PROTOTYPES
 ***********************************************************/
static void s_getopt(int argc, char* args[]);
static void s_init();
static void s_init_sdl();

static void s_sketch_setup();
static void s_sketch_loop();

static void exit_err(const char* msg, const char * msg_param = nullptr);
static void signalHandler( int signum );

#ifdef _DEBUG_MESSAGE
#define DBGOUT(...) fprintf(stderr, __VA_ARGS__) 
#else
#define DBGOUT(msg,...)
#endif

/***********************************************************
 * VARIABLES
 ***********************************************************/

// exit flag
bool g_quit_sdl_loop = false;
uint32_t g_sdl2_user_event_type = 0;

//The window we'll be rendering to
SDL_Window* gWindow = nullptr;

//The window renderer
SDL_Renderer* gRenderer = nullptr;

// Window icon
SDL_Surface *gSurface_icon_win = nullptr;
std::unique_ptr<uint32_t[]> g_pixdata_icon_win;

//Screen dimension constants
int SCREEN_WIDTH = 640;
int SCREEN_HEIGHT = 480;
int SCREEN_POS_X = 0;
int SCREEN_POS_Y = 0;

// SCREEN window size preset.
static const int screen_size_tbl[][2] = {
	{640,480}, {960,720}, {1280,720}, {1280,960}, {1920,1080}, {320,240}, {-1,-1}
};

// console
#if defined(_MSC_VER) || defined(__MINGW32__)
TWETerm_WinConsole con_screen(80, 24);
#elif defined(__APPLE__)
TWETerm_MacConsole con_screen(80, 24);
#elif defined(__linux)
TWETerm_LinuxConsole con_screen(80, 24);
#endif
ITerm& TWETERM::the_sys_console = con_screen; // global object reference of system console.
TWECUI::KeyInput TWE::the_sys_keyboard(512); // cosole keyboard buffer (mainly for debugging use)

TWE_GetChar_CONIO con_keyboard;
TWE_PutChar_CONIO TWE::WrtCon;

// The Serial Device
SerialFtdi Serial;
SerialFtdi Serial2;
TWE_PutChar_Serial<SerialFtdi> TWE::WrtTWE(Serial2);

// TWE BOOTLOADER PROTOCOL
TweModCtlFTDI obj_ftdi(Serial2);
TweProg TWE::twe_prog(new TweBlProtocol<TWE::SerialFtdi, TweModCtlFTDI>(Serial2, obj_ftdi));

// the M5 stack instance
static const int M5_LCD_WIDTH = 320;
static const int M5_LCD_HEIGHT = 240;
M5Stack M5(M5_LCD_WIDTH, M5_LCD_HEIGHT);

// settings from getopt
struct _gen_preference {
	int render_engine; // choose rendering option (osx Metal)
} the_pref;

/***********************************************************
 * ICON
 ***********************************************************/

/***********************************************************
 * IMPLEMENTATION
 ***********************************************************/

// procedure for generic operation
struct app_core_generic_procs {
	void module_reset() {
		if (Serial2.is_opened()) {
			Serial2.begin(115200);
			delay(10);
			twe_prog.reset_module();
		}
	}

	void type_plus3() {
		if (Serial2.is_opened()) {
			const uint8_t buf[] = {'+'};
			Serial2.write(buf, 1);
			delay(400);
			Serial2.write(buf, 1);
			delay(400);
			Serial2.write(buf, 1);
		}
	}

	void clip_copy_request() {
		the_clip.copy.request_to_copy();
	}

	void clip_paste() {
		the_clip.paste.past_from_clip();
	}

} the_generic_ops;

// class for SDL2 handling
struct app_core_sdl {
	static const int M5_LCD_SUB_WIDTH = 640;
	static const int M5_LCD_SUB_HEIGHT = 480;
	
	static const int M5_LCD_TEXTE_WIDTH = 320;
	static const int M5_LCD_TEXTE_HEIGHT = 32;

	static const int SDLOP_NEXT = -1;
	static const int SDLOP_PREV = -2;

	// Texture
	SDL_Texture* mTexture;
	SDL_Texture* mTexture_sub;
	SDL_Texture* mTexture_texte;

	int nAltDown;
	int nAltState;
	int nTextEditing;

	// main screen
	TWETerm_M5_Console sub_screen; // reinit_state the screen.
	TWETerm_M5_Console sub_screen_tr; // reinit_state the screen.
	TWETerm_M5_Console sub_screen_br; // reinit_state the screen.

	// key input display
	TWETerm_M5_Console sub_textediting;

	// button quit
	std::unique_ptr<twe_wid_button> sp_btn_quit;

	// button quit
	std::unique_ptr<twe_wid_button> sp_btn_A;

	// button quit
	std::unique_ptr<twe_wid_button> sp_btn_B;

	// button quit
	std::unique_ptr<twe_wid_button> sp_btn_C;

	// FULL SCREEN RENDERING OBJECT (mimic M5Stack)
	M5Stack M5_SUB;

	// Text Editing Object
	M5Stack M5_TEXTE;
	int _nTextEdtLen;
	
	// SCREEN render mode (0:scanline, 1:LCD like, 2:simple blur 3:blocky)
	int render_mode_m5_main;
	static const int render_mode_m5_main_maxval = 3;

	// fading out when quitting.
	int quit_loop_count;
	static const int QUIT_LOOP_COUNT_MAX = 32;

	// fullscreen
	int _bfullscr;
	int _nscrsiz;

	// constructor
	app_core_sdl()
		: mTexture(nullptr)
		, mTexture_sub(nullptr)
		, mTexture_texte(nullptr)
		, nAltDown(0)
		, nAltState(0)
		, sub_screen(80, 32, { 0, 0, M5_LCD_SUB_WIDTH / 2, M5_LCD_SUB_HEIGHT }, M5_SUB)
		, sub_screen_tr(80, 16, { M5_LCD_SUB_WIDTH / 2, 0, M5_LCD_SUB_WIDTH / 2, M5_LCD_SUB_HEIGHT / 2 }, M5_SUB)
		, sub_screen_br(80, 16, { M5_LCD_SUB_WIDTH / 2, M5_LCD_SUB_HEIGHT / 2, M5_LCD_SUB_WIDTH / 2, M5_LCD_SUB_HEIGHT / 2 }, M5_SUB)
		, sub_textediting(80, 1, { 0, 0, M5_LCD_TEXTE_WIDTH, M5_LCD_TEXTE_HEIGHT }, M5_TEXTE)
		, sp_btn_quit(new twe_wid_button(L"[閉]", { 640 - 64, 0, 64, 32 }))
		, sp_btn_A(new twe_wid_button(L"[  A  ]", { 4, 480 - 32, 208, 32 }))
		, sp_btn_B(new twe_wid_button(L"[  B  ]", { 4 + 208 + 4, 480 - 32, 208, 32 }))
		, sp_btn_C(new twe_wid_button(L"[  C  ]", { 4 + 208 + 4 + 208 + 4, 480 - 32, 208, 32 }))
		, M5_SUB(M5_LCD_SUB_WIDTH, M5_LCD_SUB_HEIGHT)
		, M5_TEXTE(M5_LCD_TEXTE_WIDTH, M5_LCD_TEXTE_HEIGHT)
		, render_mode_m5_main(0)
		, quit_loop_count(-1)
		, _bfullscr(0)
		, _nscrsiz(0)
		, _nTextEdtLen(0)
		, nTextEditing(0)
	{
	}

	// destructor
	~app_core_sdl() {
		SDL_DestroyTexture(mTexture);
		mTexture = NULL;

		SDL_DestroyTexture(mTexture_sub);
		mTexture_sub = NULL;

		SDL_DestroyTexture(mTexture_texte);
		mTexture_texte = NULL;

		sp_btn_A.release();
		sp_btn_B.release();
		sp_btn_C.release();
		sp_btn_quit.release();

		//Destroy window	
		SDL_FreeSurface(gSurface_icon_win);
		SDL_DestroyRenderer(gRenderer);
		SDL_DestroyWindow(gWindow);

		gWindow = NULL;
		gRenderer = NULL;

		//Quit SDL subsystems
		SDL_Quit();
	}

	/* SDL Initialize */
	void init_sdl() {
		// Texture
		mTexture = SDL_CreateTexture(gRenderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING,
			M5_LCD_WIDTH * 2, M5_LCD_HEIGHT * 2); // alloc double size (for optional rendering)
		if (mTexture == NULL)
			exit_err("SDL_CreateTexture()");

		// Texture
		mTexture_sub = SDL_CreateTexture(gRenderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING,
			M5_LCD_SUB_WIDTH, M5_LCD_SUB_HEIGHT);
		if (mTexture_sub == NULL)
			exit_err("SDL_CreateTexture()");

		// Texture of input area
		mTexture_texte = SDL_CreateTexture(gRenderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING,
			M5_LCD_TEXTE_WIDTH, M5_LCD_TEXTE_HEIGHT);
		if (mTexture_texte == NULL)
			exit_err("SDL_CreateTexture()");

		// enable text input
		SDL_StartTextInput();
		
		// set candidate window position other than (0,0)
		const SDL_Rect rctIME = { 100, 100, 100, 100 };
		SDL_SetTextInputRect((SDL_Rect*)&rctIME); 
	}

	// calculate screen position from 640,480 based screen to actual screen coordinates.
	inline int screen_weight(int i) {
		return i * SCREEN_WIDTH / 640;
	}

	void update_help_screen() {
		sub_screen_tr << "\033[2J";
		sub_screen_tr << "\033[31;1m[シリアルポート]\033[0m";
		
		#if defined(__APPLE__)
		# define STR_ALT "Cmd"
		#else
		# define STR_ALT "Alt"
		#endif
		
		if (!Serial2.is_opened()) sub_screen_tr << " - ｵﾌﾗｲﾝ";

		if (SerialFtdi::ser_count > 0) {
			for (int i = 0; i < SerialFtdi::ser_count; i++) {
				sub_screen_tr << crlf << printfmt(STR_ALT "+%d ", i + 1); // ALT+1 .. 3

				bool bOpened = false;
				if (!strncmp(SerialFtdi::ser_devname[i], Serial2.get_devname(), sizeof(SerialFtdi::ser_devname[i]))) {
					bOpened = true;
				}

				if (bOpened) {
					sub_screen_tr << "\033[33;1m";
				}

				sub_screen_tr << printfmt("%s(%s)%c"
					, SerialFtdi::ser_devname[i]
					, SerialFtdi::ser_desc[i],
					bOpened ? '*' : ' ');

				if (bOpened) {
					sub_screen_tr << "\033[0m";
				}
			}
		}
		sub_screen_tr << crlf << STR_ALT "+0 : 切断&再スキャン";

		static bool b_static_message = false;
		if (!b_static_message) {
			b_static_message = true;

			sub_screen << "[基本操作]";
			sub_screen << crlf << "ESC   : 戻る";
			sub_screen << crlf << "Enter/↑↓→← : 選択";

			sub_screen << crlf << crlf << "[TWELITE 操作]";
			sub_screen << crlf << STR_ALT"+I : + + + (ｲﾝﾀﾗｸﾃｨﾌﾞﾓｰﾄﾞ)";
			sub_screen << crlf << STR_ALT"+R : モジュールのリセット";

			sub_screen << crlf << crlf << "[ボタン操作]";
			sub_screen << crlf << STR_ALT"+A : Ａ(左)ボタン";
			sub_screen << crlf << STR_ALT"+S : Ｂ(中)ボタン";
			sub_screen << crlf << STR_ALT"+D : Ｃ(右)ボタン";
			sub_screen << crlf << "  Shift+" STR_ALT " 長押し";

			sub_screen << crlf << crlf << "[コピー＆ペースト]";
			sub_screen << crlf << STR_ALT"+C : ｸﾘｯﾌﾟﾎﾞｰﾄﾞへコピー";
			sub_screen << crlf << STR_ALT"+V : ｸﾘｯﾌﾟﾎﾞｰﾄﾞよりペースト";

			sub_screen << crlf << crlf << "[その他]";
			sub_screen << crlf << STR_ALT"+F : フルスクリーン";
			sub_screen << crlf << "  Shift+" STR_ALT " 可能なら更に拡大";
			sub_screen << crlf << STR_ALT"+G : 描画方法変更";
			sub_screen << crlf << STR_ALT"+J : ｳｲﾝﾄﾞｳｻｲｽﾞ変更";

			sub_screen << crlf;
			sub_screen << crlf << STR_ALT"+Q : 終了";
		}
	}

	/**
	 * @fn	void update_help_desc(const wchar_t* msg) 
	 *
	 * @brief	Put a message on the bottom left screen.
	 */
	void update_help_desc(const wchar_t* msg) {
		sub_screen_br << L"\033[2J\033[H" << msg;
	}

	/**
	 * @fn	void update_textebox(const char* msg)
	 *
	 * @brief	Updates the textebox described by msg
	 *
	 * @param	msg	The message.
	 */
	void update_textebox(const char* msg, bool IME = false) {
		_nTextEdtLen = (int)strlen(msg);

		if (IME) {
			sub_textediting << "\033[2J\033[HIME:\033[31m" << msg << "\033[0m";
		}
		else {
			_nTextEdtLen = 1;
			sub_textediting << "\033[2J\033[H" << msg;
		}
	}

	/**
	 * @fn	void recalc_viewport()
	 *
	 * @brief	Calc view position especially when full screen.
	 */
	void recalc_viewport(SDL_Window *window,
										SDL_Renderer *renderer,
										SDL_Rect *viewport) {
		Uint8 r, g, b, a;
		SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderClear(renderer);
		SDL_RenderPresent(renderer);
		SDL_SetRenderDrawColor(renderer, r, g, b, a);
		int w, h;
		SDL_GetWindowSize(window, &w, &h);

		switch (_bfullscr) {
		case 0:
			SCREEN_WIDTH = screen_size_tbl[_nscrsiz][0];
			SCREEN_HEIGHT = screen_size_tbl[_nscrsiz][1];
			viewport->w = SCREEN_WIDTH;
			viewport->h = SCREEN_HEIGHT;
			viewport->x = 0;
			viewport->y = 0;
			break;
		case 1:
			SCREEN_WIDTH = screen_size_tbl[_nscrsiz][0];
			SCREEN_HEIGHT = screen_size_tbl[_nscrsiz][1];
			viewport->w = SCREEN_WIDTH;
			viewport->h = SCREEN_HEIGHT;
			viewport->x = (w - viewport->w) / 2;
			viewport->y = (h - viewport->h) / 2;
			break;
		case 2:
			SCREEN_WIDTH = w;
			SCREEN_HEIGHT = h;
			viewport->w = SCREEN_WIDTH;
			viewport->h = SCREEN_HEIGHT;
			viewport->x = 0;
			viewport->y = 0;
			break;
		}

		#if defined(_DEBUG) && defined(_DEBUG_MESSAGE)
		fprintf(stderr, "\n{recalc_viewport: scr=(%d,%d)%c vp(%d,%d,%d,%d)}",
			w, h, _bfullscr ? 'F' : 'W',
			viewport->x, viewport->y, viewport->w, viewport->h 
		);
		#endif
		SDL_RenderSetViewport(renderer, viewport);
	}

	/**
	 * @fn	void refresh_entirescreen()
	 
	 * @brief	Refresh entire screen
	 */
	void refresh_entirescreen() {
		SDL_Rect rct;
		recalc_viewport(gWindow, gRenderer, &rct);
		SCREEN_POS_X = rct.x;
		SCREEN_POS_Y = rct.y;

		M5.Lcd.update_line_all();
		M5_SUB.Lcd.update_line_all();
		M5_TEXTE.Lcd.update_line_all();

		sp_btn_quit->redraw();
		sp_btn_A->redraw();
		sp_btn_B->redraw();
		sp_btn_C->redraw();
	}

	inline void draw_point(uint32_t *tgt, RGBA c, uint8_t lumi = 0xFF) {
		uint8_t* p = (uint8_t*)tgt;

		if (lumi == 0xff) {
			*(p + 0) = 0xff; // c.u8col[3];
			*(p + 1) = c.u8col[2];
			*(p + 2) = c.u8col[1];
			*(p + 3) = c.u8col[0];
		} else {
			*(p + 0) = 0xff; //((signed)c.u8col[3] * lumi) >> 8;
			*(p + 1) = ((signed)c.u8col[2] * lumi) >> 8;
			*(p + 2) = ((signed)c.u8col[1] * lumi) >> 8;
			*(p + 3) = ((signed)c.u8col[0] * lumi) >> 8;
		}
	}

	void init_sdl_sub() {
		// font set (Shinonome 16 dot, full chars set)
		TWEFONT::createFontShinonome16_full(0x80, 0, 0);
		TWEFONT::createFontShinonome16_full(0x81, 0, 0, TWEFONT::U32_OPT_FONT_TATEBAI);
		TWEFONT::createFontShinonome16_full(0x82, 0, 0, TWEFONT::U32_OPT_FONT_YOKOBAI);
		TWEFONT::createFontShinonome16_full(0x83, 0, 0, TWEFONT::U32_OPT_FONT_YOKOBAI | TWEFONT::U32_OPT_FONT_TATEBAI);

		// help screen (shown by Alt press)
		sub_screen.set_font(0x80);
		int cols = sub_screen.get_cols();
		sub_screen.set_font(0x80, cols - 2);
		sub_screen.set_color(WHITE, BLACK);
		sub_screen.set_cursor(0); // 0: no 1: curosr 2: blink cursor
		sub_screen.force_refresh();

		sub_screen_tr.set_font(0x80);
		cols = sub_screen_tr.get_cols();
		sub_screen_tr.set_font(0x80, cols - 2);
		sub_screen_tr.set_color(WHITE, color565(0x00, 0x00, 0x40));
		sub_screen_tr.set_cursor(0); // 0: no 1: curosr 2: blink cursor
		sub_screen_tr.force_refresh();

		sub_screen_br.set_font(0x80);
		cols = sub_screen_br.get_cols();
		sub_screen_br.set_font(0x81, cols - 2);
		sub_screen_br.set_color(WHITE, color565(0x00, 0x40, 0x00));
		sub_screen_br.set_cursor(0); // 0: no 1: curosr 2: blink cursor
		sub_screen_br.force_refresh();

		sub_textediting.set_font(0x83);
		sub_textediting.set_color(color565(0x80, 0x80, 0x80), WHITE);
		sub_textediting.set_cursor(0); // 0: no 1: curosr 2: blink cursor
		sub_textediting.force_refresh();

		// quit button
		sp_btn_quit->setup(gRenderer);
		sp_btn_A->setup(gRenderer);
		sp_btn_B->setup(gRenderer);
		sp_btn_C->setup(gRenderer);
	}

	bool sdlop_window_size(int n) {
		const int MAX_ENTRY = sizeof(screen_size_tbl) / sizeof(int) / 2 - 1;

		int next_mode;
		if (n == SDLOP_NEXT)
			next_mode = _nscrsiz + 1;
		else if (n == SDLOP_PREV)
			next_mode = _nscrsiz - 1;
		else
			next_mode = n;

		if (next_mode < 0) next_mode = MAX_ENTRY - 1;
		if (next_mode >= MAX_ENTRY) next_mode = 0;
		
		if (next_mode != _nscrsiz) {
			_nscrsiz = next_mode;

			if (screen_size_tbl[_nscrsiz][0] == -1) _nscrsiz = 0;

			SCREEN_WIDTH = screen_size_tbl[_nscrsiz][0];
			SCREEN_HEIGHT = screen_size_tbl[_nscrsiz][1];

			SDL_SetWindowSize(gWindow, SCREEN_WIDTH, SCREEN_HEIGHT);
			refresh_entirescreen();

			return true;
		}
		return false;
	}
	
	bool sdlop_render_mode(int n) {
		int next_mode;
		if (n == SDLOP_NEXT)
			next_mode = render_mode_m5_main + 1;
		else if (n == SDLOP_PREV)
			next_mode = render_mode_m5_main - 1;
		else
			next_mode = n;

		if (next_mode < 0) next_mode = render_mode_m5_main_maxval;
		if (next_mode > render_mode_m5_main_maxval) next_mode = 0;
		
		render_mode_m5_main = next_mode;
		refresh_entirescreen();

		return true;
	}
	
	void handle_sdl_event(SDL_Event&e) {
		//User requests quit
		if (e.type == SDL_QUIT)
		{
			g_quit_sdl_loop = true;
		}

		/* USER EVENT */
		if (e.type == SDL_USEREVENT) {
			if (IS_SDL2_USERCODE(e.user.code, SDL2_USERCODE_CHANGE_SCREEN_SIZE)) {
				sdlop_window_size(SDL2_USERCODE_GET_BYTE1(e.user.code));
			} else
			if (IS_SDL2_USERCODE(e.user.code, SDL2_USERCODE_CHANGE_SCREEN_RENDER)) {
				sdlop_render_mode(SDL2_USERCODE_GET_BYTE1(e.user.code));
			}
			return;
		}

		/* EXPOSURE EVENT, REQUIRE ENTIRE REDRAW */
		if (e.type == SDL_WINDOWEVENT) {
			if (e.window.event == SDL_WINDOWEVENT_EXPOSED) {
				refresh_entirescreen();
			}
		}

		/* HANDLE BUTTON PRESS EVENT */
		if (sp_btn_quit->update(e)) {
			if (sp_btn_quit->available()) {
				auto readstate = sp_btn_quit->read();
				g_quit_sdl_loop = true;
				return;
			}
		}

		if (sp_btn_A->update(e)) {
			if (sp_btn_A->available()) {
				auto readstate = sp_btn_A->read();
				if (readstate == 1) M5.BtnA._press = true;
				if (readstate == 2) M5.BtnA._lpress = true;
			}
		}

		if (sp_btn_B->update(e)) {
			if (sp_btn_B->available()) {
				auto readstate = sp_btn_B->read();
				if (readstate == 1) M5.BtnB._press = true;
				if (readstate == 2) M5.BtnB._lpress = true;
			}
		}

		if (sp_btn_C->update(e)) {
			if (sp_btn_C->available()) {
				auto readstate = sp_btn_C->read();
				if (readstate == 1) M5.BtnC._press = true;
				if (readstate == 2) M5.BtnC._lpress = true;
			}
		}

		/* MOUSE MOTION */
		if (e.type == SDL_MOUSEMOTION) {
			int d = screen_weight(32);

			if (e.motion.x < d && e.motion.y) {
				if (nAltDown != 1) {
					nAltDown = 2;
				}
			}
			else {
				if (nAltDown == 2) {
					nAltDown = -7;
				}
			}
		}

		// just test
		if (e.type == SDL_TEXTEDITING) {
			if (e.text.text[0] == 0) {
				nTextEditing = -31;
			} else {
				nTextEditing = 1;
			}
			update_textebox(e.text.text, true);
		}

		if (e.type == SDL_TEXTINPUT) {
			// normal keyinput
			// the_keyboard_sdl2.handle_event(e);
			if (!nAltState && the_keyboard_sdl2.handle_event(e)) {
				nTextEditing = -31;

				update_textebox(e.text.text);
				return;
			}
		}

		// handle KP_
		if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_KP_ENTER) {
			the_keyboard_sdl2.handle_event(e);
		}

		if (e.type == SDL_DROPFILE)	{
			char* str_dir;

			str_dir = e.drop.file;
#if 0
			SDL_ShowSimpleMessageBox(
				SDL_MESSAGEBOX_INFORMATION,
				"File dropped on window",
				str_dir,
				gWindow
			);
#endif
			the_file_drop.new_drop(e.drop.file);

			SDL_free(str_dir);
			return;
		}
		
		if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
			// normal keyinput
			if (the_keyboard_sdl2.handle_event(e)) {
				return;
			}

			if (e.key.keysym.mod & (KMOD_STG)) {
				if (
#ifdef __APPLE__
					   e.key.keysym.scancode == SDL_SCANCODE_LGUI
					|| e.key.keysym.scancode == SDL_SCANCODE_RGUI
#else
					   e.key.keysym.scancode == SDL_SCANCODE_LALT
					|| e.key.keysym.scancode == SDL_SCANCODE_RALT
#endif
					) {
					nAltDown = 1;
					nAltState = 1;
					update_help_desc(L"");
				}
			}

			bool bhandled = false;
			switch (e.key.keysym.scancode) {
			case SDL_SCANCODE_1:
			case SDL_SCANCODE_2:
			case SDL_SCANCODE_3:
			case SDL_SCANCODE_4:
			case SDL_SCANCODE_5:
			case SDL_SCANCODE_6:
			case SDL_SCANCODE_7:
			case SDL_SCANCODE_8:
				if (e.key.keysym.mod & (KMOD_STG)) {
					if (e.type == SDL_KEYDOWN) {
						update_help_desc(L"シリアルポートをオープンします");
					} else {
						int n = e.key.keysym.scancode - SDL_SCANCODE_1;
						if (n >= 0 && n < SerialFtdi::ser_count) {
							Serial2.close();
							Serial2.open(SerialFtdi::ser_devname[n]);
						}
						
						bhandled = true;
					}
				}
				break;
			case SDL_SCANCODE_0:
				if (e.key.keysym.mod & (KMOD_STG)) {
					if (e.type == SDL_KEYDOWN) {
						update_help_desc(L"シリアルポートをクローズします");
					} else {
						Serial2.close();
						
						SerialFtdi::list_devices();
						update_help_screen();
						
						bhandled = false;
					}
				}
				break;

			case SDL_SCANCODE_C:
				if (e.key.keysym.mod & (KMOD_STG)) {
					if (e.type == SDL_KEYDOWN) {
						update_help_desc(L"画面文字列をクリップボードにコピーします");
					}
					else {
						the_generic_ops.clip_copy_request();
						bhandled = true;
					}
				}
				break;

			case SDL_SCANCODE_V:
				if (e.key.keysym.mod & (KMOD_STG)) {
					if (e.type == SDL_KEYDOWN) {
						update_help_desc(L"クリップボードの文字列をペーストします");
					}
					else {
						the_generic_ops.clip_paste();
						bhandled = true;
					}
				}
				break;

			case SDL_SCANCODE_A:
				if (e.key.keysym.mod & (KMOD_STG)) {
					if (e.type == SDL_KEYDOWN) {
						update_help_desc(L"ボタンAを入力します");
					} else {
						if (e.key.keysym.mod & (KMOD_SHIFT))
							M5.BtnA._lpress = true;
						else
							M5.BtnA._press = true;

						bhandled = true;
					}
				}
				break;
			case SDL_SCANCODE_S:
				if (e.key.keysym.mod & (KMOD_STG)) {
					if (e.type == SDL_KEYDOWN) {
						update_help_desc(L"ボタンBを入力します");
					} else {
						if (e.key.keysym.mod & (KMOD_SHIFT))
							M5.BtnB._lpress = true;
						else
							M5.BtnB._press = true;

						bhandled = true;
					}
				}
				break;
			case SDL_SCANCODE_D:
				if (e.key.keysym.mod & (KMOD_STG)) {
					if (e.type == SDL_KEYDOWN) {
						update_help_desc(L"ボタンCを入力します");
					} else {
						if (e.key.keysym.mod & (KMOD_SHIFT))
							M5.BtnC._lpress = true;
						else
							M5.BtnC._press = true;
							
						bhandled = true;
					}
				}
				break;

			case SDL_SCANCODE_R:
				if (e.key.keysym.mod & (KMOD_STG)) {
					if (e.type == SDL_KEYDOWN) {
						update_help_desc(L"TWELITE無線モジュールのハードウェアリセットをします");
					} else {
						the_generic_ops.module_reset();
						bhandled = true;
					}
				}
				break;

			case SDL_SCANCODE_RETURN:
			case SDL_SCANCODE_F:
				if (e.key.keysym.mod & (KMOD_STG)) {
					if (e.type == SDL_KEYDOWN) {
						update_help_desc(L"フルスクリーン画面に設定します。Shift+で、スクリーン最大に拡大します");
					} else {
						_bfullscr = !_bfullscr;

						if (_bfullscr && e.key.keysym.mod & (KMOD_SHIFT)) _bfullscr = 2;

						if (_bfullscr) {
							SDL_SetWindowFullscreen(gWindow, SDL_WINDOW_FULLSCREEN);
						} else {
							SDL_SetWindowFullscreen(gWindow, 0);
						}

						refresh_entirescreen();
						bhandled = true;
					}
				}
				break;

			case SDL_SCANCODE_I:
			case SDL_SCANCODE_P:
				if (e.key.keysym.mod & (KMOD_STG)) {
					if (e.type == SDL_KEYDOWN) {
						update_help_desc(L"TWELITE無線モジュールに + + + を入力します");
					} else {
						the_generic_ops.type_plus3();
						
						bhandled = true;
					}
				}
				break;

			case SDL_SCANCODE_J:
				if (e.key.keysym.mod & (KMOD_STG)) {
					if (e.type == SDL_KEYDOWN) {
						update_help_desc(L"画面サイズを変更します。"
						                 L"キーの入力ごとに640x480/960x720/1280x720/1920x1080/320x240の順で変更します");

					} else {
						sdlop_window_size(SDLOP_NEXT);

						bhandled = true;
					}
				}
				break;

			case SDL_SCANCODE_G:
				if (e.key.keysym.mod & (KMOD_STG)) {
					if (e.type == SDL_KEYDOWN) {
						update_help_desc(L"画面の描画方式を変更します。キーを押すたびにLCD風/CRT風/ぼやけ/ブロックの順で切り替えます");
					} else {
						sdlop_render_mode(SDLOP_NEXT);
						bhandled = true;
					}
				}
				break;

			case SDL_SCANCODE_Q:
				if (e.key.keysym.mod & (KMOD_STG)) {
					if (e.type == SDL_KEYDOWN) {
						update_help_desc(L"TWELITE STAGEの終演です。ごきげんよう");
					} else {
						if (_bfullscr) {
							// exit wiht fullscr will cause hang up! (SDL2.0.12, OSX)
							SDL_SetWindowFullscreen(gWindow, 0);
							_bfullscr = 0;
						}
						
						g_quit_sdl_loop = true;
						bhandled = true;
					}
				}
				break;

			default:
				break;
			}

			if (bhandled && nAltDown > 0) nAltDown = -7;
		}

		if (e.type == SDL_KEYUP) {
			if (!(e.key.keysym.mod & (KMOD_STG))) {
				nAltState = 0;
				if(nAltDown > 0) nAltDown = -7;
			}
		}
	}

	void render_main_screen() {
		// texture source rect
		const SDL_Rect* p_srcrect = NULL;

		// the start of texure update by memory update.
		void* mPixels;
		int mPitch;
		SDL_LockTexture( mTexture, NULL, &mPixels, &mPitch );
			
		if (render_mode_m5_main == 0) {
			static const SDL_Rect srcrect = { 0, 0, M5_LCD_WIDTH * 2, M5_LCD_HEIGHT * 2 };
			p_srcrect = &srcrect;

			for (int y = 0; y < M5_LCD_HEIGHT; y++) {
				if (M5.Lcd.update_line(y)) {
					uint32_t* p1 = (uint32_t*)mPixels + (y * M5_LCD_WIDTH * 4);
					uint32_t* p2 = p1 + (M5_LCD_WIDTH * 2);

					for (int x = 0; x < M5_LCD_WIDTH; x++) {
						auto c = M5.Lcd.get_pt(x, y);

						// RENDER LIKE LCD
						draw_point(p1, c);
						draw_point(p1+1, c, 192);
						draw_point(p2, c, 128);
						draw_point(p2+1, c, 128);

						p1 += 2;
						p2 += 2;
					}
				}
			}
		} else if (render_mode_m5_main == 1) {
			static const SDL_Rect srcrect = { 0, 0, M5_LCD_WIDTH, M5_LCD_HEIGHT * 2 };
			p_srcrect = &srcrect;

			for (int y = 0; y < M5_LCD_HEIGHT; y++) {
				if (M5.Lcd.update_line(y)) {
					uint32_t* p1 = (uint32_t*)mPixels + (y * M5_LCD_WIDTH * 4);
					uint32_t* p2 = p1 + (M5_LCD_WIDTH * 2);

					for (int x = 0; x < M5_LCD_WIDTH; x++) {
						RGBA c = M5.Lcd.get_pt(x, y);
						
						draw_point(p1, c);
						draw_point(p2, c, 128);

						p1 += 1;
						p2 += 1;
					}
				}
			}
		} else if (render_mode_m5_main == 2) {
			static const SDL_Rect srcrect = { 0, 0, M5_LCD_WIDTH, M5_LCD_HEIGHT };
			p_srcrect = &srcrect;

			for (int y = 0; y < M5_LCD_HEIGHT; y++) {
				if (M5.Lcd.update_line(y)) {
					uint32_t* p1 = (uint32_t*)mPixels + (y * M5_LCD_WIDTH * 2);
					uint32_t* p2 = p1 + (M5_LCD_WIDTH * 1);

					for (int x = 0; x < M5_LCD_WIDTH; x++) {
						auto c = M5.Lcd.get_pt(x, y);

						// RENDER BLUR
						draw_point(p1, c);
						draw_point(p2, c);

						p1 += 1;
						p2 += 1;
					}
				}
			}

		} else
		if (render_mode_m5_main == 3) {
			static const SDL_Rect srcrect = { 0, 0, M5_LCD_WIDTH * 2, M5_LCD_HEIGHT * 2 };
			p_srcrect = &srcrect;
								
			for (int y = 0; y < M5_LCD_HEIGHT; y++) {
				if (M5.Lcd.update_line(y)) {
					uint32_t* p1 = (uint32_t*)mPixels + (y * M5_LCD_WIDTH * 4);
					uint32_t* p2 = p1 + (M5_LCD_WIDTH * 2);

					for (int x = 0; x < M5_LCD_WIDTH; x++) {
						auto c = M5.Lcd.get_pt(x, y);

						// RENDER LIKE DIGITAL TEXTURE
						draw_point(p1, c);
						draw_point(p1+1, c);
						draw_point(p2, c);
						draw_point(p2+1, c);

						p1 += 2;
						p2 += 2;
					}
				}
			}
		}

		// the end of texture update by memory update
		SDL_UnlockTexture(mTexture);

		//Reset render target
		SDL_SetRenderTarget(gRenderer, nullptr);

		//Show rendered to texture
		const SDL_Rect dstrect = { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT };

		if (quit_loop_count >= 0) {
			uint8 alpha = quit_loop_count * 255 / QUIT_LOOP_COUNT_MAX;
			SDL_SetTextureColorMod(mTexture, alpha, alpha, alpha);
		} else {
			SDL_SetTextureColorMod(mTexture, 0xFF, 0xFF, 0xFF);
		}
		SDL_RenderCopy(gRenderer, mTexture, p_srcrect, &dstrect);
	}

	void render_texte_box() {
		if (nTextEditing && _nTextEdtLen > 0) {
			SDL_Rect dstrect_sub = { 0, 0, M5_LCD_TEXTE_WIDTH, M5_LCD_TEXTE_HEIGHT };
			SDL_Rect srcrect_sub = { 0, 0, M5_LCD_TEXTE_WIDTH, M5_LCD_TEXTE_HEIGHT };

			if (_nTextEdtLen == 1) {
				dstrect_sub.w = 16;
				srcrect_sub.w = 16;
			}

			// the start of texure update by memory update.
			void* mPixels;
			int mPitch;
			SDL_LockTexture(mTexture_texte, NULL, &mPixels, &mPitch);
			for (int y = 0; y < M5_LCD_TEXTE_HEIGHT; y++) {
				if (M5_TEXTE.Lcd.update_line(y)) {
					uint32_t* p1 = (uint32_t*)mPixels + (y * M5_LCD_TEXTE_WIDTH);

					for (int x = 0; x < M5_LCD_TEXTE_WIDTH; x++) {
						auto c = M5_TEXTE.Lcd.get_pt(x, y);
						draw_point(p1, c);
						p1++;
					}
				}
			}
			SDL_UnlockTexture(mTexture_texte);

			uint8_t alpha = (nTextEditing < 0) ? (-nTextEditing * 0xc0) / 32 : 0xc0;

			SDL_SetRenderTarget(gRenderer, NULL);
			SDL_SetTextureBlendMode(mTexture_texte, SDL_BLENDMODE_BLEND);
			SDL_SetTextureAlphaMod(mTexture_texte, alpha);
			SDL_RenderCopy(gRenderer, mTexture_texte, &srcrect_sub, &dstrect_sub);

			if (nTextEditing < 0) {
				nTextEditing++;
				if (nTextEditing == 0) _nTextEdtLen = 0; // set to 0
			}
		}
	}

	void render_help_screen() {
		if (nAltDown) {
			const SDL_Rect dstrect_sub = { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT };
			const SDL_Rect srcrect_sub = { 0, 0, M5_LCD_SUB_WIDTH, M5_LCD_SUB_HEIGHT };
			
			// the start of texure update by memory update.
			void* mPixels;
			int mPitch;
			SDL_LockTexture( mTexture_sub, NULL, &mPixels, &mPitch );
			for (int y = 0; y < M5_LCD_SUB_HEIGHT; y++) {
				if (M5_SUB.Lcd.update_line(y)) {
					uint32_t* p1 = (uint32_t*)mPixels + (y * M5_LCD_SUB_WIDTH);

					for (int x = 0; x < M5_LCD_SUB_WIDTH; x++) {
						auto c = M5_SUB.Lcd.get_pt(x, y);

						draw_point(p1, c);
						p1++;
					}
				}
			}
			SDL_UnlockTexture(mTexture_sub);

			uint8_t alpha = (nAltDown < 0) ? (-nAltDown * 0xc0) / 8 : 0xc0;

			SDL_SetRenderTarget(gRenderer, NULL);
			SDL_SetTextureBlendMode(mTexture_sub, SDL_BLENDMODE_BLEND);
			SDL_SetTextureAlphaMod(mTexture_sub, alpha);
			SDL_RenderCopy(gRenderer, mTexture_sub, &srcrect_sub, &dstrect_sub);

			if (nAltDown < 0) nAltDown++;
		}
	}

	void setup() {
		init_sdl();
		init_sdl_sub();
	}

	void loop() {

		SDL_Event e;

		SDL_Point screenCenter = { SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 };

		while (g_quit_sdl_loop == false || quit_loop_count > 0) {
			//Event handler
			while (SDL_PollEvent(&e) != 0) {
				handle_sdl_event(e);	
			}

			// SKETCH WORKS
			::s_sketch_loop();

			// Update Alt Screen	
			static FT_HANDLE ser2handle = (FT_HANDLE)(-1);
			if (Serial2.get_handle() != ser2handle) {
				ser2handle = Serial2.get_handle();
				update_help_screen();
			}
			sub_screen.refresh(); // update sub screen
			sub_screen_tr.refresh();
			sub_screen_br.refresh();
			sub_textediting.refresh();

			// clear back margin
			if (_bfullscr > 0 && (SCREEN_POS_X != 0 || SCREEN_POS_Y != 0)) {
				Uint8 r, g, b, a;
				SDL_GetRenderDrawColor(gRenderer, &r, &g, &b, &a);
				SDL_SetRenderDrawColor(gRenderer, 0, 0, 0, 255);
				SDL_RenderClear(gRenderer);
				SDL_SetRenderDrawColor(gRenderer, r, g, b, a);
			}

			// render M5stack area
			render_main_screen();
			
			/* RENDER ALT SCREEN (HELP, SOME OPERATION) */
			render_help_screen();

			// TEXTEDITING box
			render_texte_box();
			
			/* RENDER BOTTONS */
			sp_btn_A->render_sdl(gRenderer);
			sp_btn_B->render_sdl(gRenderer);
			sp_btn_C->render_sdl(gRenderer);

			// BUTTON QUIT
			sp_btn_quit->render_sdl(gRenderer);

			// Update screen (may wait here to VSYNC)
			SDL_RenderPresent(gRenderer);

			if (quit_loop_count == -1 && g_quit_sdl_loop) {
				quit_loop_count = QUIT_LOOP_COUNT_MAX;
				
				// exitting message
				con_screen << crlf << "exiting";
			}
			else if (quit_loop_count >= 0) {
				con_screen << '.';
				con_screen.refresh();

				if (quit_loop_count > 0) quit_loop_count--;
			}
		}	
	}
} the_app_core;

/**
 * @fn	int exit_err(const char* msg, const char * msg_param = NULL)
 *
 * @brief	Exit with an error
 *
 * @param	msg		 	The message.
 * @param	msg_param	(Optional) The message parameter.
 *
 * @returns	An int.
 */
static void exit_err(const char* msg, const char * msg_param) {
	fprintf(stderr, "ERROR: ");
	fprintf(stderr, msg, msg_param);
	fprintf(stderr, "\r\n");

#ifdef __APPLE__
	_exit(1);
#else
	exit(1);
#endif
}

// handle terminal Ctrl+C
static void signalHandler( int signum ) {  
	if (signum == SIGINT 
#if defined(__APPLE__) || defined(__linux)
		|| signum == SIGQUIT
#endif	
		) {
		g_quit_sdl_loop = true;
	}
	// fprintf(stderr, "signal handled = %d\n", signum);
}

// initialize
static void s_init() {
	// set dir
	the_cwd.begin();
#ifndef _DEBUG
	the_cwd.change_dir(the_cwd.get_dir_exe());
#endif

	// DLL delay loading (only for VC++)
#if defined(_MSC_VER)  // || defined(__MINGW32__) // not for MINGW32, because /DELAYLOAD is not supported.
	SetDllDirectoryW(make_full_path(the_cwd.get_dir_exe(), L"dll").c_str()); // DLL directory, needs to specify /DELAYLOAD (VC++).
#endif

	// capture Ctrl+C on the console
	signal(SIGINT, signalHandler);
#if defined(__APPLE__) || defined(__linux)
	signal(SIGQUIT, signalHandler);
#endif

	// the OS dependent initialize
	TWESYS::SysInit();
	
	// prepare serial port
	SerialFtdi::list_devices();
}

static void s_init_sdl() {
	SDL_SetMainReady();

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) < 0) {
		exit_err("SDL_Init() [%s]");
	}

	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

	switch (the_pref.render_engine) {
	case 1:
		SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
		DBGOUT("\nSDL_HINT_RENDER_DRIVER=opengl");
		break;
	case 2:
#if defined(_MSC_VER) || defined(__MINGW32__)
		SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d");
		DBGOUT("\nSDL_HINT_RENDER_DRIVER=direct3d");
#elif defined(__APPLE__)
		SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
		DBGOUT("\nSDL_HINT_RENDER_DRIVER=metal");
#elif defined(__linux)
		DBGOUT("\nSDL_HINT_RENDER_DRIVER is not set");
#endif
		break;
	case 3:
		SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
		DBGOUT("\nSDL_HINT_RENDER_DRIVER=software");
		break;
	default:
		DBGOUT("\nSDL_HINT_RENDER_DRIVER is not set");
	}

	char title[256];
	snprintf(title, 256, "%s (v%d-%d-%d)", MWM5_APP_NAME, MWM5_APP_VERSION_MAIN, MWM5_APP_VERSION_SUB, MWM5_APP_VERSION_VAR);
	DBGOUT("\nTILTLE=%s",title);

	// Create Main window.
	gWindow = SDL_CreateWindow(title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 
										SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN); // | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	if (gWindow == NULL)
		exit_err("SDL_CreateWindow()");

	// system icon
#if defined(_MSC_VER) || defined(__MINGW32__)
	// for Windows, choose 32x32 for Window ICON
	g_pixdata_icon_win.reset(new uint32_t[32 * 32]);
	prepare_icon_data(g_pixdata_icon_win.get(), 32, 32, pix_icon_win_32);
	gSurface_icon_win = SDL_CreateRGBSurfaceFrom(g_pixdata_icon_win.get(), 32, 32, 32, 32 * 4, 0xFF000000, 0xFF0000, 0xFF00, 0xFF);

	if (gSurface_icon_win == NULL) exit_err("SDL_CreateRGBSurfaceFrom()");
	SDL_SetWindowIcon(gWindow, gSurface_icon_win);
#elif defined(__APPLE__) || defined(__linux)
	// for Linux/osx choose 128x128.
	g_pixdata_icon_win.reset(new uint32_t[128 * 128]);
	prepare_icon_data(g_pixdata_icon_win.get(), 128, 128, pix_icon_win_128);
	gSurface_icon_win = SDL_CreateRGBSurfaceFrom(g_pixdata_icon_win.get(), 128, 128, 32, 128 * 4, 0xFF000000, 0xFF0000, 0xFF00, 0xFF);

	if (gSurface_icon_win == NULL) exit_err("SDL_CreateRGBSurfaceFrom()");
	SDL_SetWindowIcon(gWindow, gSurface_icon_win);
#endif

	// Renderer
	gRenderer = SDL_CreateRenderer(gWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

	if (gRenderer == NULL)
		exit_err("SDL_CreateRenderer()");

	// user events
	if ((g_sdl2_user_event_type = SDL_RegisterEvents(1)) == -1) {
		exit_err("SDL_RegisterEvents()");
	}
}

static void s_sketch_setup() {
	// external setup() procedure
	::setup();
}

static void s_sketch_loop() {
	// update tick counter
	u32TickCount_ms = TWESYS::u32GetTick_ms();

	// get serial2 buffer
	int nSer2 = Serial2.update();

	// console test
	if (!twe_prog.is_protocol_busy()) {
		// handle serial input from TWE
		if (nSer2 >= 1) {
			for (int i = 0; i < nSer2; i++) {
				con_screen << char_t(Serial2._get_last_buf(i));
			}
		}

		// handle console input
		while (1) {
			int c = con_keyboard.get_a_byte();
			if (c == -1) break;

#if 0 && defined(__APPLE__)
			// APPLE TERMINAL will input latin char when pressed Alt+?.
			// con_screen << printfmt("[%02X]", c); // debug
			// ALT+R
			if (c == 0xC2) {
				c = con_keyboard.get_a_byte();
				if (c == 0xAE) {
					con_screen << crlf << "opt+R pressed, reset module";
					con_screen.force_refresh();
					
					if (Serial2.is_opened()) {
						the_generic_ops.module_reset();
					}	
				}
				c = -1;
			}
			
			// Alt+P
			if (c == 0xCF) {
				c = con_keyboard.get_a_byte();
				if (c == 0x80) {
					if (Serial2.is_opened()) {
						con_screen << crlf << "opt+C pressed, + + +";
						con_screen.force_refresh();

						the_generic_ops.type_plus3();
					}
				}
				c = -1;
			}

			// Alt+X
			if (c == 0xE2) {
				c = con_keyboard.get_a_byte();
				if (c == 0x89) {
					c = con_keyboard.get_a_byte();
					if (c == 0x88) {
						con_screen << crlf << "opt+X pressed, ";
						con_screen.force_refresh();
						g_quit_sdl_loop = true;
					}
				}
				c = -1;
			}
#endif
			if (c >= 0) the_sys_keyboard.push(c & 0xFF); // WrtTWE << char_t(c & 0xFF);
		}

		con_screen.refresh();

		::loop(); // external loop()
	} else {
		// is in bootloader protocol.
		// too make it faster, process a bulk of command at one time, otherwise it's affected by VSYNC wait.
		static int ct = 0; // bulk process counter
		
		// more loop
		do {
			if (Serial2.update() > 0) ct++;
			::loop();

			if (ct >= 4) { // process 4 commands at every loop.
				ct = 0;
				break;
			}
		} while (twe_prog.is_protocol_busy());
	}	
}

/**
 * @fn	static void s_getopt(int argc, char* args[])
 *
 * @brief	getopts using oss_getopt() (simple implementation of getopt())
 *
 * @param 		  	argc	The argc.
 * @param [in,out]	args	If non-null, the arguments.
 */
static void s_getopt(int argc, char* args[]) {
	memset(&the_pref, 0, sizeof(the_pref));
	
	int opt = 0;
	ts_opt_getopt* popt = oss_getopt_ref();

    while ((opt = oss_getopt(argc, args, "nR:")) != -1) {
        switch (opt) {
        case 'n': // single arg
            break;
        case 'R': // Render engine (0:default 1:opengl 2:metal)
            the_pref.render_engine = atoi(popt->optarg);
            break;
        default: /* '?' */
            fprintf(stderr, "Usage: %s [-t nsecs] [-n] name\n",
                    args[0]);
            exit(EXIT_FAILURE);
        }
    }
}

/**
 * @fn	void push_window_event(int32_t code, void* data1, void* data2)
 *
 * @brief	Pushes a window event
 *
 * @param 		  	code 	The code.
 * @param [in,out]	data1	If non-null, the first data.
 * @param [in,out]	data2	If non-null, the second data.
 */
void push_window_event(int32_t code, void* data1, void* data2) {
    SDL_Event event;
    SDL_memset(&event, 0, sizeof(event)); /* or SDL_zero(event) */
    event.type = g_sdl2_user_event_type;
    event.user.code = code;
    event.user.data1 = data1;
    event.user.data2 = data2;
    SDL_PushEvent(&event);
}

/**
 * @fn	int TWESYS::Get_Logical_CPU_COUNT()
 *
 * @brief	Gets logical CPU count
 *
 * @returns	The logical CPU count.
 */
int TWESYS::Get_Logical_CPU_COUNT() {
	return SDL_GetCPUCount();
}

/**
 * @fn	int main(int argc, char* args[])
 *
 * @brief	Main entry-point for this application
 *
 * @param	argc	The number of command-line arguments provided.
 * @param	args	An array of command-line argument strings.
 *
 * @returns	Exit-code for the process - 0 for success, else an error code.
 */
int main(int argc, char* args[]) {
	// check command line agrs
	s_getopt(argc, args);

	printf("\033[2J\033[H");

	// initialize
	s_init();
	s_init_sdl();
	
	// console setup
	con_screen.setup();
	con_screen << printfmt("*** TWELITE STAGE (v%d-%d-%d) ***", MWM5_APP_VERSION_MAIN, MWM5_APP_VERSION_SUB, MWM5_APP_VERSION_VAR) << crlf;

	// init SDL instance
	the_app_core.setup();

	// call sketch setup();
	s_sketch_setup();

	// SDL MainLoop
	the_app_core.loop();

	// on exit 
	con_screen.close_term(); // shall take the screen back before calling _exit().

#if defined(__APPLE__) || defined(__linux)
	int apiret = system("clear"); (void)apiret;

	// force terminate...
	_exit(0); // terminate here to pass rest of clean up process. (may not open crash report dialogue.)
#endif

	return 0;
}

#endif // WIN/MAC