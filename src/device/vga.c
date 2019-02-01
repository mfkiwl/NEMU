#include "common.h"

#ifdef HAS_IOE

#include "device/map.h"
#include <SDL2/SDL.h>

#define VMEM 0x40000

#define SCREEN_PORT 0x100 // Note that this is not the standard
#define SCREEN_MMIO 0x4100
#define SCREEN_H 300
#define SCREEN_W 400

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;

static uint32_t (*vmem) [SCREEN_W];
static uint32_t *screensize_port_base;

void update_screen() {
  SDL_UpdateTexture(texture, NULL, vmem, SCREEN_W * sizeof(vmem[0][0]));
  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, texture, NULL, NULL);
  SDL_RenderPresent(renderer);
}

void init_vga() {
  SDL_Init(SDL_INIT_VIDEO);
  SDL_CreateWindowAndRenderer(SCREEN_W * 2, SCREEN_H * 2, 0, &window, &renderer);
  SDL_SetWindowTitle(window, "NEMU");
  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
      SDL_TEXTUREACCESS_STATIC, SCREEN_W, SCREEN_H);

  screensize_port_base = (void *)new_space(4);
  *screensize_port_base = ((SCREEN_W) << 16) | (SCREEN_H);
  add_pio_map("screen", SCREEN_PORT, (void *)screensize_port_base, 4, NULL);
  add_mmio_map("screen", SCREEN_MMIO, (void *)screensize_port_base, 4, NULL);

  vmem = (void *)new_space(0x80000);
  add_mmio_map("vmem", VMEM, (void *)vmem, 0x80000, NULL);
}
#endif	/* HAS_IOE */
