#pragma once

#include <etna/Window.hpp>
#include "wsi/OsWindowingManager.hpp"
#include <etna/PerFrameCmdMgr.hpp>
#include <etna/ComputePipeline.hpp>
#include <etna/Sampler.hpp>
#include <etna/Buffer.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/Image.hpp>
#include <etna/BlockingTransferHelper.hpp>
#include <chrono>

class App
{
public:
  App();
  ~App();

  void run();

private:
  void drawFrame();

private:
  OsWindowingManager windowing;
  std::unique_ptr<OsWindow> osWindow;

  glm::uvec2 resolution;
  bool useVsync;

  std::unique_ptr<etna::Window> vkWindow;
  std::unique_ptr<etna::PerFrameCmdMgr> commandManager;
  etna::ComputePipeline texturePipeline;
  std::unique_ptr<etna::BlockingTransferHelper> transferHelper;
  etna::Image fileTextureImage;
  etna::Image textureImage;
  etna::Sampler textureSampler;
  etna::Sampler fileTextureSampler;
  etna::GraphicsPipeline shaderPipeline;
  std::vector<etna::Buffer> constantsBuffers;
  unsigned cur_frame;
  std::chrono::system_clock::time_point timeStart;
};

struct Params
{
  glm::uvec2 iResolution;
  glm::uvec2 iMouse;
  float iTime;
};