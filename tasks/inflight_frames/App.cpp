#include "App.hpp"
#include <etna/Etna.hpp>
#include <tracy/Tracy.hpp>
#include <etna/Profiling.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include "stb_image.h"
#include <etna/RenderTargetStates.hpp>
#include <chrono>

App::App()
  : resolution{1280, 720}
  , useVsync{true}
  , timeStart{std::chrono::system_clock::now()}
{
  {
    auto glfwInstExts = windowing.getRequiredVulkanInstanceExtensions();

    std::vector<const char*> instanceExtensions{glfwInstExts.begin(), glfwInstExts.end()};
    std::vector<const char*> deviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    etna::initialize(etna::InitParams{
      .applicationName = "inflight frames",
      .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
      .instanceExtensions = instanceExtensions,
      .deviceExtensions = deviceExtensions,
      .physicalDeviceIndexOverride = {},
      .numFramesInFlight = 2,
    });
  }

  osWindow = windowing.createWindow(OsWindow::CreateInfo{
    .resolution = resolution,
  });

  {
    auto surface = osWindow->createVkSurface(etna::get_context().getInstance());
    vkWindow = etna::get_context().createWindow(etna::Window::CreateInfo{
      .surface = std::move(surface),
    });
    auto [w, h] = vkWindow->recreateSwapchain(etna::Window::DesiredProperties{
      .resolution = {resolution.x, resolution.y},
      .vsync = useVsync,
    });
    resolution = {w, h};
  }
  commandManager = etna::get_context().createPerFrameCmdMgr();
  etna::create_program(
    "procedural_texture", 
    {INFLIGHT_FRAMES_SHADERS_ROOT "procedural_texture.comp.spv"});

  texturePipeline =
    etna::get_context().getPipelineManager().createComputePipeline("procedural_texture", {});

  textureImage = etna::get_context().createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{resolution.x, resolution.y, 1},
    .name = "procedural_texture",
    .format = vk::Format::eR8G8B8A8Unorm,
    .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage});

  textureSampler = etna::Sampler::Sampler(etna::Sampler::CreateInfo{
    .addressMode = vk::SamplerAddressMode::eRepeat, .name = "textureSampler"});

  int width, height, channels;
  unsigned char* image_data = stbi_load(
    GRAPHICS_COURSE_RESOURCES_ROOT "/textures/test_tex_1.png", &width, &height, &channels, 4);

  fileTextureImage = etna::get_context().createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{static_cast<unsigned>(width), static_cast<unsigned>(height), 1},
    .name = "file_texture",
    .format = vk::Format::eR8G8B8A8Unorm,
    .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage |
      vk::ImageUsageFlagBits::eTransferDst});

  fileTextureSampler = etna::Sampler::Sampler(etna::Sampler::CreateInfo{
    .addressMode = vk::SamplerAddressMode::eRepeat, .name = "fileTextureSampler"});

  transferHelper =
    std::make_unique<etna::BlockingTransferHelper>(etna::BlockingTransferHelper::CreateInfo{
      .stagingSize = static_cast<std::uint32_t>(width * height),
    });

  std::unique_ptr<etna::OneShotCmdMgr> OneShotCommands = etna::get_context().createOneShotCmdMgr();

  transferHelper->uploadImage(*OneShotCommands, fileTextureImage, 0, 0,
      std::span<const std::byte>(reinterpret_cast<const std::byte*>(image_data), width * height * 4));

  stbi_image_free(image_data);

  cur_frame = 0;

  for (int i = 0; i < 2; ++i)
  {
    constantsBuffers.push_back(etna::get_context().createBuffer(etna::Buffer::CreateInfo{
      .size = sizeof(Params),
      .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_CPU_ONLY,
      .name = "constants_buffer",
    }));
    constantsBuffers[i].map();
  }

  etna::create_program(
    "shader",
    {INFLIGHT_FRAMES_SHADERS_ROOT "main_shader.vert.spv",
     INFLIGHT_FRAMES_SHADERS_ROOT "main_shader.frag.spv"});

  shaderPipeline = 
      etna::get_context().getPipelineManager().createGraphicsPipeline(
          "shader", 
          etna::GraphicsPipeline::CreateInfo{
              .fragmentShaderOutput = 
              {
                  .colorAttachmentFormats = {vk::Format::eB8G8R8A8Srgb}
              },
          });

}

App::~App()
{
  ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
}

void App::run()
{
  while (!osWindow->isBeingClosed())
  {
    ZoneScopedN("tick");
    windowing.poll();

    drawFrame();
  }
  ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
}

void App::drawFrame()
{
  ZoneScopedN("draw_func");
  auto currentCmdBuf = commandManager->acquireNext();
  etna::begin_frame();
  auto nextSwapchainImage = vkWindow->acquireNext();
  if (nextSwapchainImage) {
    ZoneScopedN("swapchain_good");
    auto [backbuffer, backbufferView, backbufferAvailableSem] = *nextSwapchainImage;
    ETNA_CHECK_VK_RESULT(currentCmdBuf.begin(vk::CommandBufferBeginInfo{})); {
      ETNA_PROFILE_GPU(currentCmdBuf, renderFrame);
      etna::set_state(
        currentCmdBuf,
        backbuffer,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageAspectFlagBits::eColor);
        etna::flush_barriers(currentCmdBuf);
        float time =
        std::chrono::duration<float>(std::chrono::system_clock::now() - timeStart).count();

      Params param{
          .iResolution = resolution, 
          .iMouse = osWindow->mouse.freePos, 
          .iTime = time
      };
      std::memcpy(constantsBuffers[cur_frame % 3].data(), &param, sizeof(param));
      {
        ZoneScopedN("creating_compute_shader");
        auto computeInfo = etna::get_shader_program("procedural_texture");

        auto set = etna::create_descriptor_set(
          computeInfo.getDescriptorLayoutId(0),
          currentCmdBuf,
          {
                etna::Binding{0, textureImage.genBinding({}, vk::ImageLayout::eGeneral)},
                etna::Binding{1, constantsBuffers[cur_frame % 3].genBinding()}
          });

        vk::DescriptorSet vkSet = set.getVkSet();

        currentCmdBuf.bindPipeline(vk::PipelineBindPoint::eCompute, texturePipeline.getVkPipeline());
        currentCmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eCompute, texturePipeline.getVkPipelineLayout(),
          0, 1, &vkSet, 0, nullptr);

        currentCmdBuf.pushConstants( texturePipeline.getVkPipelineLayout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(param), &param);

        etna::flush_barriers(currentCmdBuf);

        currentCmdBuf.dispatch(resolution.x / 16, resolution.y / 16, 1);
      }


      etna::set_state(
        currentCmdBuf,
        textureImage.get(),
        vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderSampledRead,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageAspectFlagBits::eColor);

      etna::flush_barriers(currentCmdBuf);
      {
        ZoneScopedN("creating_graphic_shader");

        etna::RenderTargetState state{currentCmdBuf, {{}, {resolution.x, resolution.y}}, 
            {{backbuffer, backbufferView}}, {}};

        auto graphicsInfo = etna::get_shader_program("shader");

        auto set = etna::create_descriptor_set(
          graphicsInfo.getDescriptorLayoutId(0),
          currentCmdBuf,
          {
                etna::Binding{0, textureImage.genBinding(textureSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
                etna::Binding{1, fileTextureImage.genBinding(fileTextureSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
                etna::Binding{2, constantsBuffers[cur_frame % 3].genBinding()}
          });

        vk::DescriptorSet vkSet = set.getVkSet();

        currentCmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, shaderPipeline.getVkPipeline());
        currentCmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, shaderPipeline.getVkPipelineLayout(),
          0, 1, &vkSet, 0, nullptr);

        currentCmdBuf.pushConstants(shaderPipeline.getVkPipelineLayout(), vk::ShaderStageFlagBits::eFragment, 0, sizeof(param), &param);

        currentCmdBuf.draw(3, 1, 0, 0);
      }
      etna::set_state(
        currentCmdBuf,
        backbuffer,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        {},
        vk::ImageLayout::ePresentSrcKHR,
        vk::ImageAspectFlagBits::eColor);
      etna::flush_barriers(currentCmdBuf);

      std::this_thread::sleep_for(std::chrono::milliseconds(7));

      ++cur_frame;

      ETNA_READ_BACK_GPU_PROFILING(currentCmdBuf);
    }
    ETNA_CHECK_VK_RESULT(currentCmdBuf.end());
    auto renderingDone =
      commandManager->submit(std::move(currentCmdBuf), std::move(backbufferAvailableSem));

    const bool presented = vkWindow->present(std::move(renderingDone), backbufferView);

    if (!presented)
      nextSwapchainImage = std::nullopt;
  }

  etna::end_frame();
  if (!nextSwapchainImage && osWindow->getResolution() != glm::uvec2{0, 0})
  {
    auto [w, h] = vkWindow->recreateSwapchain(etna::Window::DesiredProperties{
      .resolution = {resolution.x, resolution.y},
      .vsync = useVsync,
    });
    ETNA_VERIFY((resolution == glm::uvec2{w, h}));
  }
}