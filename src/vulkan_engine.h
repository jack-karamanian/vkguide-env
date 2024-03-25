#pragma once
#include <SDL2/SDL.h>
#include <VkBootstrap.h>
#include <array>
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <span>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

struct ComputePushConstants {
  glm::vec4 data1;
  glm::vec4 data2;
  glm::vec4 data3;
  glm::vec4 data4;
};

class VulkanEngine {
public:
  bool init(uint32_t width, uint32_t height);
  bool init_vulkan(uint32_t width, uint32_t height);
  bool init_commands();
  bool init_sync_structures();
  bool create_swapchain(uint32_t width, uint32_t height);

  void draw(const ComputePushConstants &push_constants);

  ~VulkanEngine();

private:
  struct DescriptorAllocator {
    struct PoolSizeRatio {
      vk::DescriptorType type;
      float ratio;
    };

    vk::DescriptorPool descriptor_pool;

    void init_pool(vk::Device device, uint32_t max_sets,
                   std::span<PoolSizeRatio> pool_ratios) {
      std::vector<vk::DescriptorPoolSize> pool_sizes;
      for (auto ratio : pool_ratios) {
        pool_sizes.push_back(vk::DescriptorPoolSize{
            ratio.type, uint32_t(ratio.ratio * max_sets)});
      }

      auto create_info =
          vk::DescriptorPoolCreateInfo{}.setMaxSets(max_sets).setPoolSizes(
              pool_sizes);

      descriptor_pool = device.createDescriptorPool(create_info);
    }

    vk::DescriptorSet allocate(vk::Device device,
                               vk::DescriptorSetLayout layout) {
      auto alloc_info = vk::DescriptorSetAllocateInfo{}
                            .setDescriptorPool(descriptor_pool)
                            .setSetLayouts(layout);
      return device.allocateDescriptorSets(alloc_info).at(0);
    }
  };
  struct AllocatedImage {
    vk::Image image;
    vk::ImageView image_view;
    VmaAllocation allocation;
    vk::Extent3D image_extent;
    vk::Format image_format;
  };

  static void transition_image(vk::CommandBuffer command_buffer,
                               vk::Image image, vk::ImageLayout current_layout,
                               vk::ImageLayout new_layout);

  static vk::ImageSubresourceRange
  image_subresource_range(vk::ImageAspectFlags aspect_mask);

  static vk::ImageCreateInfo image_create_info(vk::Format format,
                                               vk::ImageUsageFlags usage_flags,
                                               vk::Extent3D extent) {
    return vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setFormat(format)
        .setExtent(extent)
        .setMipLevels(1)
        .setArrayLayers(1)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setTiling(vk::ImageTiling::eOptimal)
        .setUsage(usage_flags);
  }

  static vk::ImageViewCreateInfo
  imageview_create_info(vk::Format format, vk::Image image,
                        vk::ImageAspectFlags aspect_flags) {
    return vk::ImageViewCreateInfo{}
        .setViewType(vk::ImageViewType::e2D)
        .setImage(image)
        .setFormat(format)
        .setSubresourceRange(vk::ImageSubresourceRange{}
                                 .setBaseMipLevel(0)
                                 .setLevelCount(1)
                                 .setBaseArrayLayer(0)
                                 .setLayerCount(1)
                                 .setAspectMask(aspect_flags));
  }

  void copy_image_to_image(vk::CommandBuffer command_buffer, vk::Image source,
                           vk::Image destination, vk::Extent2D source_size,
                           vk::Extent2D dest_size) {
    vk::ImageBlit2 blit_region = {};
    blit_region.srcOffsets[1].x = source_size.width;
    blit_region.srcOffsets[1].y = source_size.height;
    blit_region.srcOffsets[1].z = 1;

    blit_region.dstOffsets[1].x = dest_size.width;
    blit_region.dstOffsets[1].y = dest_size.height;
    blit_region.dstOffsets[1].z = 1;

    blit_region.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    blit_region.srcSubresource.baseArrayLayer = 0;
    blit_region.srcSubresource.layerCount = 1;
    blit_region.srcSubresource.mipLevel = 0;

    blit_region.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    blit_region.dstSubresource.baseArrayLayer = 0;
    blit_region.dstSubresource.layerCount = 1;
    blit_region.dstSubresource.mipLevel = 0;

    auto blit_info =
        vk::BlitImageInfo2{}
            .setDstImage(destination)
            .setDstImageLayout(vk::ImageLayout::eTransferDstOptimal)
            .setSrcImage(source)
            .setSrcImageLayout(vk::ImageLayout::eTransferSrcOptimal)
            .setFilter(vk::Filter::eLinear)
            .setRegions(blit_region);

    command_buffer.blitImage2(blit_info);
  }

  void immediate_submit(std::function<void(vk::CommandBuffer)> &&function) {
    m_device.resetFences(m_immediate_fence);
    m_immediate_command_buffer.reset();

    auto begin_info = vk::CommandBufferBeginInfo{}.setFlags(
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit);

    m_immediate_command_buffer.begin(begin_info);

    function(m_immediate_command_buffer);

    m_immediate_command_buffer.end();

    auto command_buffer_submit_info =
        vk::CommandBufferSubmitInfo{}
            .setCommandBuffer(m_immediate_command_buffer)
            .setDeviceMask(0);

    auto submit_info =
        vk::SubmitInfo2{}.setCommandBufferInfos(command_buffer_submit_info);

    m_graphics_queue.submit2(submit_info, m_immediate_fence);
    vk::resultCheck(m_device.waitForFences(m_immediate_fence, true, UINT64_MAX),
                    "immediate wait");
  }

  uint32_t m_width = 0;
  uint32_t m_height = 0;
  SDL_Window *m_window = nullptr;
  vkb::Instance m_instance;
  vkb::PhysicalDevice m_vkb_physical_device;
  vkb::Device m_vkb_device;
  vk::PhysicalDevice m_physical_device;
  vk::Device m_device;
  vk::SurfaceKHR m_surface;
  VmaAllocator m_allocator;

  // Swapchain
  vkb::Swapchain m_vkb_swapchain;
  vk::SwapchainKHR m_swapchain;
  vk::Format m_swapchain_image_format;

  std::vector<vk::Image> m_swapchain_images;
  std::vector<vk::ImageView> m_swapchain_image_views;
  vk::Extent2D m_swapchain_extent;

  AllocatedImage m_draw_image;
  vk::Extent2D m_draw_extent;

  DescriptorAllocator descriptor_allocator;
  vk::DescriptorSet m_draw_image_descriptors;
  vk::DescriptorSetLayout m_draw_image_descriptor_layout;

  vk::Pipeline m_gradient_pipeline;
  vk::PipelineLayout m_gradient_pipeline_layout;

  vk::Fence m_immediate_fence;
  vk::CommandBuffer m_immediate_command_buffer;
  vk::CommandPool m_immediate_command_pool;

  // Frames

  struct FrameData {
    vk::CommandPool command_pool;
    vk::CommandBuffer command_buffer;
    vk::Semaphore swapchain_semaphore;
    vk::Semaphore render_semaphore;
    vk::Fence render_fence;
  };

  static constexpr int FrameOverlap = 2;
  std::array<FrameData, FrameOverlap> m_frames;
  size_t m_current_frame_index = 0;
  FrameData &current_frame() {
    return m_frames[m_current_frame_index % FrameOverlap];
  }

  vk::Queue m_graphics_queue;
  uint32_t m_graphics_queue_family;
};
