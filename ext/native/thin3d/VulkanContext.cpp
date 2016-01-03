#include <cstdlib>
#include <assert.h>
#include <cstring>
#include <iostream>

#include "VulkanContext.h"

#ifdef USE_CRT_DBG
#undef new
#endif

#pragma warning(push)
#pragma warning(disable:4996)
#include "glslang/SPIRV/GlslangToSpv.h"
#pragma warning(pop)

#ifdef USE_CRT_DBG
#define new DBG_NEW
#endif

#define GET_INSTANCE_PROC_ADDR(inst, entrypoint)                         \
{                                                                        \
    fp##entrypoint = (PFN_vk##entrypoint) vkGetInstanceProcAddr(inst, "vk"#entrypoint); \
    if (fp##entrypoint == NULL) {                                   \
        std::cout << "vkGetDeviceProcAddr failed to find vk"#entrypoint; \
        exit(-1);                                                        \
    }                                                                    \
}

#define GET_DEVICE_PROC_ADDR(dev, entrypoint)                           \
{                                                                       \
    fp##entrypoint = (PFN_vk##entrypoint) vkGetDeviceProcAddr(dev, "vk"#entrypoint);   \
    if (fp##entrypoint == NULL) {                                   \
        std::cout << "vkGetDeviceProcAddr failed to find vk"#entrypoint; \
        exit(-1);                                                        \
    }                                                                    \
}

using namespace std;

VulkanContext::VulkanContext(const char *app_name, uint32_t flags)
	: device_(nullptr),
	gfx_queue_(nullptr),
	connection(nullptr),
	graphics_queue_family_index_(-1),
	surface(nullptr),
	window(nullptr),
	prepared(false),
	use_staging_buffer_(false),
	instance_(nullptr),
	width(0),
	height(0),
	flags_(flags),
	swapchain_format(VK_FORMAT_UNDEFINED),
	swapchainImageCount(0),
	swap_chain_(nullptr),
	cmd_pool_(nullptr),
	dbgCreateMsgCallback(nullptr),
	dbgDestroyMsgCallback(nullptr),
	queue_count(0),
	curFrame_(0)
{
	// List extensions to try to enable.
	instance_extension_names.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
	instance_extension_names.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
	device_extension_names.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

	// DrawState validation is broken, hangs on vkResetDescriptorPool.
	if (flags & VULKAN_FLAG_VALIDATE) {
		instance_layer_names.push_back("VK_LAYER_LUNARG_threading");
		// instance_layer_names.push_back("VK_LAYER_LUNARG_draw_state");
		instance_layer_names.push_back("VK_LAYER_LUNARG_image");
		instance_layer_names.push_back("VK_LAYER_LUNARG_mem_tracker");
		instance_layer_names.push_back("VK_LAYER_LUNARG_object_tracker");
		instance_layer_names.push_back("VK_LAYER_LUNARG_param_checker");
	
		device_layer_names.push_back("VK_LAYER_LUNARG_threading");
		// device_layer_names.push_back("VK_LAYER_LUNARG_draw_state");
		device_layer_names.push_back("VK_LAYER_LUNARG_image");
		device_layer_names.push_back("VK_LAYER_LUNARG_mem_tracker");
		device_layer_names.push_back("VK_LAYER_LUNARG_object_tracker");
		device_layer_names.push_back("VK_LAYER_LUNARG_param_checker");

		instance_extension_names.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
	}

	VkApplicationInfo app_info = {};
	app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app_info.pNext = NULL;
	app_info.pApplicationName = app_name;
	app_info.applicationVersion = 1;
	app_info.pEngineName = app_name;
	app_info.engineVersion = 1;
	app_info.apiVersion = VK_API_VERSION;

	VkInstanceCreateInfo inst_info = {};
	inst_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	inst_info.pNext = NULL;
	inst_info.flags = 0;
	inst_info.pApplicationInfo = &app_info;
	inst_info.enabledLayerCount = (uint32_t)instance_layer_names.size();
	inst_info.ppEnabledLayerNames = instance_layer_names.size() ? instance_layer_names.data() : NULL;
	inst_info.enabledExtensionCount = (uint32_t)instance_extension_names.size();
	inst_info.ppEnabledExtensionNames = instance_extension_names.data();

	VkResult res = vkCreateInstance(&inst_info, NULL, &instance_);
	assert(res == VK_SUCCESS);

	GET_INSTANCE_PROC_ADDR(instance_, GetPhysicalDeviceSurfaceSupportKHR);
	GET_INSTANCE_PROC_ADDR(instance_, GetPhysicalDeviceSurfaceCapabilitiesKHR);
	GET_INSTANCE_PROC_ADDR(instance_, GetPhysicalDeviceSurfaceFormatsKHR);
	GET_INSTANCE_PROC_ADDR(instance_, GetPhysicalDeviceSurfacePresentModesKHR);

	uint32_t gpu_count = 1;
	res = vkEnumeratePhysicalDevices(instance_, &gpu_count, NULL);
	assert(gpu_count);
	physical_devices_.resize(gpu_count);
	res = vkEnumeratePhysicalDevices(instance_, &gpu_count, physical_devices_.data());
	assert(!res);

	InitGlobalLayerProperties();
	InitGlobalExtensionProperties();

	if (!CheckLayers(instance_layer_properties, instance_layer_names)) {
		exit(1);
	}

	InitDeviceLayerProperties();
	if (!CheckLayers(device_layer_properties, device_layer_names)) {
		exit(1);
	}
}

VulkanContext::~VulkanContext() {
	vkDestroyInstance(instance_, NULL);
}

void TransitionToPresent(VkCommandBuffer cmd, VkImage image) {
	VkImageMemoryBarrier prePresentBarrier = {};
	prePresentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	prePresentBarrier.pNext = NULL;
	prePresentBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	prePresentBarrier.dstAccessMask = 0;
	prePresentBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	prePresentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	prePresentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	prePresentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	prePresentBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	prePresentBarrier.subresourceRange.baseMipLevel = 0;
	prePresentBarrier.subresourceRange.levelCount = 1;
	prePresentBarrier.subresourceRange.baseArrayLayer = 0;
	prePresentBarrier.subresourceRange.layerCount = 1;
	prePresentBarrier.image = image;
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		0, 0, nullptr, 0, nullptr, 1, &prePresentBarrier);
}

void TransitionFromPresent(VkCommandBuffer cmd, VkImage image) {
	VkImageMemoryBarrier prePresentBarrier = {};
	prePresentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	prePresentBarrier.pNext = NULL;
	prePresentBarrier.srcAccessMask = 0;
	prePresentBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	prePresentBarrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	prePresentBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	prePresentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	prePresentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	prePresentBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	prePresentBarrier.subresourceRange.baseMipLevel = 0;
	prePresentBarrier.subresourceRange.levelCount = 1;
	prePresentBarrier.subresourceRange.baseArrayLayer = 0;
	prePresentBarrier.subresourceRange.layerCount = 1;
	prePresentBarrier.image = image;
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		0, 0, nullptr, 0, nullptr, 1, &prePresentBarrier);
}

VkCommandBuffer VulkanContext::GetInitCommandBuffer() {
	FrameData *frame = &frame_[curFrame_];
	if (!frame->hasInitCommands) {
		VulkanBeginCommandBuffer(frame->cmdInit);
		frame->hasInitCommands = true;
	}
	return frame_[curFrame_].cmdInit;
}

VkCommandBuffer VulkanContext::BeginSurfaceRenderPass(VkClearValue clear_values[2]) {
	FrameData *frame = &frame_[curFrame_];

	// Make sure the command buffer from the frame before the previous has been fully executed.
	WaitAndResetFence(frame->fence);

	// Process pending deletes.
	frame->deleteList.PerformDeletes(device_);

	// Get the index of the next available swapchain image, and a semaphore to block command buffer execution on.
	// Now, I wonder if we should do this early in the frame or late?
	VkResult res = fpAcquireNextImageKHR(device_, swap_chain_,
		UINT64_MAX,
		acquireSemaphore,
		NULL,
		&current_buffer);
	// TODO: Deal with the VK_SUBOPTIMAL_KHR and VK_ERROR_OUT_OF_DATE_KHR
	// return codes
	assert(res == VK_SUCCESS);

	VkCommandBufferBeginInfo begin;
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.pNext = NULL;
	begin.flags = 0;
	begin.pInheritanceInfo = nullptr;
	res = vkBeginCommandBuffer(frame->cmdBuf, &begin);

	TransitionFromPresent(frame->cmdBuf, swapChainBuffers[current_buffer].image);

	VkRenderPassBeginInfo rp_begin;
	rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rp_begin.pNext = NULL;
	rp_begin.renderPass = surface_render_pass_;
	rp_begin.framebuffer = framebuffers_[current_buffer];
	rp_begin.renderArea.offset.x = 0;
	rp_begin.renderArea.offset.y = 0;
	rp_begin.renderArea.extent.width = width;
	rp_begin.renderArea.extent.height = height;
	rp_begin.clearValueCount = 2;
	rp_begin.pClearValues = clear_values;

	vkCmdBeginRenderPass(frame->cmdBuf, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
	return frame->cmdBuf;
}

void VulkanContext::WaitUntilQueueIdle() {
	// Should almost never be used
	vkQueueWaitIdle(gfx_queue_);
}

bool VulkanContext::MemoryTypeFromProperties(uint32_t typeBits, VkFlags requirements_mask, uint32_t *typeIndex) {
	// Search memtypes to find first index with those properties
	for (uint32_t i = 0; i < 32; i++) {
		if ((typeBits & 1) == 1) {
			// Type is available, does it match user properties?
			if ((memory_properties.memoryTypes[i].propertyFlags & requirements_mask) == requirements_mask) {
				*typeIndex = i;
				return true;
			}
		}
		typeBits >>= 1;
	}
	// No memory types matched, return failure
	return false;
}

void VulkanContext::EndSurfaceRenderPass() {
	FrameData *frame = &frame_[curFrame_];
	vkCmdEndRenderPass(frame->cmdBuf);

	TransitionToPresent(frame->cmdBuf, swapChainBuffers[current_buffer].image);

	VkResult res = vkEndCommandBuffer(frame->cmdBuf);
	assert(res == VK_SUCCESS);

	int numCmdBufs = 0;
	VkCommandBuffer cmdBufs[2];

	if (frame->hasInitCommands) {
		vkEndCommandBuffer(frame->cmdInit);
		cmdBufs[numCmdBufs++] = frame->cmdInit;
		frame->hasInitCommands = false;
	}
	cmdBufs[numCmdBufs++] = frame->cmdBuf;

	VkSubmitInfo submit_info[1] = {};
	submit_info[0].pNext = NULL;
	submit_info[0].sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info[0].waitSemaphoreCount = 1;
	submit_info[0].pWaitSemaphores = &acquireSemaphore;
	submit_info[0].commandBufferCount = numCmdBufs;
	submit_info[0].pCommandBuffers = cmdBufs;
	submit_info[0].signalSemaphoreCount = 0;
	submit_info[0].pSignalSemaphores = NULL;
	res = vkQueueSubmit(gfx_queue_, 1, submit_info, frame->fence);
	assert(res == VK_SUCCESS);

	VkPresentInfoKHR present;
	present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present.pNext = NULL;
	present.swapchainCount = 1;
	present.pSwapchains = &swap_chain_;
	present.pImageIndices = &current_buffer;
	present.pWaitSemaphores = NULL;
	present.waitSemaphoreCount = 0;
	present.pResults = NULL;

	res = fpQueuePresentKHR(gfx_queue_, &present);
	// TODO: Deal with the VK_SUBOPTIMAL_WSI and VK_ERROR_OUT_OF_DATE_WSI
	// return codes
	assert(!res);

	frame->deleteList.Ingest(globalDeleteList_);
	curFrame_ ^= 1;
}

void VulkanBeginCommandBuffer(VkCommandBuffer cmd) {
	VkResult U_ASSERT_ONLY res;
	VkCommandBufferBeginInfo cmd_buf_info = {};
	cmd_buf_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmd_buf_info.pNext = NULL;
	cmd_buf_info.pInheritanceInfo = nullptr;
	cmd_buf_info.flags = 0;
	res = vkBeginCommandBuffer(cmd, &cmd_buf_info);
	assert(res == VK_SUCCESS);
}

void VulkanContext::InitObjects(HINSTANCE hInstance, HWND hWnd, bool depthPresent) {
	InitSurfaceAndQueue(hInstance, hWnd);
	InitCommandPool();

	// Create frame data

	VkCommandBufferAllocateInfo cmd_alloc = {};
	cmd_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmd_alloc.pNext = NULL;
	cmd_alloc.commandPool = cmd_pool_;
	cmd_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmd_alloc.commandBufferCount = 4;

	VkCommandBuffer cmdBuf[4];
	VkResult res = vkAllocateCommandBuffers(device_, &cmd_alloc, cmdBuf);
	assert(res == VK_SUCCESS);

	frame_[0].cmdBuf = cmdBuf[0];
	frame_[0].cmdInit = cmdBuf[1];
	frame_[0].fence = CreateFence(true);  // So it can be instantly waited on
	frame_[1].cmdBuf = cmdBuf[2];
	frame_[1].cmdInit = cmdBuf[3];
	frame_[1].fence = CreateFence(true);

	VkCommandBuffer cmd = GetInitCommandBuffer();
	InitSwapchain(cmd);
	InitDepthStencilBuffer(cmd);

	InitSurfaceRenderPass(depthPresent, true);
	InitFramebuffers(depthPresent);

	// The init command buffer will be executed as part of the first frame.
}

void VulkanContext::DestroyObjects() {
	VkCommandBuffer cmdBuf[4] = { frame_[0].cmdBuf, frame_[0].cmdInit, frame_[1].cmdBuf, frame_[1].cmdInit};

	vkFreeCommandBuffers(device_, cmd_pool_, sizeof(cmdBuf)/sizeof(cmdBuf[0]), cmdBuf);
	vkDestroyFence(device_, frame_[0].fence, nullptr);
	vkDestroyFence(device_, frame_[1].fence, nullptr);

	DestroyFramebuffers();
	DestroySurfaceRenderPass();
	DestroyDepthStencilBuffer();
	DestroySwapChain();
	DestroyCommandPool();
}

VkResult VulkanContext::InitLayerExtensionProperties(layer_properties &layer_props) {
	VkExtensionProperties *instance_extensions;
	uint32_t instance_extension_count;
	VkResult res;
	char *layer_name = NULL;

	layer_name = layer_props.properties.layerName;

	do {
		res = vkEnumerateInstanceExtensionProperties(layer_name, &instance_extension_count, NULL);
		if (res)
			return res;

		if (instance_extension_count == 0) {
			return VK_SUCCESS;
		}

		layer_props.extensions.resize(instance_extension_count);
		instance_extensions = layer_props.extensions.data();
		res = vkEnumerateInstanceExtensionProperties(
			layer_name,
			&instance_extension_count,
			instance_extensions);
	} while (res == VK_INCOMPLETE);

	return res;
}

VkResult VulkanContext::InitGlobalExtensionProperties() {
	uint32_t instance_extension_count;
	VkResult res;

	do {
		res = vkEnumerateInstanceExtensionProperties(NULL, &instance_extension_count, NULL);
		if (res)
			return res;

		if (instance_extension_count == 0) {
			return VK_SUCCESS;
		}

		instance_extension_properties.resize(instance_extension_count);
		res = vkEnumerateInstanceExtensionProperties(
			NULL,
			&instance_extension_count,
			instance_extension_properties.data());
	} while (res == VK_INCOMPLETE);

	return res;
}

VkResult VulkanContext::InitGlobalLayerProperties() {
	uint32_t instance_layer_count;
	VkLayerProperties *vk_props = NULL;
	VkResult res;

	/*
	 * It's possible, though very rare, that the number of
	 * instance layers could change. For example, installing something
	 * could include new layers that the loader would pick up
	 * between the initial query for the count and the
	 * request for VkLayerProperties. The loader indicates that
	 * by returning a VK_INCOMPLETE status and will update the
	 * the count parameter.
	 * The count parameter will be updated with the number of
	 * entries loaded into the data pointer - in case the number
	 * of layers went down or is smaller than the size given.
	 */
	do {
		res = vkEnumerateInstanceLayerProperties(&instance_layer_count, NULL);
		if (res)
			return res;

		if (instance_layer_count == 0) {
			return VK_SUCCESS;
		}

		vk_props = (VkLayerProperties *)realloc(vk_props, instance_layer_count * sizeof(VkLayerProperties));

		res = vkEnumerateInstanceLayerProperties(&instance_layer_count, vk_props);
	} while (res == VK_INCOMPLETE);

	// Now gather the extension list for each instance layer.
	for (uint32_t i = 0; i < instance_layer_count; i++) {
		layer_properties layer_props;
		layer_props.properties = vk_props[i];
		res = InitLayerExtensionProperties(layer_props);
		if (res)
			return res;
		instance_layer_properties.push_back(layer_props);
	}
	free(vk_props);

	return res;
}

VkResult VulkanContext::InitDeviceExtensionProperties(layer_properties &layer_props) {
	VkExtensionProperties *device_extensions;
	uint32_t device_extension_count;
	VkResult res;
	char *layer_name = NULL;

	layer_name = layer_props.properties.layerName;
	do {
		res = vkEnumerateDeviceExtensionProperties(
			physical_devices_[0],
			layer_name, &device_extension_count, NULL);
		if (res)
			return res;

		if (device_extension_count == 0) {
			return VK_SUCCESS;
		}

		layer_props.extensions.resize(device_extension_count);
		device_extensions = layer_props.extensions.data();
		res = vkEnumerateDeviceExtensionProperties(
			physical_devices_[0],
			layer_name,
			&device_extension_count,
			device_extensions);
	} while (res == VK_INCOMPLETE);

	return res;
}

/*
 * TODO: function description here
 */
VkResult VulkanContext::InitDeviceLayerProperties() {
	uint32_t device_layer_count;
	VkLayerProperties *vk_props = NULL;
	VkResult res;

	/*
	 * It's possible, though very rare, that the number of
	 * instance layers could change. For example, installing something
	 * could include new layers that the loader would pick up
	 * between the initial query for the count and the
	 * request for VkLayerProperties. The loader indicates that
	 * by returning a VK_INCOMPLETE status and will update the
	 * the count parameter.
	 * The count parameter will be updated with the number of
	 * entries loaded into the data pointer - in case the number
	 * of layers went down or is smaller than the size given.
	 */
	do {
		res = vkEnumerateDeviceLayerProperties(physical_devices_[0], &device_layer_count, NULL);
		if (res)
			return res;

		if (device_layer_count == 0) {
			return VK_SUCCESS;
		}

		vk_props = (VkLayerProperties *)realloc(vk_props, device_layer_count * sizeof(VkLayerProperties));

		res = vkEnumerateDeviceLayerProperties(physical_devices_[0], &device_layer_count, vk_props);
	} while (res == VK_INCOMPLETE);

	/*
	 * Now gather the extension list for each device layer.
	 */
	for (uint32_t i = 0; i < device_layer_count; i++) {
		layer_properties layer_props;
		layer_props.properties = vk_props[i];
		res = InitDeviceExtensionProperties(layer_props);
		if (res)
			return res;
		device_layer_properties.push_back(layer_props);
	}
	free(vk_props);

	return res;
}

/*
 * Return 1 (true) if all layer names specified in check_names
 * can be found in given layer properties.
 */
VkBool32 CheckLayers(const std::vector<layer_properties> &layer_props, const std::vector<const char *> &layer_names) {
	uint32_t check_count = (uint32_t)layer_names.size();
	uint32_t layer_count = (uint32_t)layer_props.size();
	for (uint32_t i = 0; i < check_count; i++) {
		VkBool32 found = 0;
		for (uint32_t j = 0; j < layer_count; j++) {
			if (!strcmp(layer_names[i], layer_props[j].properties.layerName)) {
				found = 1;
			}
		}
		if (!found) {
			std::cout << "Cannot find layer: " << layer_names[i] << std::endl;
			return 0;
		}
	}
	return 1;
}

VkResult VulkanContext::CreateDevice(int physical_device) {
  VkResult res;
  VkDeviceQueueCreateInfo queue_info = {};

  vkGetPhysicalDeviceQueueFamilyProperties(physical_devices_[0], &queue_count, NULL);
  assert(queue_count >= 1);

  queue_props.resize(queue_count);
  vkGetPhysicalDeviceQueueFamilyProperties(physical_devices_[0], &queue_count, queue_props.data());
  assert(queue_count >= 1);

  bool found = false;
  for (int i = 0; i < (int)queue_count; i++) {
		if (queue_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			queue_info.queueFamilyIndex = i;
			found = true;
			break;
		}
  }
  assert(found);
  assert(queue_count >= 1);

  /* This is as good a place as any to do this */
  vkGetPhysicalDeviceMemoryProperties(physical_devices_[0], &memory_properties);
  vkGetPhysicalDeviceProperties(physical_devices_[0], &gpu_props);

  float queue_priorities[1] = { 0.0 };
  queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_info.pNext = NULL;
  queue_info.queueCount = 1;
  queue_info.pQueuePriorities = queue_priorities;


	// Optional features
	vkGetPhysicalDeviceFeatures(physical_devices_[0], &featuresAvailable_);
	memset(&featuresEnabled_, 0, sizeof(featuresEnabled_));

	// Enable a few safe ones if they are available.
	if (featuresAvailable_.dualSrcBlend) {
		featuresEnabled_.dualSrcBlend = true;
	}
	if (featuresAvailable_.largePoints) {
		featuresEnabled_.largePoints = true;
	}
	if (featuresAvailable_.wideLines) {
		featuresEnabled_.wideLines = true;
	}
	if (featuresAvailable_.geometryShader) {
		featuresEnabled_.geometryShader = true;
	}
	if (featuresAvailable_.logicOp) {
		featuresEnabled_.logicOp = true;
	}

  VkDeviceCreateInfo device_info = {};
  device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  device_info.pNext = NULL;
  device_info.queueCreateInfoCount = 1;
  device_info.pQueueCreateInfos = &queue_info;
  device_info.enabledLayerCount = (uint32_t)device_layer_names.size();
  device_info.ppEnabledLayerNames =
          device_info.enabledLayerCount ? device_layer_names.data() : NULL;
  device_info.enabledExtensionCount = (uint32_t)device_extension_names.size();
  device_info.ppEnabledExtensionNames =
          device_info.enabledExtensionCount ? device_extension_names.data() : NULL;
	device_info.pEnabledFeatures = &featuresEnabled_;

  res = vkCreateDevice(physical_devices_[0], &device_info, NULL, &device_);
  assert(res == VK_SUCCESS);

	GET_DEVICE_PROC_ADDR(device_, CreateSwapchainKHR);
	GET_DEVICE_PROC_ADDR(device_, DestroySwapchainKHR);
	GET_DEVICE_PROC_ADDR(device_, GetSwapchainImagesKHR);
	GET_DEVICE_PROC_ADDR(device_, AcquireNextImageKHR);
	GET_DEVICE_PROC_ADDR(device_, QueuePresentKHR);

  return res;
}

VkResult VulkanContext::InitDebugMsgCallback(PFN_vkDebugReportCallbackEXT dbgFunc, int bits, void *userdata) {
	VkResult res;
	VkDebugReportCallbackEXT msg_callback;

	dbgCreateMsgCallback = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(instance_, "vkCreateDebugReportCallbackEXT");
	if (!dbgCreateMsgCallback) {
		std::cout << "GetInstanceProcAddr: Unable to find vkDbgCreateMsgCallback function." << std::endl;
		return VK_ERROR_INITIALIZATION_FAILED;
	}

	dbgDestroyMsgCallback = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(instance_, "vkDestroyDebugReportCallbackEXT");
	if (!dbgDestroyMsgCallback) {
		std::cout << "GetInstanceProcAddr: Unable to find vkDbgDestroyMsgCallback function." << std::endl;
		return VK_ERROR_INITIALIZATION_FAILED;
	}

	VkDebugReportCallbackCreateInfoEXT cb;
	cb.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
	cb.pNext = nullptr;
	cb.flags = bits;
	cb.pfnCallback = dbgFunc;
	cb.pUserData = userdata;
	res = dbgCreateMsgCallback(instance_, &cb, nullptr, &msg_callback);
	switch (res) {
	case VK_SUCCESS:
		msg_callbacks.push_back(msg_callback);
		break;
	case VK_ERROR_OUT_OF_HOST_MEMORY:
		puts("dbgCreateMsgCallback: out of host memory pointer\n");
		return VK_ERROR_INITIALIZATION_FAILED;
		break;
	default:
		puts("dbgCreateMsgCallback: unknown failure\n");
		return VK_ERROR_INITIALIZATION_FAILED;
		break;
	}
	return res;
}

void VulkanContext::DestroyDebugMsgCallback() {
  while (msg_callbacks.size() > 0) {
    dbgDestroyMsgCallback(instance_, msg_callbacks.back(), nullptr);
    msg_callbacks.pop_back();
  }
}

void VulkanContext::InitDepthStencilBuffer(VkCommandBuffer cmd) {
  VkResult U_ASSERT_ONLY res;
  bool U_ASSERT_ONLY pass;
  VkImageCreateInfo image_info = {};
  const VkFormat depth_format = VK_FORMAT_D16_UNORM;
  VkFormatProperties props;
	vkGetPhysicalDeviceFormatProperties(physical_devices_[0], depth_format, &props);
	if (props.linearTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
		image_info.tiling = VK_IMAGE_TILING_LINEAR;
	} else if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
		image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	} else {
		/* Try other depth formats? */
		std::cout << "VK_FORMAT_D16_UNORM Unsupported.\n";
		exit(-1);
	}

	image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_info.pNext = NULL;
	image_info.imageType = VK_IMAGE_TYPE_2D;
	image_info.format = depth_format;
	image_info.extent.width = width;
	image_info.extent.height = height;
	image_info.extent.depth = 1;
	image_info.mipLevels = 1;
	image_info.arrayLayers = 1;
  image_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_info.queueFamilyIndexCount = 0;
  image_info.pQueueFamilyIndices = NULL;
  image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  image_info.flags = 0;

  VkMemoryAllocateInfo mem_alloc = {};
  mem_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  mem_alloc.pNext = NULL;
  mem_alloc.allocationSize = 0;
  mem_alloc.memoryTypeIndex = 0;

  VkImageViewCreateInfo view_info = {};
  view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view_info.pNext = NULL;
  view_info.image = VK_NULL_HANDLE;
  view_info.format = depth_format;
  view_info.components.r = VK_COMPONENT_SWIZZLE_R;
  view_info.components.g = VK_COMPONENT_SWIZZLE_G;
  view_info.components.b = VK_COMPONENT_SWIZZLE_B;
  view_info.components.a = VK_COMPONENT_SWIZZLE_A;
  view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  view_info.subresourceRange.baseMipLevel = 0;
  view_info.subresourceRange.levelCount = 1;
  view_info.subresourceRange.baseArrayLayer = 0;
  view_info.subresourceRange.layerCount = 1;
  view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_info.flags = 0;

  VkMemoryRequirements mem_reqs;

  depth.format = depth_format;

  /* Create image */
  res = vkCreateImage(device_, &image_info, NULL, &depth.image);
  assert(res == VK_SUCCESS);

  vkGetImageMemoryRequirements(device_, depth.image, &mem_reqs);

  mem_alloc.allocationSize = mem_reqs.size;
  /* Use the memory properties to determine the type of memory required */
  pass = MemoryTypeFromProperties(mem_reqs.memoryTypeBits,
                                    0, /* No requirements */
                                    &mem_alloc.memoryTypeIndex);
  assert(pass);

  /* Allocate memory */
  res = vkAllocateMemory(device_, &mem_alloc, NULL, &depth.mem);
  assert(res == VK_SUCCESS);

  /* Bind memory */
  res = vkBindImageMemory(device_, depth.image, depth.mem, 0);
  assert(res == VK_SUCCESS);

  /* Set the image layout to depth stencil optimal */
  TransitionImageLayout(cmd, depth.image,
                        VK_IMAGE_ASPECT_DEPTH_BIT,
                        VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

  /* Create image view */
	view_info.image = depth.image;
	res = vkCreateImageView(device_, &view_info, NULL, &depth.view);
	assert(res == VK_SUCCESS);
}

void VulkanContext::InitSurfaceAndQueue(HINSTANCE conn, HWND wnd) {
	connection = conn;
	window = wnd;

	RECT rc;
	GetClientRect(wnd, &rc);
	width = rc.right - rc.left;
	height = rc.bottom - rc.top;

	VkResult U_ASSERT_ONLY res;

#ifdef _WIN32
	VkWin32SurfaceCreateInfoKHR win32;
	win32.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	win32.flags = 0;
	win32.hwnd = wnd;
	win32.hinstance = conn;
	res = vkCreateWin32SurfaceKHR(instance_, &win32, nullptr, &surface);

#else  // _WIN32
	res = vkCreateXcbSurfaceKHR(instance_, info.connection, info.window, NULL, &info.surface);
#endif // _WIN32

	assert(res == VK_SUCCESS);

	// Iterate over each queue to learn whether it supports presenting:
	VkBool32* supportsPresent = new VkBool32[queue_count];
	for (uint32_t i = 0; i < queue_count; i++) {
		fpGetPhysicalDeviceSurfaceSupportKHR(physical_devices_[0], i, surface, &supportsPresent[i]);
	}

	// Search for a graphics queue and a present queue in the array of queue
	// families, try to find one that supports both
	uint32_t graphicsQueueNodeIndex = UINT32_MAX;
	uint32_t presentQueueNodeIndex = UINT32_MAX;
	for (uint32_t i = 0; i < queue_count; i++) {
		if ((queue_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
			if (graphicsQueueNodeIndex == UINT32_MAX) {
				graphicsQueueNodeIndex = i;
			}

			if (supportsPresent[i] == VK_TRUE) {
				graphicsQueueNodeIndex = i;
				presentQueueNodeIndex = i;
				break;
			}
		}
	}
	if (presentQueueNodeIndex == UINT32_MAX) {
		// If didn't find a queue that supports both graphics and present, then
		// find a separate present queue.
		for (uint32_t i = 0; i < queue_count; ++i) {
			if (supportsPresent[i] == VK_TRUE) {
				presentQueueNodeIndex = i;
				break;
			}
		}
	}
	delete[] supportsPresent;

	// Generate error if could not find both a graphics and a present queue
	if (graphicsQueueNodeIndex == UINT32_MAX || presentQueueNodeIndex == UINT32_MAX) {
		std::cout << "Could not find a graphics and a present queue\nCould not find a graphics and a present queue\n";
		exit(-1);
	}

	graphics_queue_family_index_ = graphicsQueueNodeIndex;

	// Get the list of VkFormats that are supported:
	uint32_t formatCount;
	res = fpGetPhysicalDeviceSurfaceFormatsKHR(physical_devices_[0],
		surface,
		&formatCount, NULL);
	assert(res == VK_SUCCESS);
	VkSurfaceFormatKHR *surfFormats = new VkSurfaceFormatKHR[formatCount];
	res = fpGetPhysicalDeviceSurfaceFormatsKHR(physical_devices_[0],
		surface,
		&formatCount, surfFormats);
	assert(res == VK_SUCCESS);
	// If the format list includes just one entry of VK_FORMAT_UNDEFINED,
	// the surface has no preferred format.  Otherwise, at least one
	// supported format will be returned.
	if (formatCount == 1 && surfFormats[0].format == VK_FORMAT_UNDEFINED) {
		swapchain_format = VK_FORMAT_B8G8R8A8_UNORM;
	} else {
		assert(formatCount >= 1);
		swapchain_format = surfFormats[0].format;
	}
	delete[] surfFormats;

	vkGetDeviceQueue(device_, graphics_queue_family_index_, 0, &gfx_queue_);

	VkSemaphoreCreateInfo acquireSemaphoreCreateInfo;
	acquireSemaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	acquireSemaphoreCreateInfo.pNext = NULL;
	acquireSemaphoreCreateInfo.flags = 0;

	res = vkCreateSemaphore(device_,
		&acquireSemaphoreCreateInfo,
		NULL,
		&acquireSemaphore);
	assert(res == VK_SUCCESS);
}

void VulkanContext::InitSwapchain(VkCommandBuffer cmd) {
	VkResult U_ASSERT_ONLY res;
	VkSurfaceCapabilitiesKHR surfCapabilities;

	res = fpGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_devices_[0],
		surface,
		&surfCapabilities);
	assert(res == VK_SUCCESS);

	uint32_t presentModeCount;
	res = fpGetPhysicalDeviceSurfacePresentModesKHR(physical_devices_[0],
		surface,
		&presentModeCount, NULL);
	assert(res == VK_SUCCESS);
	VkPresentModeKHR *presentModes =
		(VkPresentModeKHR *)malloc(presentModeCount * sizeof(VkPresentModeKHR));
	assert(presentModes);
	res = fpGetPhysicalDeviceSurfacePresentModesKHR(physical_devices_[0],
		surface,
		&presentModeCount, presentModes);
	assert(res == VK_SUCCESS);

	VkExtent2D swapChainExtent;
	// width and height are either both -1, or both not -1.
	if (surfCapabilities.currentExtent.width == -1)
	{
		// If the surface size is undefined, the size is set to
		// the size of the images requested.
		swapChainExtent.width = width;
		swapChainExtent.height = height;
	} else
	{
		// If the surface size is defined, the swap chain size must match
		swapChainExtent = surfCapabilities.currentExtent;
	}

	// If mailbox mode is available, use it, as is the lowest-latency non-
	// tearing mode.  If not, try IMMEDIATE which will usually be available,
	// and is fastest (though it tears).  If not, fall back to FIFO which is
	// always available.
	VkPresentModeKHR swapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR;
	for (size_t i = 0; i < presentModeCount; i++) {
		if ((flags_ & VULKAN_FLAG_PRESENT_MAILBOX) && presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
			swapchainPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
			break;
		}
		if ((flags_ & VULKAN_FLAG_PRESENT_IMMEDIATE) && presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) {
			swapchainPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
			break;
		}
	}

	// Determine the number of VkImage's to use in the swap chain (we desire to
	// own only 1 image at a time, besides the images being displayed and
	// queued for display):
	uint32_t desiredNumberOfSwapChainImages = surfCapabilities.minImageCount + 1;
	if ((surfCapabilities.maxImageCount > 0) &&
		(desiredNumberOfSwapChainImages > surfCapabilities.maxImageCount))
	{
		// Application must settle for fewer images than desired:
		desiredNumberOfSwapChainImages = surfCapabilities.maxImageCount;
	}

	VkSurfaceTransformFlagBitsKHR preTransform;
	if (surfCapabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
		preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	} else {
		preTransform = surfCapabilities.currentTransform;
	}

	VkSwapchainCreateInfoKHR swap_chain_info = {};
	swap_chain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swap_chain_info.pNext = NULL;
	swap_chain_info.surface = surface;
	swap_chain_info.minImageCount = desiredNumberOfSwapChainImages;
	swap_chain_info.imageFormat = swapchain_format;
	swap_chain_info.imageExtent.width = swapChainExtent.width;
	swap_chain_info.imageExtent.height = swapChainExtent.height;
	swap_chain_info.preTransform = preTransform;
	swap_chain_info.imageArrayLayers = 1;
	swap_chain_info.presentMode = swapchainPresentMode;
	swap_chain_info.oldSwapchain = VK_NULL_HANDLE;
	swap_chain_info.clipped = true;
	swap_chain_info.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
	swap_chain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	swap_chain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swap_chain_info.queueFamilyIndexCount = 0;
	swap_chain_info.pQueueFamilyIndices = NULL;

	res = fpCreateSwapchainKHR(device_, &swap_chain_info, NULL, &swap_chain_);
	assert(res == VK_SUCCESS);

	res = fpGetSwapchainImagesKHR(device_, swap_chain_,
		&swapchainImageCount, NULL);
	assert(res == VK_SUCCESS);

	VkImage* swapchainImages = (VkImage*)malloc(swapchainImageCount * sizeof(VkImage));
	assert(swapchainImages);
	res = fpGetSwapchainImagesKHR(device_, swap_chain_, &swapchainImageCount, swapchainImages);
	assert(res == VK_SUCCESS);

	for (uint32_t i = 0; i < swapchainImageCount; i++) {
		swap_chain_buffer sc_buffer;

		VkImageViewCreateInfo color_image_view = {};
		color_image_view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		color_image_view.pNext = NULL;
		color_image_view.format = swapchain_format;
		color_image_view.components.r = VK_COMPONENT_SWIZZLE_R;
		color_image_view.components.g = VK_COMPONENT_SWIZZLE_G;
		color_image_view.components.b = VK_COMPONENT_SWIZZLE_B;
		color_image_view.components.a = VK_COMPONENT_SWIZZLE_A;
		color_image_view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		color_image_view.subresourceRange.baseMipLevel = 0;
		color_image_view.subresourceRange.levelCount = 1;
		color_image_view.subresourceRange.baseArrayLayer = 0;
		color_image_view.subresourceRange.layerCount = 1;
		color_image_view.viewType = VK_IMAGE_VIEW_TYPE_2D;
		color_image_view.flags = 0;

		sc_buffer.image = swapchainImages[i];

		// TODO: Pre-set them to PRESENT_SRC_KHR, as the first thing we do after acquiring
		// in image to render to will be to transition them away from that.
		TransitionImageLayout(cmd, sc_buffer.image,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

		color_image_view.image = sc_buffer.image;

		res = vkCreateImageView(device_,
			&color_image_view, NULL, &sc_buffer.view);
		swapChainBuffers.push_back(sc_buffer);
		assert(res == VK_SUCCESS);
	}
	free(swapchainImages);

	current_buffer = 0;
}

void VulkanContext::InitSurfaceRenderPass(bool include_depth, bool clear) {
  VkResult U_ASSERT_ONLY res;
  /* Need attachments for render target and depth buffer */
  VkAttachmentDescription attachments[2];
  attachments[0].format = swapchain_format;
  attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[0].loadOp = clear?VK_ATTACHMENT_LOAD_OP_CLEAR:VK_ATTACHMENT_LOAD_OP_LOAD;
  attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  attachments[0].flags = 0;

  if (include_depth) {
		attachments[1].format = depth.format;
		attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[1].loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
		attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		attachments[1].flags = 0;
  }

  VkAttachmentReference color_reference = {};
  color_reference.attachment = 0;
  color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference depth_reference = {};
  depth_reference.attachment = 1;
  depth_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.flags = 0;
  subpass.inputAttachmentCount = 0;
  subpass.pInputAttachments = NULL;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_reference;
  subpass.pResolveAttachments = NULL;
  subpass.pDepthStencilAttachment = include_depth?&depth_reference:NULL;
  subpass.preserveAttachmentCount = 0;
  subpass.pPreserveAttachments = NULL;

  VkRenderPassCreateInfo rp_info = {};
  rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rp_info.pNext = NULL;
  rp_info.attachmentCount = include_depth ? 2 : 1;
  rp_info.pAttachments = attachments;
  rp_info.subpassCount = 1;
  rp_info.pSubpasses = &subpass;
  rp_info.dependencyCount = 0;
  rp_info.pDependencies = NULL;

  res = vkCreateRenderPass(device_, &rp_info, NULL, &surface_render_pass_);
  assert(res == VK_SUCCESS);
}

void VulkanContext::InitFramebuffers(bool include_depth) {
  VkResult U_ASSERT_ONLY res;
  VkImageView attachments[2];
  attachments[1] = depth.view;

  VkFramebufferCreateInfo fb_info = {};
  fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  fb_info.pNext = NULL;
  fb_info.renderPass = surface_render_pass_;
  fb_info.attachmentCount = include_depth ? 2 : 1;
  fb_info.pAttachments = attachments;
  fb_info.width  = width;
  fb_info.height = height;
  fb_info.layers = 1;

  framebuffers_.resize(swapchainImageCount);

  for (uint32_t i = 0; i < swapchainImageCount; i++) {
    attachments[0] = swapChainBuffers[i].view;
    res = vkCreateFramebuffer(device_, &fb_info, NULL, &framebuffers_[i]);
    assert(res == VK_SUCCESS);
	}
}

void VulkanContext::InitCommandPool() {
  VkResult U_ASSERT_ONLY res;

  VkCommandPoolCreateInfo cmd_pool_info = {};
  cmd_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cmd_pool_info.pNext = NULL;
  cmd_pool_info.queueFamilyIndex = graphics_queue_family_index_;
  cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

  res = vkCreateCommandPool(device_, &cmd_pool_info, NULL, &cmd_pool_);
  assert(res == VK_SUCCESS);
}

void VulkanTexture::Create(VulkanContext *vulkan, int w, int h, VkFormat format) {
	tex_width = w;
	tex_height = h;
	format_ = format;

	bool U_ASSERT_ONLY pass;

	VkFormatProperties formatProps;
	vkGetPhysicalDeviceFormatProperties(vulkan->GetPhysicalDevice(), format, &formatProps);

	// See if we can use a linear tiled image for a texture, if not, we will need a staging image for the texture data.
	// Linear tiling is usually only supported for 2D non-array textures.
	needStaging = (!(formatProps.linearTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) ? true : false;

	VkImageCreateInfo image_create_info = {};
	image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_create_info.pNext = NULL;
	image_create_info.imageType = VK_IMAGE_TYPE_2D;
	image_create_info.format = format;
	image_create_info.extent.width = tex_width;
	image_create_info.extent.height = tex_height;
	image_create_info.extent.depth = 1;
	image_create_info.mipLevels = 1;
	image_create_info.arrayLayers = 1;
	image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_create_info.tiling = VK_IMAGE_TILING_LINEAR;
	image_create_info.usage = needStaging ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : VK_IMAGE_USAGE_SAMPLED_BIT;
	image_create_info.queueFamilyIndexCount = 0;
	image_create_info.pQueueFamilyIndices = NULL;
	image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_create_info.flags = 0;

	VkMemoryAllocateInfo mem_alloc = {};
	mem_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	mem_alloc.pNext = NULL;
	mem_alloc.allocationSize = 0;
	mem_alloc.memoryTypeIndex = 0;

	/* Create a mappable image.  It will be the texture if linear images are ok to be textures */
	/* or it will be the staging image if they are not.                                        */
	VkResult res = vkCreateImage(vulkan->GetDevice(), &image_create_info, NULL, &mappableImage);
	assert(res == VK_SUCCESS);

	vkGetImageMemoryRequirements(vulkan->GetDevice(), mappableImage, &mem_reqs);
	assert(res == VK_SUCCESS);

	mem_alloc.allocationSize = mem_reqs.size;

	/* Find the memory type that is host mappable */
	pass = vulkan->MemoryTypeFromProperties(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &mem_alloc.memoryTypeIndex);
	assert(pass);

	res = vkAllocateMemory(vulkan->GetDevice(), &mem_alloc, NULL, &(mappableMemory));
	assert(res == VK_SUCCESS);

	res = vkBindImageMemory(vulkan->GetDevice(), mappableImage, mappableMemory, 0);
	assert(res == VK_SUCCESS);
}

uint8_t *VulkanTexture::Lock(VulkanContext *vulkan, int *rowPitch) {
	VkImageSubresource subres = {};
	subres.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subres.mipLevel = 0;
	subres.arrayLayer = 0;

	VkSubresourceLayout layout;
	void *data;

	/* Get the subresource layout so we know what the row pitch is */
	vkGetImageSubresourceLayout(vulkan->GetDevice(), mappableImage, &subres, &layout);

	VkResult res = vkMapMemory(vulkan->GetDevice(), mappableMemory, 0, mem_reqs.size, 0, &data);
	assert(res == VK_SUCCESS);

	*rowPitch = (int)layout.rowPitch;
	return (uint8_t *)data;
}

void VulkanTexture::Unlock(VulkanContext *vulkan) {
	vkUnmapMemory(vulkan->GetDevice(), mappableMemory);

	VkCommandBuffer cmd = vulkan->GetInitCommandBuffer();

	if (!needStaging) {
		/* If we can use the linear tiled image as a texture, just do it */
		image = mappableImage;
		mem = mappableMemory;
		imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		TransitionImageLayout(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, imageLayout);
	} else {
		VkImageCreateInfo image_create_info = {};
		image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		image_create_info.pNext = NULL;
		image_create_info.imageType = VK_IMAGE_TYPE_2D;
		image_create_info.format = format_;
		image_create_info.extent.width = tex_width;
		image_create_info.extent.height = tex_height;
		image_create_info.extent.depth = 1;
		image_create_info.mipLevels = 1;
		image_create_info.arrayLayers = 1;
		image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
		image_create_info.queueFamilyIndexCount = 0;
		image_create_info.pQueueFamilyIndices = NULL;
		image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		image_create_info.flags = 0;
		// The mappable image cannot be our texture, so create an optimally tiled image and blit to it
		image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
		image_create_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VkResult res = vkCreateImage(vulkan->GetDevice(), &image_create_info, NULL, &image);
		assert(res == VK_SUCCESS);

		vkGetImageMemoryRequirements(vulkan->GetDevice(), image, &mem_reqs);

		VkMemoryAllocateInfo mem_alloc = {};
		mem_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		mem_alloc.pNext = NULL;
		mem_alloc.allocationSize = 0;
		mem_alloc.memoryTypeIndex = 0;
		mem_alloc.allocationSize = mem_reqs.size;

		/* Find memory type - dont specify any mapping requirements */
		bool pass = vulkan->MemoryTypeFromProperties(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &mem_alloc.memoryTypeIndex);
		assert(pass);

		/* allocate memory */
		res = vkAllocateMemory(vulkan->GetDevice(), &mem_alloc, NULL, &mem);
		assert(res == VK_SUCCESS);

		/* bind memory */
		res = vkBindImageMemory(vulkan->GetDevice(), image, mem, 0);
		assert(res == VK_SUCCESS);

		/* Since we're going to blit from the mappable image, set its layout to SOURCE_OPTIMAL */
		/* Side effect is that this will create info.cmd                                       */
		TransitionImageLayout(cmd, mappableImage,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

		/* Since we're going to blit to the texture image, set its layout to DESTINATION_OPTIMAL */
		TransitionImageLayout(cmd, image,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		VkImageCopy copy_region;
		copy_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy_region.srcSubresource.mipLevel = 0;
		copy_region.srcSubresource.baseArrayLayer = 0;
		copy_region.srcSubresource.layerCount = 1;
		copy_region.srcOffset.x = 0;
		copy_region.srcOffset.y = 0;
		copy_region.srcOffset.z = 0;
		copy_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy_region.dstSubresource.mipLevel = 0;
		copy_region.dstSubresource.baseArrayLayer = 0;
		copy_region.dstSubresource.layerCount = 1;
		copy_region.dstOffset.x = 0;
		copy_region.dstOffset.y = 0;
		copy_region.dstOffset.z = 0;
		copy_region.extent.width = tex_width;
		copy_region.extent.height = tex_height;
		copy_region.extent.depth = 1;

		// Put the copy command into the command buffer
		vkCmdCopyImage(cmd,
			mappableImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &copy_region);

		assert(res == VK_SUCCESS);

		/* Set the layout for the texture image from DESTINATION_OPTIMAL to SHADER_READ_ONLY */
		imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		TransitionImageLayout(cmd, image,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			imageLayout);

		vulkan->QueueDelete(mappableMemory);
		vulkan->QueueDelete(mappableImage);
	}

	VkImageViewCreateInfo view_info = {};
	view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view_info.pNext = NULL;
	view_info.image = VK_NULL_HANDLE;
	view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = format_;
	view_info.components.r = VK_COMPONENT_SWIZZLE_R;
	view_info.components.g = VK_COMPONENT_SWIZZLE_G;
	view_info.components.b = VK_COMPONENT_SWIZZLE_B;
	view_info.components.a = VK_COMPONENT_SWIZZLE_A;
	view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view_info.subresourceRange.baseMipLevel = 0;
	view_info.subresourceRange.levelCount = 1;
	view_info.subresourceRange.baseArrayLayer = 0;
	view_info.subresourceRange.layerCount = 1;

	/* create image view */
	view_info.image = image;
	VkResult res = vkCreateImageView(vulkan->GetDevice(), &view_info, NULL, &view);
	assert(res == VK_SUCCESS);
}

void VulkanTexture::Destroy(VulkanContext *vulkan) {
	vulkan->QueueDelete(view);
	vulkan->QueueDelete(image);
	vulkan->QueueDelete(mem);

	view = NULL;
	image = NULL;
	mem = NULL;
}

VkFence VulkanContext::CreateFence(bool presignalled) {
	VkFence fence;
  VkFenceCreateInfo fenceInfo;
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.pNext = NULL;
  fenceInfo.flags = presignalled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;
  vkCreateFence(device_, &fenceInfo, NULL, &fence);
	return fence;
}

void VulkanContext::WaitAndResetFence(VkFence fence) {
	vkWaitForFences(device_, 1, &fence, true, UINT64_MAX);
	vkResetFences(device_, 1, &fence);
}

void VulkanContext::DestroyCommandPool() {
  vkDestroyCommandPool(device_, cmd_pool_, NULL);
	cmd_pool_ = nullptr;
}

void VulkanContext::DestroyDepthStencilBuffer() {
	vkDestroyImageView(device_, depth.view, NULL);
	vkDestroyImage(device_, depth.image, NULL);
	vkFreeMemory(device_, depth.mem, NULL);
	depth.view = NULL;
	depth.image = NULL;
	depth.mem = NULL;
}

void VulkanContext::DestroySwapChain() {
	for (uint32_t i = 0; i < swapchainImageCount; i++) {
		vkDestroyImageView(device_, swapChainBuffers[i].view, NULL);
	}
	fpDestroySwapchainKHR(device_, swap_chain_, NULL);
	swap_chain_ = nullptr;
	swapChainBuffers.clear();
	vkDestroySemaphore(device_, acquireSemaphore, NULL);
}

void VulkanContext::DestroyFramebuffers() {
  for (uint32_t i = 0; i < framebuffers_.size(); i++) {
    vkDestroyFramebuffer(device_, framebuffers_[i], NULL);
  }
	framebuffers_.clear();
}

void VulkanContext::DestroySurfaceRenderPass() {
  vkDestroyRenderPass(device_, surface_render_pass_, NULL);
	surface_render_pass_ = NULL;
}

void VulkanContext::DestroyDevice() {
  vkDestroyDevice(device_, NULL);
	device_ = NULL;
}

VkPipelineCache VulkanContext::CreatePipelineCache() {
	VkPipelineCache cache;
	VkPipelineCacheCreateInfo pc;
	pc.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	pc.pNext = nullptr;
	pc.pInitialData = nullptr;
	pc.initialDataSize = 0;
	pc.flags = 0;
	VkResult res = vkCreatePipelineCache(device_, &pc, nullptr, &cache);
	assert(VK_SUCCESS == res);
	return cache;
}

void TransitionImageLayout(
	VkCommandBuffer cmd,
	VkImage image,
	VkImageAspectFlags aspectMask,
	VkImageLayout old_image_layout,
	VkImageLayout new_image_layout) {
	/* DEPENDS on info.cmd and info.queue initialized */

	VkImageMemoryBarrier image_memory_barrier = {};
	image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	image_memory_barrier.pNext = NULL;
	image_memory_barrier.srcAccessMask = 0;
	image_memory_barrier.dstAccessMask = 0;
	image_memory_barrier.oldLayout = old_image_layout;
	image_memory_barrier.newLayout = new_image_layout;
	image_memory_barrier.image = image;
	image_memory_barrier.subresourceRange.aspectMask = aspectMask;
	image_memory_barrier.subresourceRange.baseMipLevel = 0;
	image_memory_barrier.subresourceRange.levelCount = 1;
	image_memory_barrier.subresourceRange.layerCount = 1;
	if (old_image_layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
		image_memory_barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	}

	if (old_image_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
		image_memory_barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	}

	if (new_image_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
		image_memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	}

	if (new_image_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
		/* Make sure anything that was copying from this image has completed */
		image_memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT;
	}

	if (new_image_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
		/* Make sure any Copy or CPU writes to image are flushed */
		if (old_image_layout != VK_IMAGE_LAYOUT_UNDEFINED) {
			image_memory_barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
		}
		image_memory_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	}

	if (new_image_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
		image_memory_barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
	}

	if (new_image_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
		image_memory_barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
	}

	VkPipelineStageFlags src_stages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	VkPipelineStageFlags dest_stages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

	vkCmdPipelineBarrier(cmd, src_stages, dest_stages, 0, 0, nullptr, 0, nullptr, 1, &image_memory_barrier);
}

void init_resources(TBuiltInResource &Resources) {
	Resources.maxLights = 32;
	Resources.maxClipPlanes = 6;
	Resources.maxTextureUnits = 32;
	Resources.maxTextureCoords = 32;
	Resources.maxVertexAttribs = 64;
	Resources.maxVertexUniformComponents = 4096;
	Resources.maxVaryingFloats = 64;
	Resources.maxVertexTextureImageUnits = 32;
	Resources.maxCombinedTextureImageUnits = 80;
	Resources.maxTextureImageUnits = 32;
	Resources.maxFragmentUniformComponents = 4096;
	Resources.maxDrawBuffers = 32;
	Resources.maxVertexUniformVectors = 128;
	Resources.maxVaryingVectors = 8;
	Resources.maxFragmentUniformVectors = 16;
	Resources.maxVertexOutputVectors = 16;
	Resources.maxFragmentInputVectors = 15;
	Resources.minProgramTexelOffset = -8;
	Resources.maxProgramTexelOffset = 7;
	Resources.maxClipDistances = 8;
	Resources.maxComputeWorkGroupCountX = 65535;
	Resources.maxComputeWorkGroupCountY = 65535;
	Resources.maxComputeWorkGroupCountZ = 65535;
	Resources.maxComputeWorkGroupSizeX = 1024;
	Resources.maxComputeWorkGroupSizeY = 1024;
	Resources.maxComputeWorkGroupSizeZ = 64;
	Resources.maxComputeUniformComponents = 1024;
	Resources.maxComputeTextureImageUnits = 16;
	Resources.maxComputeImageUniforms = 8;
	Resources.maxComputeAtomicCounters = 8;
	Resources.maxComputeAtomicCounterBuffers = 1;
	Resources.maxVaryingComponents = 60;
	Resources.maxVertexOutputComponents = 64;
	Resources.maxGeometryInputComponents = 64;
	Resources.maxGeometryOutputComponents = 128;
	Resources.maxFragmentInputComponents = 128;
	Resources.maxImageUnits = 8;
	Resources.maxCombinedImageUnitsAndFragmentOutputs = 8;
	Resources.maxCombinedShaderOutputResources = 8;
	Resources.maxImageSamples = 0;
	Resources.maxVertexImageUniforms = 0;
	Resources.maxTessControlImageUniforms = 0;
	Resources.maxTessEvaluationImageUniforms = 0;
	Resources.maxGeometryImageUniforms = 0;
	Resources.maxFragmentImageUniforms = 8;
	Resources.maxCombinedImageUniforms = 8;
	Resources.maxGeometryTextureImageUnits = 16;
	Resources.maxGeometryOutputVertices = 256;
	Resources.maxGeometryTotalOutputComponents = 1024;
	Resources.maxGeometryUniformComponents = 1024;
	Resources.maxGeometryVaryingComponents = 64;
	Resources.maxTessControlInputComponents = 128;
	Resources.maxTessControlOutputComponents = 128;
	Resources.maxTessControlTextureImageUnits = 16;
	Resources.maxTessControlUniformComponents = 1024;
	Resources.maxTessControlTotalOutputComponents = 4096;
	Resources.maxTessEvaluationInputComponents = 128;
	Resources.maxTessEvaluationOutputComponents = 128;
	Resources.maxTessEvaluationTextureImageUnits = 16;
	Resources.maxTessEvaluationUniformComponents = 1024;
	Resources.maxTessPatchComponents = 120;
	Resources.maxPatchVertices = 32;
	Resources.maxTessGenLevel = 64;
	Resources.maxViewports = 16;
	Resources.maxVertexAtomicCounters = 0;
	Resources.maxTessControlAtomicCounters = 0;
	Resources.maxTessEvaluationAtomicCounters = 0;
	Resources.maxGeometryAtomicCounters = 0;
	Resources.maxFragmentAtomicCounters = 8;
	Resources.maxCombinedAtomicCounters = 8;
	Resources.maxAtomicCounterBindings = 1;
	Resources.maxVertexAtomicCounterBuffers = 0;
	Resources.maxTessControlAtomicCounterBuffers = 0;
	Resources.maxTessEvaluationAtomicCounterBuffers = 0;
	Resources.maxGeometryAtomicCounterBuffers = 0;
	Resources.maxFragmentAtomicCounterBuffers = 1;
	Resources.maxCombinedAtomicCounterBuffers = 1;
	Resources.maxAtomicCounterBufferSize = 16384;
	Resources.maxTransformFeedbackBuffers = 4;
	Resources.maxTransformFeedbackInterleavedComponents = 64;
	Resources.maxCullDistances = 8;
	Resources.maxCombinedClipAndCullDistances = 8;
	Resources.maxSamples = 4;
	Resources.limits.nonInductiveForLoops = 1;
	Resources.limits.whileLoops = 1;
	Resources.limits.doWhileLoops = 1;
	Resources.limits.generalUniformIndexing = 1;
	Resources.limits.generalAttributeMatrixVectorIndexing = 1;
	Resources.limits.generalVaryingIndexing = 1;
	Resources.limits.generalSamplerIndexing = 1;
	Resources.limits.generalVariableIndexing = 1;
	Resources.limits.generalConstantMatrixVectorIndexing = 1;
}

EShLanguage FindLanguage(const VkShaderStageFlagBits shader_type) {
	switch (shader_type) {
	case VK_SHADER_STAGE_VERTEX_BIT:
		return EShLangVertex;

	case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
		return EShLangTessControl;

	case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
		return EShLangTessEvaluation;

	case VK_SHADER_STAGE_GEOMETRY_BIT:
		return EShLangGeometry;

	case VK_SHADER_STAGE_FRAGMENT_BIT:
		return EShLangFragment;

	case VK_SHADER_STAGE_COMPUTE_BIT:
		return EShLangCompute;

	default:
		return EShLangVertex;
	}
}

// Compile a given string containing GLSL into SPV for use by VK
// Return value of false means an error was encountered.
bool GLSLtoSPV(const VkShaderStageFlagBits shader_type,
	const char *pshader,
	std::vector<unsigned int> &spirv, std::string *errorMessage) {

	glslang::TProgram& program = *new glslang::TProgram;
	const char *shaderStrings[1];
	TBuiltInResource Resources;
	init_resources(Resources);

	// Enable SPIR-V and Vulkan rules when parsing GLSL
	EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);

	EShLanguage stage = FindLanguage(shader_type);
	glslang::TShader* shader = new glslang::TShader(stage);

	shaderStrings[0] = pshader;
	shader->setStrings(shaderStrings, 1);

	if (!shader->parse(&Resources, 100, false, messages)) {
		puts(shader->getInfoLog());
		puts(shader->getInfoDebugLog());
		if (errorMessage) {
			*errorMessage = shader->getInfoLog();
			(*errorMessage) += shader->getInfoDebugLog();
		}
		return false; // something didn't work
	}

	program.addShader(shader);

	//
	// Program-level processing...
	//

	if (!program.link(messages)) {
		puts(shader->getInfoLog());
		puts(shader->getInfoDebugLog());
		if (errorMessage) {
			*errorMessage = shader->getInfoLog();
			(*errorMessage) += shader->getInfoDebugLog();
		}
		return false;
	}

	// Can't fail, parsing worked, "linking" worked.
	glslang::GlslangToSpv(*program.getIntermediate(stage), spirv);
	return true;
}

void init_glslang() {
	glslang::InitializeProcess();
}

void finalize_glslang() {
	glslang::FinalizeProcess();
}

const char *VulkanResultToString(VkResult res) {
	switch (res) {
	case VK_NOT_READY: return "VK_NOT_READY";
	case VK_TIMEOUT: return "VK_TIMEOUT";
	case VK_EVENT_SET: return "VK_EVENT_SET";
	case VK_EVENT_RESET: return "VK_EVENT_RESET";
	case VK_INCOMPLETE: return "VK_INCOMPLETE";
	case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
	case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
	case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
	case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
	case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
	case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
	case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
	case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
	case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
	case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
	case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
	case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
	case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
	case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
	case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR: return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
	case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
	default:
		return "Unknown";
	}
}

void VulkanAssertImpl(VkResult check, const char *function, const char *file, int line) {
	const char *error = "(none)";
}

void VulkanFramebuffer::Create(VulkanContext *vulkan, int w, int h, VkFormat format) {

}
// void TransitionToImage()

void VulkanFramebuffer::BeginPass(VkCommandBuffer cmd) {

}
void VulkanFramebuffer::EndPass(VkCommandBuffer cmd) {

}
void VulkanFramebuffer::TransitionToTexture(VkCommandBuffer cmd) {

}

VkImageView VulkanFramebuffer::GetColorImageView() {
	return nullptr;
}
