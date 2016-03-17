#include "Common/Vulkan/VulkanImage.h"

VkResult VulkanTexture::Create(int w, int h, VkFormat format) {
	tex_width = w;
	tex_height = h;
	format_ = format;

	VkFormatProperties formatProps;
	vkGetPhysicalDeviceFormatProperties(vulkan_->GetPhysicalDevice(), format, &formatProps);

	// See if we can use a linear tiled image for a texture, if not, we will need a staging image for the texture data.
	// Linear tiling is usually only supported for 2D non-array textures.
	// needStaging = (!(formatProps.linearTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) ? true : false;
	// Always stage.
	needStaging = true;

	return VK_SUCCESS;
}

void VulkanTexture::CreateMappableImage() {
	// If we already have a mappableImage, forget it.
	if (mappableImage) {
		vulkan_->Delete().QueueDeleteImage(mappableImage);
		mappableImage = VK_NULL_HANDLE;
	}
	if (mappableMemory) {
		vulkan_->Delete().QueueDeleteDeviceMemory(mappableMemory);
		mappableMemory = VK_NULL_HANDLE;
	}

	bool U_ASSERT_ONLY pass;

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
	image_create_info.tiling = VK_IMAGE_TILING_LINEAR;
	image_create_info.usage = needStaging ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : VK_IMAGE_USAGE_SAMPLED_BIT;
	image_create_info.queueFamilyIndexCount = 0;
	image_create_info.pQueueFamilyIndices = NULL;
	image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_create_info.flags = 0;
	image_create_info.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;

	VkMemoryAllocateInfo mem_alloc = {};
	mem_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	mem_alloc.pNext = NULL;
	mem_alloc.allocationSize = 0;
	mem_alloc.memoryTypeIndex = 0;

	// Create a mappable image.  It will be the texture if linear images are ok to be textures
	// or it will be the staging image if they are not.
	VkResult res = vkCreateImage(vulkan_->GetDevice(), &image_create_info, NULL, &mappableImage);
	assert(res == VK_SUCCESS);

	vkGetImageMemoryRequirements(vulkan_->GetDevice(), mappableImage, &mem_reqs);
	assert(res == VK_SUCCESS);

	mem_alloc.allocationSize = mem_reqs.size;

	// Find the memory type that is host mappable.
	pass = vulkan_->MemoryTypeFromProperties(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &mem_alloc.memoryTypeIndex);
	assert(pass);

	res = vkAllocateMemory(vulkan_->GetDevice(), &mem_alloc, NULL, &mappableMemory);
	assert(res == VK_SUCCESS);

	res = vkBindImageMemory(vulkan_->GetDevice(), mappableImage, mappableMemory, 0);
	assert(res == VK_SUCCESS);
}

uint8_t *VulkanTexture::Lock(int level, int *rowPitch) {
	CreateMappableImage();

	VkImageSubresource subres = {};
	subres.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subres.mipLevel = 0;
	subres.arrayLayer = 0;

	VkSubresourceLayout layout;
	void *data;

	// Get the subresource layout so we know what the row pitch is
	vkGetImageSubresourceLayout(vulkan_->GetDevice(), mappableImage, &subres, &layout);
	VkResult res = vkMapMemory(vulkan_->GetDevice(), mappableMemory, layout.offset, layout.size, 0, &data);
	assert(res == VK_SUCCESS);

	*rowPitch = (int)layout.rowPitch;
	return (uint8_t *)data;
}

void VulkanTexture::Unlock() {
	vkUnmapMemory(vulkan_->GetDevice(), mappableMemory);

	VkCommandBuffer cmd = vulkan_->GetInitCommandBuffer();

	// if we already have an image, queue it for destruction and forget it.
	Wipe();
	if (!needStaging) {
		// If we can use the linear tiled image as a texture, just do it
		image = mappableImage;
		mem = mappableMemory;
		TransitionImageLayout(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		// Make sure we don't accidentally delete the main image.
		mappableImage = VK_NULL_HANDLE;
		mappableMemory = VK_NULL_HANDLE;
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

		VkResult res = vkCreateImage(vulkan_->GetDevice(), &image_create_info, NULL, &image);
		assert(res == VK_SUCCESS);

		vkGetImageMemoryRequirements(vulkan_->GetDevice(), image, &mem_reqs);

		VkMemoryAllocateInfo mem_alloc = {};
		mem_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		mem_alloc.pNext = NULL;
		mem_alloc.memoryTypeIndex = 0;
		mem_alloc.allocationSize = mem_reqs.size;

		// Find memory type - don't specify any mapping requirements
		bool pass = vulkan_->MemoryTypeFromProperties(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &mem_alloc.memoryTypeIndex);
		assert(pass);

		res = vkAllocateMemory(vulkan_->GetDevice(), &mem_alloc, NULL, &mem);
		assert(res == VK_SUCCESS);

		res = vkBindImageMemory(vulkan_->GetDevice(), image, mem, 0);
		assert(res == VK_SUCCESS);

		// Since we're going to blit from the mappable image, set its layout to SOURCE_OPTIMAL
		TransitionImageLayout(cmd, mappableImage,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_PREINITIALIZED,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

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

		// Set the layout for the texture image from DESTINATION_OPTIMAL to SHADER_READ_ONLY
		TransitionImageLayout(cmd, image,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		// Then drop the temporary mappable image - although should not be necessary...
		vulkan_->Delete().QueueDeleteImage(mappableImage);
		vulkan_->Delete().QueueDeleteDeviceMemory(mappableMemory);

		mappableImage = VK_NULL_HANDLE;
		mappableMemory = VK_NULL_HANDLE;
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
	view_info.image = image;
	VkResult res = vkCreateImageView(vulkan_->GetDevice(), &view_info, NULL, &view);
	assert(res == VK_SUCCESS);
}

void VulkanTexture::Wipe() {
	if (image) {
		vulkan_->Delete().QueueDeleteImage(image);
		image = VK_NULL_HANDLE;
	}
	if (view) {
		vulkan_->Delete().QueueDeleteImageView(view);
		view = VK_NULL_HANDLE;
	}
	if (mem) {
		vulkan_->Delete().QueueDeleteDeviceMemory(mem);
		mem = VK_NULL_HANDLE;
	}
}

void VulkanTexture::CreateDirect(int w, int h, int numMips, VkFormat format, VkImageUsageFlags usage) {
	Wipe();

	VkCommandBuffer cmd = vulkan_->GetInitCommandBuffer();

	tex_width = w;
	tex_height = h;
	numMips_ = numMips;
	format_ = format;

	VkImageCreateInfo image_create_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	image_create_info.imageType = VK_IMAGE_TYPE_2D;
	image_create_info.format = format_;
	image_create_info.extent.width = tex_width;
	image_create_info.extent.height = tex_height;
	image_create_info.extent.depth = 1;
	image_create_info.mipLevels = numMips;
	image_create_info.arrayLayers = 1;
	image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_create_info.flags = 0;
	image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_create_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkResult res = vkCreateImage(vulkan_->GetDevice(), &image_create_info, NULL, &image);
	assert(res == VK_SUCCESS);

	vkGetImageMemoryRequirements(vulkan_->GetDevice(), image, &mem_reqs);
	VkMemoryAllocateInfo mem_alloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	mem_alloc.memoryTypeIndex = 0;
	mem_alloc.allocationSize = mem_reqs.size;

	// Find memory type - don't specify any mapping requirements
	bool pass = vulkan_->MemoryTypeFromProperties(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &mem_alloc.memoryTypeIndex);
	assert(pass);

	res = vkAllocateMemory(vulkan_->GetDevice(), &mem_alloc, NULL, &mem);
	assert(res == VK_SUCCESS);

	res = vkBindImageMemory(vulkan_->GetDevice(), image, mem, 0);
	assert(res == VK_SUCCESS);

	// Since we're going to blit to the target, set its layout to TRANSFER_DST
	TransitionImageLayout(cmd, image,
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	// Create the view while we're at it.
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
	view_info.subresourceRange.levelCount = numMips;
	view_info.subresourceRange.baseArrayLayer = 0;
	view_info.subresourceRange.layerCount = 1;
	view_info.image = image;
	res = vkCreateImageView(vulkan_->GetDevice(), &view_info, NULL, &view);
	assert(res == VK_SUCCESS);
}

void VulkanTexture::UploadMip(int mip, int mipWidth, int mipHeight, VkBuffer buffer, size_t offset, size_t rowLength) {
	VkBufferImageCopy copy_region = {};
	copy_region.bufferOffset = offset;
	copy_region.bufferRowLength = (uint32_t)rowLength;
	copy_region.bufferImageHeight = 0;  // 2D
	copy_region.imageExtent.width = mipWidth;
	copy_region.imageExtent.height = mipHeight;
	copy_region.imageExtent.depth = 1;
	copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy_region.imageSubresource.mipLevel = mip;
	copy_region.imageSubresource.baseArrayLayer = 0;
	copy_region.imageSubresource.layerCount = 1;

	VkCommandBuffer cmd = vulkan_->GetInitCommandBuffer();
	vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);
}

void VulkanTexture::EndCreate() {
	VkCommandBuffer cmd = vulkan_->GetInitCommandBuffer();
	TransitionImageLayout(cmd, image,
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void VulkanTexture::Destroy() {
	if (view) {
		vulkan_->Delete().QueueDeleteImageView(view);
	}
	if (image) {
		vulkan_->Delete().QueueDeleteImage(image);
		if (mappableImage == image) {
			mappableImage = VK_NULL_HANDLE;
		}
	}
	if (mem) {
		vulkan_->Delete().QueueDeleteDeviceMemory(mem);
		if (mappableMemory == mem) {
			mappableMemory = VK_NULL_HANDLE;
		}
	}

	view = VK_NULL_HANDLE;
	image = VK_NULL_HANDLE;
	mem = VK_NULL_HANDLE;
}
