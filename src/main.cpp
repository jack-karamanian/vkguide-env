#include <SDL2/SDL.h>
#include <VkBootstrap.h>
#include <absl/cleanup/cleanup.h>
#include <cstdlib>
#include <spdlog/spdlog.h>
#include <vulkan/vulkan.h>

constexpr int WindowWidth = 1920;
constexpr int WindowHeight = 1080;

int main(int, char **) {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    spdlog::error("SDL_Init failed: {}", SDL_GetError());
    return EXIT_FAILURE;
  }

  absl::Cleanup sdl_cleanup = [] { SDL_Quit(); };

  SDL_Window *window = SDL_CreateWindow("Vulkan Guide", SDL_WINDOWPOS_CENTERED,
                                        SDL_WINDOWPOS_CENTERED, WindowWidth,
                                        WindowHeight, SDL_WINDOW_VULKAN);

  if (window == nullptr) {
    spdlog::error("SDL_CreateWindow failed: {}", SDL_GetError());
    return EXIT_FAILURE;
  }

  absl::Cleanup window_cleanup = [window] { SDL_DestroyWindow(window); };

  vkb::Instance instance = vkb::InstanceBuilder{}
                               .set_app_name("Vulkan Guide")
                               .use_default_debug_messenger()
                               .require_api_version(1, 3, 0)
                               .enable_validation_layers(true)
                               .build()
                               .value();

  absl::Cleanup instance_cleanup = [instance] {
    vkb::destroy_instance(instance);
  };

  bool quit = false;

  while (!quit) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_QUIT:
        quit = true;
        break;
      }
    }
  }
}
