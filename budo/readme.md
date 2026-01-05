# BUDO Libraries

BUDO provides small C11 helper libraries for apps and games targeting
`apps/terminal`. These helpers emit the OSC 777 control sequences that the
terminal emulator listens to for graphics, sound, and input.

## Graphics
* `budo_graphics_set_resolution(width, height, layer)`
* `budo_graphics_draw_pixel(x, y, r, g, b, layer)`
* `budo_graphics_draw_sprite_rgba(x, y, width, height, rgba, layer)`
* `budo_graphics_draw_text(x, y, text, color, layer)`
* `budo_graphics_clear_rect(x, y, width, height, layer)`
* `budo_graphics_clear_pixels(layer)`
* `budo_graphics_render_layer(layer)`

## Sound
* `budo_sound_play(channel, path, volume)`
* `budo_sound_stop(channel)`

## Input
* `budo_input_init()` / `budo_input_shutdown()`
* `budo_input_poll(&event)` for keyboard events
* `budo_input_query_mouse(&state, timeout_ms)` for mouse position/buttons
