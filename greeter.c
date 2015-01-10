#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cairo.h>
#include <cairo-xcb.h>
// #include <cairo/cairo-xcb-xrender.h>

// #include <lightdm.h>
// #include <pango/pango.h>
#include <pango/pangocairo.h>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef enum turbo_align_t {
  TURBO_TOP,
  TURBO_MIDDLE,
  TURBO_BOTTOM
} turbo_align_t;

typedef struct turbo_color_t {
  double r, g, b;
} turbo_color_t;

typedef struct turbo_config_t {
  char *font_family;
  turbo_color_t *background, *text, *circle, *valid, *invalid;
} turbo_config_t;

typedef struct turbo_context_t {
  xcb_connection_t *display;
  xcb_screen_t *screen;
  xcb_window_t window;

  uint16_t width, height;

  cairo_surface_t *surf_out, *surf_buf;
  cairo_t *cr_out, *cr_buf;

  PangoLayout *p_layout;

  turbo_config_t *config;
} turbo_context_t;

void *xmalloc(size_t bytes) {
  void *ptr = malloc(bytes);
  if (ptr == NULL) {
    fprintf(stderr, "[FATAL] Out of memory\n");
    exit(1);
  }
  return ptr;
}

xcb_visualtype_t *get_root_visual_type(xcb_screen_t *screen) {
  xcb_depth_iterator_t depth_iter = xcb_screen_allowed_depths_iterator(screen);
  xcb_visualtype_iterator_t visual_iter;

  while (depth_iter.rem) {
    visual_iter = xcb_depth_visuals_iterator(depth_iter.data);

    while (visual_iter.rem) {
      if (screen->root_visual != visual_iter.data->visual_id) {
        return visual_iter.data;
      }

      xcb_visualtype_next(&visual_iter);
    }

    xcb_depth_next(&depth_iter);
  }

  return NULL;
}

void parse_color(turbo_color_t *color, char *hex) {
  unsigned value = 0;

  static const signed char table[] = "                                         "
    "       \0\x01\x02\x03\x04\x05\x06\x07\x08\t       \n\x0b\x0c\r\x0e\x0f    "
    "                      \n\x0b\x0c\r\x0e\x0f                                "
    "                                                                          "
    "                                              ";

  if (hex != NULL) {
    char *hexChar = hex;
    for (char chr = *hexChar; chr != 0 && hexChar < hex + 6; chr = *++hexChar) {
      chr = table[(size_t) chr];
      if (chr == ' ') {
        value = 0;
        break;
      }
      value = (value << 4) | chr;
    }
    if (hexChar != hex + 6) {
      value = 0;
    }
  }

  color->r = ((double) (value >> 16)) / 255;
  color->g = ((double) ((value >> 8) & 0xff)) / 255;
  color->b = ((double) (value & 0xff)) / 255;
}

turbo_color_t *create_color(char *hex) {
  turbo_color_t *color = xmalloc(sizeof(*color));
  parse_color(color, hex);
  return color;
}

void pango_layout_set_font_family(PangoLayout *layout, char *font_family) {
  PangoFontDescription *desc = pango_font_description_from_string(font_family);
  pango_layout_set_font_description(layout, desc);
  pango_font_description_free(desc);
}

void write_text(turbo_context_t *ctx, double x, double y, char *text, turbo_align_t align) {
  pango_layout_set_text(ctx->p_layout, text, -1);
  pango_cairo_update_layout(ctx->cr_buf, ctx->p_layout);

  int width;
  pango_layout_get_pixel_size(ctx->p_layout, &width, NULL);

  cairo_move_to(ctx->cr_buf, x - (width / 2.d), y);

  pango_cairo_show_layout(ctx->cr_buf, ctx->p_layout);
}

void cairo_set_color(cairo_t *cr, turbo_color_t *color) {
  cairo_set_source_rgb(cr, color->r, color->g, color->b);
}

void draw(turbo_context_t *ctx) {
  turbo_config_t *config = ctx->config;

  double cx = ctx->width / 2.d, cy = ctx->height / 2.d;
  double radius = ctx->height / 10.d;

  // fill with black
  cairo_set_color(ctx->cr_buf, config->background);
  cairo_rectangle(ctx->cr_buf, 0, 0, ctx->width, ctx->height);
  cairo_fill(ctx->cr_buf);

  cairo_set_color(ctx->cr_buf, config->valid);
  cairo_arc(ctx->cr_buf, cx, cy, radius, 0, 2 * M_PI);
  cairo_fill(ctx->cr_buf);

  cairo_set_color(ctx->cr_buf, config->circle);
  cairo_arc(ctx->cr_buf, cx, cy, radius - 10, 0, 2 * M_PI);
  cairo_fill(ctx->cr_buf);

  // write username in white
  cairo_set_color(ctx->cr_buf, config->text);
  write_text(ctx, cx, 13.d * ctx->height / 20.d, "skeggse", TURBO_MIDDLE);

  // copy from buffer to output
  cairo_rectangle(ctx->cr_out, 0, 0, ctx->width, ctx->height);
  cairo_fill(ctx->cr_out);
}

// TODO: command line arguments and config file
int main() {
  turbo_config_t config = {
    .font_family = "M+ 1m",
    .background = create_color("232323"),
    .text = create_color("ffffff"),
    .circle = create_color("555555"),
    .valid = create_color("00cc00"),
    .invalid = create_color("cc0000")
  };
  turbo_context_t ctx = {
    .config = &config
  };

#if _POSIX_C_SOURCE >= 1 || _XOPEN_SOURCE || _POSIX_SOURCE
  if (!isatty(fileno(stdout))) setbuf(stdout, NULL);
  if (!isatty(fileno(stderr))) setbuf(stderr, NULL);
#endif

  // open the display
  ctx.display = xcb_connect(NULL, NULL);
  if (xcb_connection_has_error(ctx.display)) {
    fprintf(stderr, "[FATAL] Cannot open display\n");
    return 1;
  }

  // get the default screen
  const xcb_setup_t *setup = xcb_get_setup(ctx.display);
  const xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
  ctx.screen = iter.data;

  ctx.width = ctx.screen->width_in_pixels;
  ctx.height = ctx.screen->height_in_pixels;

  char *window_title = "lightdm-greeter-turbo";
  uint32_t window_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
  uint32_t window_values[2] = {
    ctx.screen->black_pixel,
    XCB_EVENT_MASK_EXPOSURE | //XCB_EVENT_MASK_BUTTON_PRESS |
    // XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION |
    XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW | // TODO: keep?
    XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
    XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
    XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW |
    XCB_EVENT_MASK_STRUCTURE_NOTIFY
  };

  ctx.window = xcb_generate_id(ctx.display);
  xcb_create_window(ctx.display, XCB_COPY_FROM_PARENT, ctx.window, ctx.screen->root,
    0, 0, ctx.width, ctx.height, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
    ctx.screen->root_visual, window_mask, window_values);

  xcb_change_property(ctx.display, XCB_PROP_MODE_REPLACE, ctx.window,
    XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, strlen(window_title), window_title);

  xcb_visualtype_t *visual_type = get_root_visual_type(ctx.screen);

  ctx.surf_out = cairo_xcb_surface_create(ctx.display, ctx.window, visual_type,
    ctx.width, ctx.height);
  ctx.surf_buf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
    ctx.width, ctx.height);

  if (!ctx.surf_out) {
    fprintf(stderr, "[FATAL] Error creating surface\n");
    return 1;
  }

  ctx.cr_out = cairo_create(ctx.surf_out);
  ctx.cr_buf = cairo_create(ctx.surf_buf);

  cairo_set_operator(ctx.cr_out, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_surface(ctx.cr_out, ctx.surf_buf, 0, 0);

  ctx.p_layout = pango_cairo_create_layout(ctx.cr_buf);

  pango_layout_set_font_family(ctx.p_layout, config.font_family);

  xcb_map_window(ctx.display, ctx.window);
  xcb_flush(ctx.display);

  draw(&ctx);

  xcb_generic_event_t *event;
  while (event = xcb_wait_for_event(ctx.display)) {
    switch (event->response_type & ~0x80) {
    case XCB_KEY_PRESS: {
      /*xcb_key_press_event_t *key = (xcb_key_press_event_t*) event;

      xcb_key_symbols_get_keycode(key->detail);*/

      draw(&ctx);
    } break;

    case XCB_KEY_RELEASE: {
      /*xcb_key_release_event_t *key = (xcb_key_release_event_t*) event;

      xcb_keycode_t *keys = xcb_key_symbols_get_keycode(key->detail);

      for (int i = 0; keys[i] != XCB_NO_SYMBOL; i++) {
        // use key
      }*/

      draw(&ctx);
    } break;

    case XCB_CONFIGURE_NOTIFY:
      // notify when a window's properties change
      // probably won't happen
      break;

    case XCB_EXPOSE:
      // redraw
      draw(&ctx);
      break;
    }

    free(event);
  }

  // TODO: the rest of the cleanup
  cairo_destroy(ctx.cr_buf);
  cairo_destroy(ctx.cr_out);
  cairo_surface_destroy(ctx.surf_buf);
  cairo_surface_destroy(ctx.surf_out);

  return 0;
}
