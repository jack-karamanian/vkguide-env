#include "vulkan_engine.h"
#include <SDL2/SDL.h>
#include <VkBootstrap.h>
#include <absl/cleanup/cleanup.h>
#include <cstdlib>
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_vulkan.h>
#include <spdlog/spdlog.h>
#include <vulkan/vulkan.hpp>

constexpr int WindowWidth = 1920;
constexpr int WindowHeight = 1080;

int main(int, char **) {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    spdlog::error("SDL_Init failed: {}", SDL_GetError());
    return EXIT_FAILURE;
  }

  absl::Cleanup sdl_cleanup = [] { SDL_Quit(); };

  VulkanEngine vulkan_engine;
  if (!vulkan_engine.init(WindowWidth, WindowHeight)) {
    return EXIT_FAILURE;
  }

  bool quit = false;

  ComputePushConstants push_constants = {
      .data1 = glm::vec4(1, 0, 0, 1),
      .data2 = glm::vec4(0, 0, 1, 1),
  };

  while (!quit) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_QUIT:
        quit = true;
        break;
      }

      ImGui_ImplSDL2_ProcessEvent(&event);
    }
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGui::ShowDemoWindow();

    ImGui::Begin("Test");
    ImGui::ColorPicker4("Data 1", glm::value_ptr(push_constants.data1));
    ImGui::ColorPicker4("Data 2", glm::value_ptr(push_constants.data2));

    ImGui::End();

    ImGui::Render();

    vulkan_engine.draw(push_constants);
  }

  return EXIT_SUCCESS;
}
