#include "vulkan_engine.h"
#include "VkBootstrap.h"
#include <SDL2/SDL_vulkan.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_vulkan.h>
#include <spdlog/spdlog.h>
#include <vector>
#include <vulkan/vulkan.hpp>

bool VulkanEngine::init(uint32_t width, uint32_t height) {
  m_width = width;
  m_height = height;

  if (!init_vulkan(width, height)) {
    return false;
  }

  if (!create_swapchain(width, height)) {
    return false;
  }
  {

    m_draw_extent = vk::Extent2D{width, height};

    vk::Extent3D draw_image_extent{width, height, 1};
    m_draw_image.image_extent = draw_image_extent;
    m_draw_image.image_format = vk::Format::eR16G16B16A16Sfloat;

    auto draw_image_usages = vk::ImageUsageFlagBits::eTransferSrc |
                             vk::ImageUsageFlagBits::eTransferDst |
                             vk::ImageUsageFlagBits::eStorage |
                             vk::ImageUsageFlagBits::eColorAttachment;

    VmaAllocationCreateInfo allocation_create_info = {};
    allocation_create_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    allocation_create_info.requiredFlags =
        VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vk::ImageCreateInfo img_create_info =
        image_create_info(m_draw_image.image_format, draw_image_usages,
                          m_draw_image.image_extent);

    {

      VkImageCreateInfo create_info = img_create_info;
      VkImage image = {};
      vmaCreateImage(m_allocator, &create_info, &allocation_create_info, &image,
                     &m_draw_image.allocation, nullptr);
      m_draw_image.image = image;
    }

    vk::ImageViewCreateInfo img_view_create_info =
        imageview_create_info(m_draw_image.image_format, m_draw_image.image,
                              vk::ImageAspectFlagBits::eColor);
    m_draw_image.image_view = m_device.createImageView(img_view_create_info);
  }

  // Init descriptors
  {
    std::vector<DescriptorAllocator::PoolSizeRatio> sizes = {
        {vk::DescriptorType::eStorageImage, 1}};

    descriptor_allocator.init_pool(m_device, 10, sizes);

    auto binding = vk::DescriptorSetLayoutBinding{}
                       .setBinding(0)
                       .setDescriptorCount(1)
                       .setStageFlags(vk::ShaderStageFlagBits::eCompute)
                       .setDescriptorType(vk::DescriptorType::eStorageImage);

    auto descriptor_set_layout_create_info =
        vk::DescriptorSetLayoutCreateInfo{}.setBindings(binding);

    m_draw_image_descriptor_layout =
        m_device.createDescriptorSetLayout(descriptor_set_layout_create_info);

    m_draw_image_descriptors =
        descriptor_allocator.allocate(m_device, m_draw_image_descriptor_layout);

    auto image_info = vk::DescriptorImageInfo{}
                          .setImageLayout(vk::ImageLayout::eGeneral)
                          .setImageView(m_draw_image.image_view);

    auto draw_image_write =
        vk::WriteDescriptorSet{}
            .setDstBinding(0)
            .setDstSet(m_draw_image_descriptors)
            .setDescriptorCount(1)
            .setDescriptorType(vk::DescriptorType::eStorageImage)
            .setImageInfo(image_info);

    m_device.updateDescriptorSets(draw_image_write, {});
  }

  {
    auto push_constant = vk::PushConstantRange{}
                             .setOffset(0)
                             .setSize(sizeof(ComputePushConstants))
                             .setStageFlags(vk::ShaderStageFlagBits::eCompute);
    auto pipeline_leyout_create_info =
        vk::PipelineLayoutCreateInfo{}
            .setSetLayouts(m_draw_image_descriptor_layout)
            .setPushConstantRanges(push_constant);

    m_gradient_pipeline_layout =
        m_device.createPipelineLayout(pipeline_leyout_create_info);

    std::ifstream file("./shaders/gradient.spv",
                       std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
      spdlog::error("Failed to open shader");
      return false;
    }

    auto size = static_cast<size_t>(file.tellg());

    std::vector<uint32_t> buffer(size / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char *>(buffer.data()), size);
    file.close();

    auto shader_create_info = vk::ShaderModuleCreateInfo{}.setCode(buffer);

    vk::ShaderModule compute_shader =
        m_device.createShaderModule(shader_create_info);
    auto shader_stage_info = vk::PipelineShaderStageCreateInfo{}
                                 .setStage(vk::ShaderStageFlagBits::eCompute)
                                 .setModule(compute_shader)
                                 .setPName("main");

    auto compute_pipeline_create_info =
        vk::ComputePipelineCreateInfo{}
            .setLayout(m_gradient_pipeline_layout)
            .setStage(shader_stage_info);

    m_gradient_pipeline =
        m_device
            .createComputePipelines(vk::PipelineCache{},
                                    compute_pipeline_create_info)
            .value.at(0);
    m_device.destroy(compute_shader);
  }

  if (!init_commands()) {
    return false;
  }

  // Imgui
  {
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    VkDescriptorPool imguiPool = m_device.createDescriptorPool(pool_info);

    // 2: initialize imgui library

    // this initializes the core structures of imgui
    ImGui::CreateContext();

    // this initializes imgui for SDL
    ImGui_ImplSDL2_InitForVulkan(m_window);

    // this initializes imgui for Vulkan
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = m_instance;
    init_info.PhysicalDevice = m_physical_device;
    init_info.Device = m_device;
    init_info.Queue = m_graphics_queue;
    init_info.DescriptorPool = imguiPool;
    init_info.MinImageCount = 3;
    init_info.ImageCount = 3;
    init_info.UseDynamicRendering = true;
    init_info.ColorAttachmentFormat = (VkFormat)m_swapchain_image_format;

    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&init_info, VK_NULL_HANDLE);

    // execute a gpu command to upload imgui font textures
    // immediate_submit(
    //     [&](VkCommandBuffer cmd) { ImGui_ImplVulkan_CreateFontsTexture(cmd);
    //     });

    // // clear font textures from cpu data
    // ImGui_ImplVulkan_DestroyFontUploadObjects();

    // // add the destroy the imgui created structures
    // _mainDeletionQueue.push_function([=]() {
    //   vkDestroyDescriptorPool(_device, imguiPool, nullptr);
    //   ImGui_ImplVulkan_Shutdown();
    // });
  }

  return true;
}

bool VulkanEngine::init_vulkan(uint32_t width, uint32_t height) {
  m_window = SDL_CreateWindow("Vulkan Guide", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, width, height,
                              SDL_WINDOW_VULKAN);

  if (m_window == nullptr) {
    spdlog::error("SDL_CreateWindow failed: {}", SDL_GetError());
    return false;
  }

  auto instance_res = vkb::InstanceBuilder{}
                          .set_app_name("Vulkan Guide")
                          .use_default_debug_messenger()
                          .require_api_version(1, 3, 0)
                          .enable_validation_layers(true)
                          .build();

  if (!instance_res) {
    spdlog::error("Failed to create Vulkan instance: {}",
                  instance_res.error().message());
    return false;
  }

  m_instance = *instance_res;

  {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(m_window, m_instance, &surface)) {
      spdlog::error("Failed to create Vulkan surface: {}", SDL_GetError());
      return false;
    }

    m_surface = surface;
  }

  vk::PhysicalDeviceVulkan13Features features13;
  features13.dynamicRendering = true;
  features13.synchronization2 = true;

  vk::PhysicalDeviceVulkan12Features features12;
  features12.bufferDeviceAddress = true;
  features12.descriptorIndexing = true;

  auto physical_device_res = vkb::PhysicalDeviceSelector{m_instance}
                                 .set_minimum_version(1, 3)
                                 .set_required_features_13(features13)
                                 .set_required_features_12(features12)
                                 .set_surface(m_surface)
                                 .select();

  if (!physical_device_res) {
    spdlog::error("Failed to select physical device: {}",
                  physical_device_res.error().message());
    return false;
  }

  m_vkb_physical_device = *physical_device_res;
  m_physical_device = m_vkb_physical_device.physical_device;

  auto device_res = vkb::DeviceBuilder{m_vkb_physical_device}.build();

  if (!device_res) {
    spdlog::error("Failed to create device: {}", device_res.error().message());
    return false;
  }

  m_vkb_device = *device_res;
  m_device = m_vkb_device.device;

  m_graphics_queue = m_vkb_device.get_queue(vkb::QueueType::graphics).value();
  m_graphics_queue_family =
      m_vkb_device.get_queue_index(vkb::QueueType::graphics).value();

  VmaAllocatorCreateInfo allocator_create_info = {};
  allocator_create_info.device = m_device;
  allocator_create_info.physicalDevice = m_physical_device;
  allocator_create_info.instance = m_instance;
  allocator_create_info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
  vmaCreateAllocator(&allocator_create_info, &m_allocator);

  spdlog::info("Created Vulkan device: {}",
               m_physical_device.getProperties().deviceName.data());

  return true;
}

bool VulkanEngine::create_swapchain(uint32_t width, uint32_t height) {
  m_swapchain_image_format = vk::Format::eB8G8R8A8Unorm;

  if (m_vkb_swapchain) {
    vkb::destroy_swapchain(m_vkb_swapchain);
    m_swapchain_images.clear();
    m_swapchain_image_views.clear();
  }

  auto swapchain_res =
      vkb::SwapchainBuilder{m_physical_device, m_device, m_surface}
          .set_desired_format(VkSurfaceFormatKHR{
              .format = (VkFormat)m_swapchain_image_format,
              .colorSpace = (VkColorSpaceKHR)vk::ColorSpaceKHR::eSrgbNonlinear})
          .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
          .set_desired_extent(width, height)
          .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
          .build();

  if (!swapchain_res) {
    spdlog::error("Failed to create swapchain: {}",
                  swapchain_res.error().message());
    return false;
  }

  m_vkb_swapchain = *swapchain_res;
  m_swapchain = m_vkb_swapchain.swapchain;
  for (VkImage image : m_vkb_swapchain.get_images().value()) {
    m_swapchain_images.push_back(image);
  }
  for (VkImageView image_view : m_vkb_swapchain.get_image_views().value()) {
    m_swapchain_image_views.push_back(image_view);
  }
  m_swapchain_extent = m_vkb_swapchain.extent;

  return true;
}

bool VulkanEngine::init_commands() {
  auto command_pool_create_info =
      vk::CommandPoolCreateInfo{}
          .setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
          .setQueueFamilyIndex(m_graphics_queue_family);

  auto semaphore_create_info = vk::SemaphoreCreateInfo{};
  auto fence_create_info =
      vk::FenceCreateInfo{}.setFlags(vk::FenceCreateFlagBits::eSignaled);
  for (int i = 0; i < FrameOverlap; ++i) {
    vk::CommandPool command_pool =
        m_device.createCommandPool(command_pool_create_info);
    auto command_buffer_create_info =
        vk::CommandBufferAllocateInfo{}
            .setCommandPool(command_pool)
            .setCommandBufferCount(1)
            .setLevel(vk::CommandBufferLevel::ePrimary);

    m_frames[i] = FrameData{
        .command_pool = command_pool,
        .command_buffer =
            m_device.allocateCommandBuffers(command_buffer_create_info).at(0),
        .swapchain_semaphore = m_device.createSemaphore(semaphore_create_info),
        .render_semaphore = m_device.createSemaphore(semaphore_create_info),
        .render_fence = m_device.createFence(fence_create_info)};
  }

  {
    m_immediate_command_pool =
        m_device.createCommandPool(command_pool_create_info);
    auto command_buffer_create_info =
        vk::CommandBufferAllocateInfo{}
            .setCommandPool(m_immediate_command_pool)
            .setCommandBufferCount(1)
            .setLevel(vk::CommandBufferLevel::ePrimary);
    m_immediate_command_buffer =
        m_device.allocateCommandBuffers(command_buffer_create_info).at(0);
    m_immediate_fence = m_device.createFence(fence_create_info);
  }

  return true;
}

bool VulkanEngine::init_sync_structures() { return true; }

vk::ImageSubresourceRange
VulkanEngine::image_subresource_range(vk::ImageAspectFlags aspect_mask) {
  return vk::ImageSubresourceRange{}
      .setAspectMask(aspect_mask)
      .setBaseMipLevel(0)
      .setLevelCount(vk::RemainingMipLevels)
      .setBaseArrayLayer(0)
      .setLayerCount(vk::RemainingArrayLayers);
}

void VulkanEngine::transition_image(vk::CommandBuffer command_buffer,
                                    vk::Image image,
                                    vk::ImageLayout current_layout,
                                    vk::ImageLayout new_layout) {
  auto image_barrier =
      vk::ImageMemoryBarrier2{}
          .setSrcStageMask(vk::PipelineStageFlagBits2::eAllCommands)
          .setSrcAccessMask(vk::AccessFlagBits2::eMemoryWrite)
          .setDstStageMask(vk::PipelineStageFlagBits2::eAllCommands)
          .setDstAccessMask(vk::AccessFlagBits2::eMemoryWrite |
                            vk::AccessFlagBits2::eMemoryRead)
          .setOldLayout(current_layout)
          .setNewLayout(new_layout);

  vk::ImageAspectFlags aspect_mask =
      new_layout == vk::ImageLayout::eDepthAttachmentOptimal
          ? vk::ImageAspectFlagBits::eDepth
          : vk::ImageAspectFlagBits::eColor;
  image_barrier =
      image_barrier.setSubresourceRange(image_subresource_range(aspect_mask))
          .setImage(image);

  auto dependency_info =
      vk::DependencyInfo{}.setImageMemoryBarriers(image_barrier);

  command_buffer.pipelineBarrier2(dependency_info);
}

void VulkanEngine::draw(const ComputePushConstants &push_constants) {
  vk::resultCheck(
      m_device.waitForFences(current_frame().render_fence, true, UINT64_MAX),
      "render fence");
  m_device.resetFences(current_frame().render_fence);

  uint32_t image_index =
      m_device
          .acquireNextImageKHR(m_swapchain, UINT64_MAX,
                               current_frame().swapchain_semaphore, nullptr)
          .value;

  vk::CommandBuffer command_buffer = current_frame().command_buffer;

  command_buffer.reset();

  auto begin_info = vk::CommandBufferBeginInfo{}.setFlags(
      vk::CommandBufferUsageFlagBits::eOneTimeSubmit);

  command_buffer.begin(begin_info);
  transition_image(command_buffer, m_draw_image.image,
                   vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);

  vk::ImageSubresourceRange clear_range =
      image_subresource_range(vk::ImageAspectFlagBits::eColor);

  // command_buffer.clearColorImage(m_draw_image.image,
  // vk::ImageLayout::eGeneral,
  //                                vk::ClearColorValue{0.2f, 0.2f, 0.2f, 1.0f},
  //                                clear_range);

  command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute,
                              m_gradient_pipeline);
  command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                                    m_gradient_pipeline_layout, 0,
                                    m_draw_image_descriptors, {});
  command_buffer.pushConstants(m_gradient_pipeline_layout,
                               vk::ShaderStageFlagBits::eCompute, 0,
                               sizeof(ComputePushConstants), &push_constants);
  command_buffer.dispatch(std::ceil(m_draw_extent.width / 16.0),
                          std::ceil(m_draw_extent.height / 16.0), 1);

  transition_image(command_buffer, m_draw_image.image,
                   vk::ImageLayout::eGeneral,
                   vk::ImageLayout::eTransferSrcOptimal);
  transition_image(command_buffer, m_swapchain_images[image_index],
                   vk::ImageLayout::eUndefined,
                   vk::ImageLayout::eTransferDstOptimal);

  copy_image_to_image(command_buffer, m_draw_image.image,
                      m_swapchain_images[image_index], m_draw_extent,
                      m_draw_extent);
  transition_image(command_buffer, m_swapchain_images[image_index],
                   vk::ImageLayout::eTransferDstOptimal,
                   vk::ImageLayout::eColorAttachmentOptimal);

  {
    auto color_attachment =
        vk::RenderingAttachmentInfo{}
            .setImageView(m_swapchain_image_views[image_index])
            .setImageLayout(vk::ImageLayout::eGeneral)
            .setLoadOp(vk::AttachmentLoadOp::eLoad)
            .setStoreOp(vk::AttachmentStoreOp::eStore);

    auto rendering_info = vk::RenderingInfo{}
                              .setColorAttachments(color_attachment)
                              .setRenderArea(vk::Rect2D{{0, 0}, m_draw_extent})
                              .setLayerCount(1);
    command_buffer.beginRendering(rendering_info);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer);
    command_buffer.endRendering();
  }

  transition_image(command_buffer, m_swapchain_images[image_index],
                   vk::ImageLayout::eColorAttachmentOptimal,
                   vk::ImageLayout::ePresentSrcKHR);
  // command_buffer.begin(begin_info);
  // transition_image(command_buffer, m_swapchain_images[image_index],
  //                  vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);

  // vk::ImageSubresourceRange clear_range =
  //     image_subresource_range(vk::ImageAspectFlagBits::eColor);

  // command_buffer.clearColorImage(
  //     m_swapchain_images[image_index], vk::ImageLayout::eGeneral,
  //     vk::ClearColorValue{0.2f, 0.2f, 0.2f, 1.0f}, clear_range);
  // transition_image(command_buffer, m_swapchain_images[image_index],
  //                  vk::ImageLayout::eGeneral,
  //                  vk::ImageLayout::ePresentSrcKHR);
  command_buffer.end();

  auto wait_info =
      vk::SemaphoreSubmitInfo{}
          .setSemaphore(current_frame().swapchain_semaphore)
          .setStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
          .setValue(1)
          .setDeviceIndex(0);
  auto signal_info = vk::SemaphoreSubmitInfo{}
                         .setSemaphore(current_frame().render_semaphore)
                         .setStageMask(vk::PipelineStageFlagBits2::eAllGraphics)
                         .setValue(1)
                         .setDeviceIndex(0);

  auto command_buffer_submit_info = vk::CommandBufferSubmitInfo{}
                                        .setCommandBuffer(command_buffer)
                                        .setDeviceMask(0);

  auto submit_info = vk::SubmitInfo2{}
                         .setWaitSemaphoreInfos(wait_info)
                         .setSignalSemaphoreInfos(signal_info)
                         .setCommandBufferInfos(command_buffer_submit_info);

  m_graphics_queue.submit2(submit_info, current_frame().render_fence);

  auto present_info = vk::PresentInfoKHR{}
                          .setSwapchains(m_swapchain)
                          .setWaitSemaphores(current_frame().render_semaphore)
                          .setImageIndices(image_index);

  vk::Result present_result = m_graphics_queue.presentKHR(present_info);

  if (present_result == vk::Result::eSuboptimalKHR) {
    create_swapchain(m_width, m_height);
  } else {
    vk::resultCheck(present_result, "present");
  }

  ++m_current_frame_index;
}

VulkanEngine::~VulkanEngine() {
  m_device.waitIdle();

  for (FrameData &frame : m_frames) {
    m_device.destroyCommandPool(frame.command_pool);
  }
  vkb::destroy_swapchain(m_vkb_swapchain);
  m_device.destroy();
  vkb::destroy_surface(m_instance, m_surface);
  vkb::destroy_instance(m_instance);
  if (m_window != nullptr) {
    SDL_DestroyWindow(m_window);
  }
}