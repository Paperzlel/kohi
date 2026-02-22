#include "texture_browser.h"

void texture_browser_open(PFN_texture_browser_texture_selected_callback selected_callback, PFN_texture_browser_cancel_callback cancel_callback) {
	// TODO:
	// - create new window with focused search-by-name textbox
	// - window should likely include a keymap that overrides all other input OR auto-focuses search textbox.
	// - Do asset search of all assets by type "texture".
	// - Load preview of all textures returned by above query, filtered by search-by-name textbox.
	// - Either double-clicking or selecting texture and clicking "ok" invokes "selected" callback. Also closes window.
	// - Clicking "cancel" closes window and invokes "cancel" callback.
}
