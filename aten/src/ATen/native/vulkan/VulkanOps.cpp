#ifdef USE_VULKAN

#include <ATen/native/vulkan/VulkanOps.h>
#include <ATen/native/vulkan/Vulkan.h>
#include <c10/util/Optional.h>
#include <iostream>
#include <limits>

namespace at {
namespace native {
namespace vulkan {
namespace details {
namespace vulkan {

void upsample_nearest2d(
    VulkanTensor& output,
    const VulkanTensor& input,
    int64_t IH,
    int64_t IW,
    int64_t OH,
    int64_t OW,
    int64_t _N,
    int64_t _C,
    float scaleH,
    float scaleW) {
  auto device = context().device();
  auto physicalDevice = context().physicalDevice();
  int64_t C = _N * _C;
  struct ConstBlock {
    int32_t IW;
    int32_t IH;
    int32_t OW;
    int32_t OH;
    float scaleX;
    float scaleY;
  };
  ConstBlock constBlock{IW, IH, OW, OH, scaleW, scaleH};
  VBuffer constBuffer =
      makeUniformConstBuffer((void*)&constBlock, sizeof(constBlock));

  VkDescriptorSetLayout descrSetLayout{};
  VkDescriptorSetLayoutBinding bindings[] = {
      vkutil::descriptorSetLayoutBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
      vkutil::descriptorSetLayoutBinding(
          1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
      vkutil::descriptorSetLayoutBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)};
  vkutil::createDescriptorSetLayout(
      device, bindings, 3 /* bindingsCount */, &descrSetLayout);

  VkDescriptorPool descrPool{};
  VkDescriptorPoolSize poolSizes[] = {
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
  vkutil::createDescriptorPool(
      device, poolSizes, 3 /* poolSizeCount */, 1 /* maxSets */, &descrPool);

  VkDescriptorSet descrSet{};
  vkutil::allocateDescriptorSet(device, descrPool, &descrSetLayout, &descrSet);
  output.impl()->image().bind(
      descrSet, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
  input.impl()->image().bind(
      descrSet,
      1,
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  constBuffer.bind(descrSet, 2);

  WorkGroupSize workGroupSize{8, 8, 1};
  ComputeUnit computeUnit{
      at::native::vulkan::GLSL_SPV(vulkan_upsampleNearest2d),
      descrSetLayout,
      workGroupSize};
  computeUnit.createCommandBuffer(descrSet);
  input.impl()->image().addImageMemoryBarrier(
      computeUnit.commandBuffer(),
      VK_IMAGE_LAYOUT_GENERAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  computeUnit.dispatchCommandBuffer(
      UP_DIV(OW, workGroupSize.x),
      UP_DIV(OH, workGroupSize.y),
      UP_DIV(C, workGroupSize.z));
  computeUnit.runCommandBuffer();
  vkDestroyDescriptorPool(device, descrPool, nullptr);
  vkDestroyDescriptorSetLayout(device, descrSetLayout, nullptr);
}

void add(
    VulkanTensor& output,
    const VulkanTensor& input0,
    const VulkanTensor& input1,
    float alpha) {
  TORCH_INTERNAL_ASSERT(
      output.impl()->dim() == 4,
      "Vulkan add is implemented for 4-dim tensors, output is not 4-dim");
  TORCH_INTERNAL_ASSERT(
      input0.impl()->dim() == 4,
      "Vulkan add is implemented for 4-dim tensors, input0 is not 4-dim");
  TORCH_INTERNAL_ASSERT(
      input1.impl()->dim() == 4,
      "Vulkan add is implemented for 4-dim tensors, input1 is not 4-dim");
  auto sizes = output.impl()->sizes();
  auto C = sizes[0] * sizes[1];
  auto H = sizes[2];
  auto W = sizes[3];

  auto device = context().device();
  auto physicalDevice = context().physicalDevice();
  struct ConstBlock {
    int32_t W;
    int32_t H;
    int32_t C;
    float alpha;
  };
  ConstBlock constBlock{W, H, C, alpha};
  VBuffer constBuffer =
      makeUniformConstBuffer((void*)&constBlock, sizeof(constBlock));

  VkDescriptorSetLayout descrSetLayout{};
  VkDescriptorSetLayoutBinding bindings[] = {
      vkutil::descriptorSetLayoutBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
      vkutil::descriptorSetLayoutBinding(
          1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
      vkutil::descriptorSetLayoutBinding(
          2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
      vkutil::descriptorSetLayoutBinding(3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)};
  vkutil::createDescriptorSetLayout(
      device, bindings, 4 /* bindingsCount */, &descrSetLayout);

  VkDescriptorPool descrPool{};
  VkDescriptorPoolSize poolSizes[] = {
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
  vkutil::createDescriptorPool(
      device, poolSizes, 4 /* poolSizeCount */, 1 /* maxSets */, &descrPool);

  VkDescriptorSet descrSet{};
  vkutil::allocateDescriptorSet(device, descrPool, &descrSetLayout, &descrSet);
  output.impl()->image().bind(
      descrSet, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
  input0.impl()->image().bind(
      descrSet,
      1,
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  input1.impl()->image().bind(
      descrSet,
      2,
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  constBuffer.bind(descrSet, 3);

  WorkGroupSize workGroupSize{8, 8, 1};
  ComputeUnit computeUnit{
      at::native::vulkan::GLSL_SPV(vulkan_add), descrSetLayout, workGroupSize};
  computeUnit.createCommandBuffer(descrSet);
  output.impl()->image().addImageMemoryBarrier(
      computeUnit.commandBuffer(),
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_GENERAL);
  input0.impl()->image().addImageMemoryBarrier(
      computeUnit.commandBuffer(),
      VK_IMAGE_LAYOUT_GENERAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  input1.impl()->image().addImageMemoryBarrier(
      computeUnit.commandBuffer(),
      VK_IMAGE_LAYOUT_GENERAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  computeUnit.dispatchCommandBuffer(
      UP_DIV(W, workGroupSize.x),
      UP_DIV(H, workGroupSize.y),
      UP_DIV(C, workGroupSize.z));
  computeUnit.runCommandBuffer();
  vkDestroyDescriptorPool(device, descrPool, nullptr);
  vkDestroyDescriptorSetLayout(device, descrSetLayout, nullptr);
}

VBuffer kernelNCHW_OCHW_repack_O4C4HWi4o4(
    const float* weights,
    const int OC,
    const int C,
    const int KH,
    const int KW) {
  const auto Cau4 = ALIGN_UP4(C);
  const auto C_4 = UP_DIV(C, 4);
  const auto kBufSizeNumel = ALIGN_UP4(OC) * Cau4 * KH * KW;
  auto size = sizeof(float) * kBufSizeNumel;
  auto sizeAligned =
      ROUND_UP(size, context().limits().minStorageBufferOffsetAlignment);
  VBuffer kernelBuffer{sizeAligned};
  const int oc_4SizeNumel = KW * KH * C_4 * 16;
  auto mappedMemory = kernelBuffer.map();
  if (mappedMemory.ptr()) {
    float* basePtr = (float*)mappedMemory.ptr();
    memset(basePtr, 0, size);
    const float* src = weights;
    int ridx = 0;
    for (int oc = 0; oc < OC; ++oc) {
      int oc_4 = oc / 4;
      int oc_4_i = oc % 4;
      float* dst_oc = basePtr + oc_4 * oc_4SizeNumel;
      for (int ic = 0; ic < C; ++ic) {
        int ic_4 = ic / 4;
        int ic_4_i = ic % 4;
        float* dst_ic = dst_oc + ic_4 * KW * KH * 16;
        for (int ky = 0; ky < KH; ++ky) {
          float* dst_ky = dst_ic + ky * KW * 16;
          for (int kx = 0; kx < KW; ++kx) {
            float* dst_kx = dst_ky + kx * 16;
            dst_kx[4 * ic_4_i + oc_4_i] = src[ridx++];
          }
        }
      }
    }
  }
  return kernelBuffer;
}

VImage conv2d_kernelImage_from_hostCHW(
    const float* data,
    int64_t OC,
    int64_t C,
    int64_t KH,
    int64_t KW) {
  auto device = context().device();
  auto kernelBuffer = kernelNCHW_OCHW_repack_O4C4HWi4o4(data, OC, C, KH, KW);
  auto OC_4 = UP_DIV(OC, 4);
  auto C_4 = UP_DIV(C, 4);

  VImage kernelImage{C_4 * 4, OC_4, 4 * KH * KW};
  struct ConstBlock {
    int32_t KWxKH;
    int32_t C_4;
  };
  ConstBlock constBlock{KW * KH, C_4};
  VBuffer constBuffer =
      makeUniformConstBuffer((void*)&constBlock, sizeof(constBlock));

  VkDescriptorSetLayout descrSetLayout{};
  VkDescriptorSetLayoutBinding bindings[] = {
      vkutil::descriptorSetLayoutBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
      vkutil::descriptorSetLayoutBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
      vkutil::descriptorSetLayoutBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)};
  vkutil::createDescriptorSetLayout(
      device, bindings, 3 /* bindingsCount */, &descrSetLayout);

  VkDescriptorPool descrPool{};
  VkDescriptorPoolSize poolSizes[] = {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
                                      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
                                      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
  vkutil::createDescriptorPool(
      device, poolSizes, 3 /* poolSizeCount */, 1 /* maxSets */, &descrPool);

  VkDescriptorSet descrSet{};
  vkutil::allocateDescriptorSet(device, descrPool, &descrSetLayout, &descrSet);

  kernelImage.bind(
      descrSet, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
  kernelBuffer.bind(descrSet, 1);
  constBuffer.bind(descrSet, 2);

  WorkGroupSize workGroupSize{1, 1, 1};
  ComputeUnit computeUnit{at::native::vulkan::GLSL_SPV(vulkan_KO4C4HW_to_image),
                          descrSetLayout,
                          workGroupSize};
  computeUnit.createCommandBuffer(descrSet);
  kernelImage.addImageMemoryBarrier(
      computeUnit.commandBuffer(),
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_GENERAL);
  kernelBuffer.addBufferMemoryBarrier(
      computeUnit.commandBuffer(), 0, kernelBuffer.sizeBytes());
  computeUnit.dispatchCommandBuffer(
      UP_DIV(C_4, workGroupSize.x),
      UP_DIV(OC_4, workGroupSize.y),
      UP_DIV(KH * KW, workGroupSize.z));
  computeUnit.runCommandBuffer();
  vkDestroyDescriptorPool(device, descrPool, nullptr);
  vkDestroyDescriptorSetLayout(device, descrSetLayout, nullptr);
  return kernelImage;
}

void conv2dDepthWise(
    VulkanTensor& output,
    const VulkanTensor& input,
    const float* weight,
    int64_t KH,
    int64_t KW,
    const float* bias,
    int64_t SY,
    int64_t SX,
    int64_t PY,
    int64_t PX,
    int64_t DY,
    int64_t DX,
    int64_t G) {
  auto isizes = input.sizes();
  int64_t C = isizes[1];
  auto device = context().device();
  auto osizes = output.sizes();
  int64_t OC = osizes[1];
  int64_t H = isizes[2];
  int64_t W = isizes[3];
  const int64_t OC_4 = UP_DIV(OC, 4);
  const int64_t C_4 = UP_DIV(C, 4);

  const int64_t KWE = (KW - 1) * DX + 1;
  const int64_t KHE = (KH - 1) * DY + 1;
  const int64_t OW = ((W - KWE + 2 * PX) / SX) + 1;
  const int64_t OH = ((H - KHE + 2 * PY) / SY) + 1;
  TORCH_INTERNAL_ASSERT(osizes[2] == OH);
  TORCH_INTERNAL_ASSERT(osizes[3] == OW);

  auto biasBufferSize = sizeof(float) * ALIGN_UP4(OC);
  auto biasBufferSizeAligned = ROUND_UP(
      biasBufferSize, context().limits().minStorageBufferOffsetAlignment);
  VBuffer biasBuffer{biasBufferSizeAligned};
  biasBuffer.copyFromHostToDevice((void*)bias, biasBufferSize);

  struct ConstBlock {
    int32_t padding[2];
    int32_t kernelSize[2];
    int32_t stride[2];
    int32_t dilate[2];
    int32_t inputSize[4];
    int32_t outputSize[4];
  };
  ConstBlock constBlock{{PX, PY},
                        {KW, KH},
                        {SX, SY},
                        {DX, DY},
                        {OW, OH, OC_4, 0},
                        {W, H, C_4, 0}};
  VBuffer constBuffer =
      makeUniformConstBuffer((void*)&constBlock, sizeof(constBlock));

  VulkanTensor kernel{std::vector<int64_t>{OC, KH, KW}};
  kernel.setDataFromHost(weight);

  VkDescriptorSetLayout descrSetLayout{};
  VkDescriptorSetLayoutBinding bindings[] = {
      vkutil::descriptorSetLayoutBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
      vkutil::descriptorSetLayoutBinding(
          1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
      vkutil::descriptorSetLayoutBinding(
          2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
      vkutil::descriptorSetLayoutBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
      vkutil::descriptorSetLayoutBinding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)};
  vkutil::createDescriptorSetLayout(
      device, bindings, 5 /* bindingsCount */, &descrSetLayout);

  VkDescriptorPool descrPool{};
  VkDescriptorPoolSize poolSizes[] = {
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
  vkutil::createDescriptorPool(
      device, poolSizes, 5 /* poolSizeCount */, 1 /* maxSets */, &descrPool);

  VkDescriptorSet descrSet{};
  vkutil::allocateDescriptorSet(device, descrPool, &descrSetLayout, &descrSet);
  output.impl()->image().bind(
      descrSet, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
  input.impl()->image().bind(
      descrSet,
      1,
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  kernel.impl()->image().bind(
      descrSet,
      2,
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  biasBuffer.bind(descrSet, 3);
  constBuffer.bind(descrSet, 4);

  WorkGroupSize workGroupSize{8, 8, 1};
  ComputeUnit computeUnit =
      ComputeUnit{at::native::vulkan::GLSL_SPV(vulkan_convDW_tex),
                  descrSetLayout,
                  workGroupSize};
  computeUnit.createCommandBuffer(descrSet);
  output.impl()->image().addImageMemoryBarrier(
      computeUnit.commandBuffer(),
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_GENERAL);
  input.impl()->image().addImageMemoryBarrier(
      computeUnit.commandBuffer(),
      VK_IMAGE_LAYOUT_GENERAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  kernel.impl()->image().addImageMemoryBarrier(
      computeUnit.commandBuffer(),
      VK_IMAGE_LAYOUT_GENERAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  computeUnit.dispatchCommandBuffer(
      UP_DIV(OW, workGroupSize.x),
      UP_DIV(OH, workGroupSize.y),
      UP_DIV(OC_4, workGroupSize.z));
  computeUnit.runCommandBuffer();

  vkDestroyDescriptorPool(device, descrPool, nullptr);
  vkDestroyDescriptorSetLayout(device, descrSetLayout, nullptr);
}

void conv2d(
    VulkanTensor& output,
    const VulkanTensor& input,
    const float* weight,
    int64_t KH,
    int64_t KW,
    const float* bias,
    int64_t SY,
    int64_t SX,
    int64_t PY,
    int64_t PX,
    int64_t DY,
    int64_t DX,
    int64_t G) {
  auto isizes = input.sizes();
  int64_t C = isizes[1];
  if (G > 1) {
    TORCH_INTERNAL_ASSERT(
        G == C,
        "Vulkan group convolutions except depthwise(groups==input channels) are not implemented");
    conv2dDepthWise(
        output, input, weight, KH, KW, bias, SY, SX, PY, PX, DY, DX, G);
    return;
  }

  auto device = context().device();
  auto osizes = output.sizes();

  int64_t N = isizes[0];
  int64_t OC = osizes[1];
  int64_t H = isizes[2];
  int64_t W = isizes[3];
  const int64_t OC_4 = UP_DIV(OC, 4);
  const int64_t C_4 = UP_DIV(C, 4);

  const int64_t KWE = (KW - 1) * DX + 1;
  const int64_t KHE = (KH - 1) * DY + 1;
  const int64_t OW = ((W - KWE + 2 * PX) / SX) + 1;
  const int64_t OH = ((H - KHE + 2 * PY) / SY) + 1;

  TORCH_INTERNAL_ASSERT(osizes[2] == OH);
  TORCH_INTERNAL_ASSERT(osizes[3] == OW);

  auto biasBufferSize = sizeof(float) * ALIGN_UP4(OC);
  auto biasBufferSizeAligned = ROUND_UP(
      biasBufferSize, context().limits().minStorageBufferOffsetAlignment);
  VBuffer biasBuffer{biasBufferSizeAligned};
  biasBuffer.copyFromHostToDevice((void*)bias, biasBufferSize);

  struct ConstBlock {
    int32_t padding[2];
    int32_t kernelSize[2];
    int32_t stride[2];
    int32_t dilate[2];
    int32_t inputSize[4];
    int32_t outputSize[4];
  };
  ConstBlock constBlock{{PX, PY},
                        {KW, KH},
                        {SX, SY},
                        {DX, DY},
                        {OW, OH, OC_4, OC},
                        {W, H, C_4, C}};
  VBuffer constBuffer =
      makeUniformConstBuffer((void*)&constBlock, sizeof(constBlock));
  VImage kernelImage = conv2d_kernelImage_from_hostCHW(weight, OC, C, KH, KW);

  VkDescriptorSetLayout descrSetLayout{};
  VkDescriptorSetLayoutBinding bindings[] = {
      vkutil::descriptorSetLayoutBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
      vkutil::descriptorSetLayoutBinding(
          1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
      vkutil::descriptorSetLayoutBinding(
          2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
      vkutil::descriptorSetLayoutBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
      vkutil::descriptorSetLayoutBinding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)};
  vkutil::createDescriptorSetLayout(
      device, bindings, 5 /* bindingsCount */, &descrSetLayout);

  VkDescriptorPool descrPool{};
  VkDescriptorPoolSize poolSizes[] = {
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
  vkutil::createDescriptorPool(
      device, poolSizes, 5 /* poolSizeCount */, 1 /* maxSets */, &descrPool);

  VkDescriptorSet descrSet{};
  vkutil::allocateDescriptorSet(device, descrPool, &descrSetLayout, &descrSet);
  output.impl()->image().bind(
      descrSet, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
  input.impl()->image().bind(
      descrSet,
      1,
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  kernelImage.bind(
      descrSet,
      2,
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  biasBuffer.bind(descrSet, 3);
  constBuffer.bind(descrSet, 4);

  WorkGroupSize workGroupSize{1, 1, OC_4};
  ComputeUnit computeUnit{at::native::vulkan::GLSL_SPV(vulkan_conv_tex_IKnc4hw),
                          descrSetLayout,
                          workGroupSize};
  computeUnit.createCommandBuffer(descrSet);
  output.impl()->image().addImageMemoryBarrier(
      computeUnit.commandBuffer(),
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_GENERAL);
  input.impl()->image().addImageMemoryBarrier(
      computeUnit.commandBuffer(),
      VK_IMAGE_LAYOUT_GENERAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  kernelImage.addImageMemoryBarrier(
      computeUnit.commandBuffer(),
      VK_IMAGE_LAYOUT_GENERAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  computeUnit.dispatchCommandBuffer(
      UP_DIV(OW, 4 * workGroupSize.x),
      UP_DIV(OH, workGroupSize.y),
      UP_DIV(OC_4, workGroupSize.z));
  computeUnit.runCommandBuffer();

  vkDestroyDescriptorPool(device, descrPool, nullptr);
  vkDestroyDescriptorSetLayout(device, descrSetLayout, nullptr);
}

void clamp(
    VulkanTensor& output,
    const VulkanTensor& input,
    float min,
    float max) {
  auto sizes = output.impl()->sizes();
  auto C = sizes[0] * sizes[1];
  auto H = sizes[2];
  auto W = sizes[3];
  auto C_4 = UP_DIV(C, 4);

  auto device = context().device();
  auto physicalDevice = context().physicalDevice();
  struct ConstBlock {
    int32_t W;
    int32_t H;
    int32_t C_4;
    int32_t C;
    float min;
    float max;
  };
  ConstBlock constBlock{W, H, C_4, C, min, max};
  VBuffer constBuffer =
      makeUniformConstBuffer((void*)&constBlock, sizeof(constBlock));

  VkDescriptorSetLayout descrSetLayout{};
  VkDescriptorSetLayoutBinding bindings[] = {
      vkutil::descriptorSetLayoutBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
      vkutil::descriptorSetLayoutBinding(
          1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
      vkutil::descriptorSetLayoutBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)};
  vkutil::createDescriptorSetLayout(
      device, bindings, 3 /* bindingsCount */, &descrSetLayout);

  VkDescriptorPool descrPool{};
  VkDescriptorPoolSize poolSizes[] = {
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
  vkutil::createDescriptorPool(
      device, poolSizes, 3 /* poolSizeCount */, 1 /* maxSets */, &descrPool);

  VkDescriptorSet descrSet{};
  vkutil::allocateDescriptorSet(device, descrPool, &descrSetLayout, &descrSet);
  output.impl()->image().bind(
      descrSet, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
  input.impl()->image().bind(
      descrSet,
      1,
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  constBuffer.bind(descrSet, 2);

  WorkGroupSize workGroupSize{8, 8, 1};
  ComputeUnit computeUnit{at::native::vulkan::GLSL_SPV(vulkan_clamp),
                          descrSetLayout,
                          workGroupSize};
  computeUnit.createCommandBuffer(descrSet);
  output.impl()->image().addImageMemoryBarrier(
      computeUnit.commandBuffer(),
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_GENERAL);
  input.impl()->image().addImageMemoryBarrier(
      computeUnit.commandBuffer(),
      VK_IMAGE_LAYOUT_GENERAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  computeUnit.dispatchCommandBuffer(
      UP_DIV(W, workGroupSize.x),
      UP_DIV(H, workGroupSize.y),
      UP_DIV(C, workGroupSize.z));
  computeUnit.runCommandBuffer();
  vkDestroyDescriptorPool(device, descrPool, nullptr);
  vkDestroyDescriptorSetLayout(device, descrSetLayout, nullptr);
}

void addmm(
    VulkanTensor& output,
    const VulkanTensor& t,
    const VulkanTensor& m1,
    const VulkanTensor& m2,
    float beta,
    float alpha) {
  auto m1Sizes = m1.impl()->sizes();
  auto m2Sizes = m2.impl()->sizes();
  TORCH_INTERNAL_ASSERT(m1Sizes.size() == 2);
  TORCH_INTERNAL_ASSERT(m2Sizes.size() == 2);
  uint32_t m1H = m1Sizes[0];
  uint32_t m1W = m1Sizes[1];
  uint32_t m1C = 1;
  uint32_t m1C_4 = UP_DIV(m1C, 4);

  uint32_t m2H = m2Sizes[0];
  uint32_t m2W = m2Sizes[1];
  uint32_t m2C = 1;
  uint32_t m2C_4 = UP_DIV(m2C, 4);

  uint32_t OH = m1Sizes[0];
  uint32_t OW = m2Sizes[1];

  TORCH_INTERNAL_ASSERT(m1W == m2H);
  TORCH_INTERNAL_ASSERT(m1C == m2C);

  uint32_t C = m1C;
  uint32_t C_4 = UP_DIV(C, 4);
  uint32_t K = m1W;

  auto tSizes = t.impl()->sizes();
  uint32_t TH = tSizes[0];
  uint32_t TW = tSizes[1];
  uint32_t TC = 1;

  auto device = context().device();
  auto physicalDevice = context().physicalDevice();

  struct ConstBlock {
    int32_t OW;
    int32_t OH;
    int32_t C_4;
    int32_t C;
    float beta;
    float alpha;
    int32_t K;
  };
  ConstBlock constBlock{OW, OH, C_4, C, beta, alpha, K};
  VBuffer constBuffer =
      makeUniformConstBuffer((void*)&constBlock, sizeof(constBlock));

  VkDescriptorSetLayout descrSetLayout{};
  VkDescriptorSetLayoutBinding bindings[] = {
      vkutil::descriptorSetLayoutBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
      vkutil::descriptorSetLayoutBinding(
          1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
      vkutil::descriptorSetLayoutBinding(
          2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
      vkutil::descriptorSetLayoutBinding(
          3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
      vkutil::descriptorSetLayoutBinding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)};
  vkutil::createDescriptorSetLayout(
      device, bindings, 5 /* bindingsCount */, &descrSetLayout);

  VkDescriptorPool descrPool{};
  VkDescriptorPoolSize poolSizes[] = {
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
  vkutil::createDescriptorPool(
      device, poolSizes, 5 /* poolSizeCount */, 1 /* maxSets */, &descrPool);

  VkDescriptorSet descrSet{};
  vkutil::allocateDescriptorSet(device, descrPool, &descrSetLayout, &descrSet);
  output.impl()->image().bind(
      descrSet, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
  m1.impl()->image().bind(
      descrSet,
      1,
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  m2.impl()->image().bind(
      descrSet,
      2,
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  t.impl()->image().bind(
      descrSet,
      3,
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  constBuffer.bind(descrSet, 4);

  WorkGroupSize workGroupSize{8, 8, 1};
  ComputeUnit computeUnit{at::native::vulkan::GLSL_SPV(vulkan_addmm),
                          descrSetLayout,
                          workGroupSize};
  computeUnit.createCommandBuffer(descrSet);
  output.impl()->image().addImageMemoryBarrier(
      computeUnit.commandBuffer(),
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_GENERAL);
  m1.impl()->image().addImageMemoryBarrier(
      computeUnit.commandBuffer(),
      VK_IMAGE_LAYOUT_GENERAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  m2.impl()->image().addImageMemoryBarrier(
      computeUnit.commandBuffer(),
      VK_IMAGE_LAYOUT_GENERAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  t.impl()->image().addImageMemoryBarrier(
      computeUnit.commandBuffer(),
      VK_IMAGE_LAYOUT_GENERAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  computeUnit.dispatchCommandBuffer(
      UP_DIV(OW, workGroupSize.x),
      UP_DIV(OH, workGroupSize.y),
      UP_DIV(C_4, workGroupSize.z));
  computeUnit.runCommandBuffer();
  vkDestroyDescriptorPool(device, descrPool, nullptr);
  vkDestroyDescriptorSetLayout(device, descrSetLayout, nullptr);
}

} // namespace vulkan
} // namespace details
} // namespace vulkan
} // namespace native
} // namespace at
#endif
