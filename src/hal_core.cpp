#include <SDL2/SDL.h>
#include <SDL2/SDL_clipboard.h>
#include <SDL2/SDL_rwops.h>
#include <array>
#include <string>
#ifdef __ANDROID__
#include <jni.h>
#endif

#include "hal_core.h"
#include "hal_palette.h"

#include "config.h"
#include "crypt.h"
#include "log.h"

static SDL_Window* sdlWin = nullptr;
static SDL_Renderer* sdlRen = nullptr;
static SDL_Texture* sdlTex = nullptr;
static SDL_PixelFormat* sdlPixFmt = nullptr;
static SDL_Joystick* joystick = nullptr;
static int screenWidth = config::INIT_SCREEN_WIDTH;
static int screenHeight = config::INIT_SCREEN_HEIGHT;

static std::array<pixel_t, 256> original_palette;
static std::array<pixel_t, 256> palette;

static bool debug_trace_state = false;
static bool reload_requested = false;

static void throw_error(std::string msg) {
	msg += SDL_GetError();
	throw(gfx_exception(msg));
}

void SYSLOG_LogMessage(const char* msg) {
	SDL_Log("%s", msg);
}

void GFX_Init(int x, int y) {
	TraceFunction();

	debug_trace_state = false;

	int init_flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO;
#ifndef TAC08_NO_JOYSTICK
	init_flags = init_flags | SDL_INIT_JOYSTICK;
#endif

	if (SDL_Init(init_flags) != 0) {
		throw_error("SDL_Init Error: ");
	}

	int window_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
#ifdef TAC08_FULL_SCREEN
	window_flags = window_flags | SDL_WINDOW_FULLSCREEN;
#endif

	sdlWin = SDL_CreateWindow("tac08", 100, 100, x, y, window_flags);
	if (sdlWin == nullptr) {
		throw_error("SDL_CreateWindow Error: ");
	}

	sdlRen = SDL_CreateRenderer(sdlWin, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (sdlRen == nullptr) {
		throw_error("SDL_CreateRenderer Error: ");
	}
	SDL_ShowCursor(SDL_DISABLE);

	int joystick_index = -1;

	for (int n = 0; n < SDL_NumJoysticks(); n++) {
		SDL_Joystick* js = SDL_JoystickOpen(n);

		int numbuttons = SDL_JoystickNumButtons(js);
		if (joystick_index == -1 && numbuttons > 1) {
			joystick_index = n;
		}

		logr << "Joystick: " << n;
		logr << "  name: " << SDL_JoystickName(js);
		logr << "  buttons: " << SDL_JoystickNumButtons(js);
		logr << "  axis: " << SDL_JoystickNumAxes(js);
		logr << "  hats: " << SDL_JoystickNumHats(js);
		SDL_JoystickClose(js);
	}

	if (joystick_index >= 0) {
		joystick = SDL_JoystickOpen(joystick_index);
		if (joystick) {
			logr << "Opened joystick " << joystick_index;
		}
	}

	int num = SDL_GetNumTouchDevices();
	logr << "num touch devices: " << num;
}

void GFX_End() {
	TraceFunction();
	if (sdlRen) {
		SDL_DestroyRenderer(sdlRen);
	}
	if (sdlWin) {
		SDL_DestroyWindow(sdlWin);
	}
	if (sdlPixFmt) {
		SDL_FreeFormat(sdlPixFmt);
	}
	if (sdlTex) {
		SDL_DestroyTexture(sdlTex);
	}
	SDL_Quit();
}

void GFX_ToggleFullScreen() {
#ifndef __ANDROID__
	bool fullNow = (SDL_GetWindowFlags(sdlWin) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
	SDL_SetWindowFullscreen(sdlWin, fullNow ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
#endif
}

void GFX_SetFullScreen(bool fullscreen) {
#ifndef __ANDROID__
	SDL_SetWindowFullscreen(sdlWin, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
#endif
}

pixel_t GFX_GetPixel(uint8_t r, uint8_t g, uint8_t b) {
	return (pixel_t)SDL_MapRGB(sdlPixFmt, r, g, b);
}

void GFX_CreateBackBuffer(int x, int y) {
	TraceFunction();
	screenWidth = x;
	screenHeight = y;

	sdlTex = SDL_CreateTexture(sdlRen, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
	                           config::MAX_SCREEN_WIDTH, config::MAX_SCREEN_HEIGHT);
	if (sdlTex == nullptr) {
		throw_error("SDL_CreateTexture Error: ");
	}

	sdlPixFmt = SDL_AllocFormat(SDL_PIXELFORMAT_RGB565);

	GFX_SelectPalette("pico8");
	GFX_RestorePalette();
}

void GFX_SetBackBufferSize(int x, int y) {
	screenWidth = x;
	screenHeight = y;
}

void GFX_SelectPalette(const std::string& name) {
	auto pal = GFX_GetPaletteInfo(name);

	for (size_t i = 0; i < pal.size; i++) {
		auto p = pal.pal[i];
		pixel_t pix = GFX_GetPixel((p >> 16) & 0xff, (p >> 8) & 0xff, p & 0xff);
		original_palette[i] = pix;
		palette[i] = pix;
	}
}

void GFX_RestorePalette() {
	palette = original_palette;
}

void GFX_RestorePaletteIndex(uint8_t i) {
	palette[i] = original_palette[i];
}

void GFX_SetPaletteIndex(uint8_t i, uint8_t r, uint8_t g, uint8_t b) {
	palette[i] = GFX_GetPixel(r, g, b);
}

void GFX_CopyBackBuffer(uint8_t* buffer, int buffer_w, int buffer_h) {
	pixel_t* pixels;
	int pitch;

	SDL_Rect r = {0, 0, buffer_w, buffer_h};

	int res = SDL_LockTexture(sdlTex, &r, (void**)&pixels, &pitch);
	if (res < 0) {
		throw_error("SDL_LockTexture Error: ");
	}
	for (int y = 0; y < buffer_h; y++) {
		for (int x = 0; x < buffer_w; x++) {
			pixels[x] = palette[buffer[x]];
			x++;
			pixels[x] = palette[buffer[x]];
		}
		pixels += pitch / sizeof(pixel_t);
		buffer += buffer_w;
	}

	SDL_UnlockTexture(sdlTex);
}

void GFX_ShowHWMouse(bool show) {
	SDL_ShowCursor(show);
}

void GFX_GetDisplayArea(int* w, int* h) {
	SDL_GetRendererOutputSize(sdlRen, w, h);
}

static SDL_Rect getDisplayArea(SDL_Window* win, double* scale = nullptr) {
	int winx, winy;
	SDL_GetWindowSize(win, &winx, &winy);

	SDL_Rect r = {0, 0, winx, winy};
	double xscale = (double)winx / (double)screenWidth;
	double yscale = (double)winy / (double)screenHeight;

	if (xscale * screenHeight > winy) {
		r.w = (int)(yscale * screenWidth);
		r.x = (int)(winx / 2 - r.w / 2);
		if (scale)
			*scale = yscale;
	} else {
		r.h = (int)(xscale * screenHeight);
		r.y = (int)(winy / 2 - r.h / 2);
		if (scale)
			*scale = xscale;
	}
	return r;
}

void GFX_Flip() {
	SDL_RenderClear(sdlRen);
	SDL_Rect dr = getDisplayArea(sdlWin);
	SDL_Rect sr = {0, 0, screenWidth, screenHeight};
	SDL_RenderCopy(sdlRen, sdlTex, &sr, &dr);
	SDL_RenderPresent(sdlRen);
}

static uint8_t keyState = 0;
static uint8_t joyState = 0;
static uint8_t hatState = 0;
static uint8_t simState = 0;
static int mouseWheel = 0;

static inline void set_state_bit(uint8_t& state, uint8_t bit, bool condition, bool value) {
	if (condition) {
		if (value) {
			state |= (1 << bit);
		} else {
			state &= ~(1 << bit);
		}
	}
}

static std::array<TouchInfo, 8> touchState;

bool INP_TouchAvailable() {
	return SDL_GetNumTouchDevices() > 0;
}

uint8_t INP_GetTouchMask() {
	uint8_t mask = 0;
	for (size_t n = 0; n < touchState.size(); n++) {
		if (touchState[n].state != TouchInfo::None) {
			mask |= (1 << n);
		}
	}
	return mask;
}

TouchInfo INP_GetTouchInfo(int idx) {
	if (idx < 0 && idx >= (int)touchState.size()) {
		return TouchInfo{};
	} else {
		return touchState[idx];
	}
}

static void flushTouchEvents() {
	for (TouchInfo& t : touchState) {
		if (t.state & TouchInfo::JustPressed) {
			t.state &= (~TouchInfo::JustPressed);
		}

		if (t.state & TouchInfo::JustReleased) {
			t.state = TouchInfo::None;
		}
	}
}

static void scaleMouse(int& x, int& y);

static void processTouchEvent(const SDL_TouchFingerEvent& ev) {
	if (ev.fingerId < (int)touchState.size()) {
		TouchInfo& ti = touchState[(int)ev.fingerId];

		int winx, winy;
		SDL_GetWindowSize(sdlWin, &winx, &winy);

		ti.x = (int)(ev.x * winx);
		ti.y = (int)(ev.y * winy);
		scaleMouse(ti.x, ti.y);

		if (ev.type == SDL_FINGERDOWN) {
			ti.state |= (TouchInfo::JustPressed | TouchInfo::Pressed);
		}
		if (ev.type == SDL_FINGERMOTION) {
			ti.state |= TouchInfo::Pressed;
		}
		if (ev.type == SDL_FINGERUP) {
			ti.state |= TouchInfo::JustReleased;
		}
	}

	// logr << "touch: x=" << ev.x << " y=" << ev.y << " fid=" << ev.fingerId << " tid=" <<
	// ev.touchId;
}

bool INP_ProcessInputEvents(const SDL_Event& ev) {
	if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F11) {
		GFX_ToggleFullScreen();
		return true;
	}
	if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_q && (ev.key.keysym.mod & KMOD_CTRL)) {
		return false;
	}
	if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_t && (ev.key.keysym.mod & KMOD_CTRL)) {
		DEBUG_Trace(!DEBUG_Trace());
		return true;
	}
	if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_r && (ev.key.keysym.mod & KMOD_CTRL)) {
		reload_requested = true;
		return true;
	}
	if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
		set_state_bit(keyState, 0, ev.key.keysym.sym == SDLK_LEFT, ev.type == SDL_KEYDOWN);
		set_state_bit(keyState, 1, ev.key.keysym.sym == SDLK_RIGHT, ev.type == SDL_KEYDOWN);
		set_state_bit(keyState, 2, ev.key.keysym.sym == SDLK_UP, ev.type == SDL_KEYDOWN);
		set_state_bit(keyState, 3, ev.key.keysym.sym == SDLK_DOWN, ev.type == SDL_KEYDOWN);
		set_state_bit(keyState, 4, ev.key.keysym.sym == SDLK_z, ev.type == SDL_KEYDOWN);
		set_state_bit(keyState, 5, ev.key.keysym.sym == SDLK_x, ev.type == SDL_KEYDOWN);
		set_state_bit(keyState, 6, ev.key.keysym.sym == SDLK_p, ev.type == SDL_KEYDOWN);
		set_state_bit(keyState, 6, ev.key.keysym.sym == SDLK_RETURN, ev.type == SDL_KEYDOWN);
		set_state_bit(keyState, 7, ev.key.keysym.sym == SDLK_ESCAPE, ev.type == SDL_KEYDOWN);
	} else if (ev.type == SDL_MOUSEWHEEL) {
		mouseWheel += ev.wheel.y;
	} else if (ev.type == SDL_JOYAXISMOTION) {
		// logr << "axis: " << (int)ev.jaxis.axis << "=" << ev.jaxis.value;
		set_state_bit(joyState, 0, ev.jaxis.axis == 0, ev.jaxis.value < -1500);
		set_state_bit(joyState, 1, ev.jaxis.axis == 0, ev.jaxis.value > 1500);
		set_state_bit(joyState, 2, ev.jaxis.axis == 1, ev.jaxis.value < -1500);
		set_state_bit(joyState, 3, ev.jaxis.axis == 1, ev.jaxis.value > 1500);
	} else if (ev.type == SDL_JOYHATMOTION) {
		// logr << "hat: " << (int)ev.jhat.hat << "=" << (int)SDL_JoystickGetHat(joystick, 0);
		auto hatval = SDL_JoystickGetHat(joystick, 0);
		set_state_bit(hatState, 0, true, hatval & SDL_HAT_LEFT);
		set_state_bit(hatState, 1, true, hatval & SDL_HAT_RIGHT);
		set_state_bit(hatState, 2, true, hatval & SDL_HAT_UP);
		set_state_bit(hatState, 3, true, hatval & SDL_HAT_DOWN);

	} else if (ev.type == SDL_JOYBUTTONDOWN || ev.type == SDL_JOYBUTTONUP) {
		// logr << "btn: " << (int)ev.jbutton.button << "=" << (bool)ev.jbutton.state;
		set_state_bit(joyState, 4, ev.jbutton.button == 1, (bool)ev.jbutton.state);
		set_state_bit(joyState, 5, ev.jbutton.button == 0, (bool)ev.jbutton.state);
		set_state_bit(joyState, 6, ev.jbutton.button == 7, (bool)ev.jbutton.state);
	} else if (ev.type == SDL_FINGERDOWN || ev.type == SDL_FINGERMOTION ||
	           ev.type == SDL_FINGERUP) {
		processTouchEvent(ev.tfinger);
	}
	return true;
}

bool EVT_ProcessEvents() {
	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		if (e.type == SDL_QUIT) {
			return false;
		} else {
			return INP_ProcessInputEvents(e);
		}
	}
	return true;
}

uint8_t INP_GetInputState() {
	return keyState | joyState | hatState | simState;
}

void INP_SetSimState(uint8_t state) {
	simState = state;
}

uint32_t TIME_GetTime_ms() {
	return SDL_GetTicks();
}

uint32_t TIME_GetElapsedTime_ms(uint32_t start) {
	return SDL_GetTicks() - start;
}

uint64_t TIME_GetProfileTime() {
	return SDL_GetPerformanceCounter();
}

uint64_t TIME_GetElapsedProfileTime_us(uint64_t start) {
	uint64_t now = SDL_GetPerformanceCounter();
	return ((now - start) * 1000000) / SDL_GetPerformanceFrequency();
}

uint64_t TIME_GetElapsedProfileTime_ms(uint64_t start) {
	uint64_t now = SDL_GetPerformanceCounter();
	return ((now - start) * 1000) / SDL_GetPerformanceFrequency();
}

void TIME_Sleep(int ms) {
	SDL_Delay(ms);
}

static void scaleMouse(int& x, int& y) {
	double scale;
	SDL_Rect r = getDisplayArea(sdlWin, &scale);
	x -= r.x;
	y -= r.y;
	x = (int)(x / scale);
	y = (int)(y / scale);
}

MouseState INP_GetMouseState() {
	MouseState ms;

	int b = SDL_GetMouseState(&ms.x, &ms.y);
	scaleMouse(ms.x, ms.y);

	ms.buttons = (b & SDL_BUTTON(SDL_BUTTON_LEFT)) ? 1 : 0;
	ms.buttons |= (b & SDL_BUTTON(SDL_BUTTON_RIGHT)) ? 2 : 0;
	ms.buttons |= (b & SDL_BUTTON(SDL_BUTTON_MIDDLE)) ? 4 : 0;

	ms.wheel = mouseWheel;
	mouseWheel = 0;
	return ms;
}

std::string FILE_LoadFile(std::string name) {
	logr << "loading file: " << name;
	std::string data;
	SDL_RWops* file = SDL_RWFromFile(name.c_str(), "r");
	if (file) {
		size_t sz = (size_t)SDL_RWsize(file);
		if (sz) {
			data.resize(sz, ' ');
			SDL_RWread(file, &data[0], sz, 1);
			logr << "  " << sz << " bytes loaded";

			SDL_RWclose(file);
		}
	}
	decrypt(data);
	return data;
}

std::string FILE_LoadGameState(std::string name) {
	const char* path = SDL_GetPrefPath("0xcafed00d", "tac08");
	name = std::string(path) + name;
	SDL_free((void*)path);

	return FILE_LoadFile(name);
}

void FILE_SaveGameState(std::string name, std::string data) {
	encrypt(data);

	const char* path = SDL_GetPrefPath("0xcafed00d", "tac08");
	name = std::string(path) + name;
	SDL_free((void*)path);

	logr << "writing file: " << name << " bytes: " << data.length();

	SDL_RWops* file = SDL_RWFromFile(name.c_str(), "w");
	if (file) {
		SDL_RWwrite(file, data.c_str(), data.length(), 1);
		SDL_RWclose(file);
		logr << "    file writen ";
	}
}

std::string FILE_ReadClip() {
	std::string res;
	if (SDL_HasClipboardText()) {
		auto t = SDL_GetClipboardText();
		if (t) {
			res = t;
			SDL_free(t);
		}
	}
	return res;
}

void FILE_WriteClip(const std::string& data) {
	SDL_SetClipboardText(data.c_str());
}

std::string FILE_GetDefaultCartName() {
	const char* val = SDL_GetHint("TAC08_DEFAULT_CART_NAME");
	if (val == nullptr) {
		return "cart.p8";
	}
	return val;
}

void HAL_StartFrame() {
	simState = 0;
	reload_requested = false;
}

void HAL_EndFrame() {
	flushTouchEvents();
}

void PLATFORM_OpenURL(std::string url) {
#ifdef __ANDROID__
	JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
	jobject activity = (jobject)SDL_AndroidGetActivity();
	jclass clazz(env->GetObjectClass(activity));
	jmethodID method_id = env->GetMethodID(clazz, "openURL", "(Ljava/lang/String;)V");

	if (method_id) {
		jstring jurl = env->NewStringUTF(url.c_str());
		env->CallVoidMethod(activity, method_id, jurl);
	} else {
		logr << "SDLActivity.openURL not found";
	}

	env->DeleteLocalRef(activity);
	env->DeleteLocalRef(clazz);
#endif
}

bool DEBUG_Trace() {
	return debug_trace_state;
}

void DEBUG_Trace(bool enable) {
	debug_trace_state = enable;
}

bool DEBUG_ReloadRequested() {
	return reload_requested;
}
