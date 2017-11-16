#include "DataFormat.h"
#include "VulkanQueueRunner.h"
#include "VulkanRenderManager.h"

// Debug help: adb logcat -s DEBUG PPSSPPNativeActivity PPSSPP

void VulkanQueueRunner::CreateDeviceObjects() {
	ILOG("VulkanQueueRunner::CreateDeviceObjects");
	InitBackbufferRenderPass();
	InitRenderpasses();
}

void VulkanQueueRunner::ResizeReadbackBuffer(VkDeviceSize requiredSize) {
	if (readbackBuffer_ && requiredSize <= readbackBufferSize_) {
		return;
	}
	if (readbackMemory_) {
		vulkan_->Delete().QueueDeleteDeviceMemory(readbackMemory_);
	}
	if (readbackBuffer_) {
		vulkan_->Delete().QueueDeleteBuffer(readbackBuffer_);
	}

	readbackBufferSize_ = requiredSize;

	VkDevice device = vulkan_->GetDevice();

	VkBufferCreateInfo buf{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	buf.size = readbackBufferSize_;
	buf.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	vkCreateBuffer(device, &buf, nullptr, &readbackBuffer_);

	VkMemoryRequirements reqs{};
	vkGetBufferMemoryRequirements(device, readbackBuffer_, &reqs);

	VkMemoryAllocateInfo alloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	alloc.allocationSize = reqs.size;

	VkFlags typeReqs = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	bool success = vulkan_->MemoryTypeFromProperties(reqs.memoryTypeBits, typeReqs, &alloc.memoryTypeIndex);
	assert(success);
	vkAllocateMemory(device, &alloc, nullptr, &readbackMemory_);

	uint32_t offset = 0;
	vkBindBufferMemory(device, readbackBuffer_, readbackMemory_, offset);
}

void VulkanQueueRunner::DestroyDeviceObjects() {
	ILOG("VulkanQueueRunner::DestroyDeviceObjects");
	VkDevice device = vulkan_->GetDevice();
	vulkan_->Delete().QueueDeleteDeviceMemory(readbackMemory_);
	vulkan_->Delete().QueueDeleteBuffer(readbackBuffer_);
	readbackBufferSize_ = 0;

	for (int i = 0; i < ARRAY_SIZE(renderPasses_); i++) {
		assert(renderPasses_[i] != VK_NULL_HANDLE);
		vulkan_->Delete().QueueDeleteRenderPass(renderPasses_[i]);
		renderPasses_[i] = VK_NULL_HANDLE;
	}
	assert(backbufferRenderPass_ != VK_NULL_HANDLE);
	vulkan_->Delete().QueueDeleteRenderPass(backbufferRenderPass_);
	backbufferRenderPass_ = VK_NULL_HANDLE;
}

void VulkanQueueRunner::InitBackbufferRenderPass() {
	VkResult U_ASSERT_ONLY res;

	VkAttachmentDescription attachments[2];
	attachments[0].format = vulkan_->GetSwapchainFormat();
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;  // We don't want to preserve the backbuffer between frames so we really don't care.
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;  // We only render once to the backbuffer per frame so we can do this here.
	attachments[0].flags = 0;

	attachments[1].format = vulkan_->GetDeviceInfo().preferredDepthStencilFormat;  // must use this same format later for the back depth buffer.
	attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;  // Don't care about storing backbuffer Z - we clear it anyway.
	attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachments[1].flags = 0;

	VkAttachmentReference color_reference{};
	color_reference.attachment = 0;
	color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depth_reference{};
	depth_reference.attachment = 1;
	depth_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.flags = 0;
	subpass.inputAttachmentCount = 0;
	subpass.pInputAttachments = nullptr;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_reference;
	subpass.pResolveAttachments = nullptr;
	subpass.pDepthStencilAttachment = &depth_reference;
	subpass.preserveAttachmentCount = 0;
	subpass.pPreserveAttachments = nullptr;

	// For the built-in layout transitions.
	VkSubpassDependency dep{};
	dep.srcSubpass = VK_SUBPASS_EXTERNAL;
	dep.dstSubpass = 0;
	dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dep.srcAccessMask = 0;
	dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	VkRenderPassCreateInfo rp_info{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
	rp_info.attachmentCount = 2;
	rp_info.pAttachments = attachments;
	rp_info.subpassCount = 1;
	rp_info.pSubpasses = &subpass;
	rp_info.dependencyCount = 1;
	rp_info.pDependencies = &dep;

	res = vkCreateRenderPass(vulkan_->GetDevice(), &rp_info, nullptr, &backbufferRenderPass_);
	assert(res == VK_SUCCESS);
}

void VulkanQueueRunner::InitRenderpasses() {
	// Create a bunch of render pass objects, for normal rendering with a depth buffer,
	// with clearing, without clearing, and dont-care for both depth/stencil and color, so 3*3=9 combos.
	VkAttachmentDescription attachments[2] = {};
	attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;  // TODO: Look into auto-transitioning to SAMPLED when appropriate.
	attachments[0].flags = 0;

	attachments[1].format = vulkan_->GetDeviceInfo().preferredDepthStencilFormat;
	attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachments[1].flags = 0;

	VkAttachmentReference color_reference{};
	color_reference.attachment = 0;
	color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depth_reference{};
	depth_reference.attachment = 1;
	depth_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.flags = 0;
	subpass.inputAttachmentCount = 0;
	subpass.pInputAttachments = nullptr;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_reference;
	subpass.pResolveAttachments = nullptr;
	subpass.pDepthStencilAttachment = &depth_reference;
	subpass.preserveAttachmentCount = 0;
	subpass.pPreserveAttachments = nullptr;

	VkRenderPassCreateInfo rp{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
	rp.attachmentCount = 2;
	rp.pAttachments = attachments;
	rp.subpassCount = 1;
	rp.pSubpasses = &subpass;
	rp.dependencyCount = 0;

	for (int depth = 0; depth < 3; depth++) {
		switch ((VKRRenderPassAction)depth) {
		case VKRRenderPassAction::CLEAR:
			attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			break;
		case VKRRenderPassAction::KEEP:
			attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			break;
		case VKRRenderPassAction::DONT_CARE:
			attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			break;
		}
		for (int color = 0; color < 3; color++) {
			switch ((VKRRenderPassAction)color) {
			case VKRRenderPassAction::CLEAR: attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; break;
			case VKRRenderPassAction::KEEP: attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; break;
			case VKRRenderPassAction::DONT_CARE: attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; break;
			}
			int index = RPIndex((VKRRenderPassAction)color, (VKRRenderPassAction)depth);
			vkCreateRenderPass(vulkan_->GetDevice(), &rp, nullptr, &renderPasses_[index]);
		}
	}
}

void VulkanQueueRunner::RunSteps(VkCommandBuffer cmd, const std::vector<VKRStep *> &steps) {
	// Optimizes renderpasses, then sequences them.
	for (int i = 0; i < steps.size(); i++) {
		const VKRStep &step = *steps[i];
		switch (step.stepType) {
		case VKRStepType::RENDER:
			PerformRenderPass(step, cmd);
			break;
		case VKRStepType::COPY:
			PerformCopy(step, cmd);
			break;
		case VKRStepType::BLIT:
			PerformBlit(step, cmd);
			break;
		case VKRStepType::READBACK:
			PerformReadback(step, cmd);
			break;
		case VKRStepType::READBACK_IMAGE:
			PerformReadbackImage(step, cmd);
			break;
		}
		delete steps[i];
	}
}

void VulkanQueueRunner::LogSteps(const std::vector<VKRStep *> &steps) {
	ILOG("=======================================");
	for (int i = 0; i < steps.size(); i++) {
		const VKRStep &step = *steps[i];
		switch (step.stepType) {
		case VKRStepType::RENDER:
			LogRenderPass(step);
			break;
		case VKRStepType::COPY:
			LogCopy(step);
			break;
		case VKRStepType::BLIT:
			LogBlit(step);
			break;
		case VKRStepType::READBACK:
			LogReadback(step);
			break;
		case VKRStepType::READBACK_IMAGE:
			LogReadbackImage(step);
			break;
		}
	}
}

void VulkanQueueRunner::LogRenderPass(const VKRStep &pass) {
	int fb = (int)(intptr_t)(pass.render.framebuffer ? pass.render.framebuffer->framebuf : 0);
	ILOG("RenderPass Begin(%x)", fb);
	for (auto &cmd : pass.commands) {
		switch (cmd.cmd) {
		case VKRRenderCommand::BIND_PIPELINE:
			ILOG("  BindPipeline(%x)", (int)(intptr_t)cmd.pipeline.pipeline);
			break;
		case VKRRenderCommand::BLEND:
			ILOG("  Blend(%f, %f, %f, %f)", cmd.blendColor.color[0], cmd.blendColor.color[1], cmd.blendColor.color[2], cmd.blendColor.color[3]);
			break;
		case VKRRenderCommand::CLEAR:
			ILOG("  Clear");
			break;
		case VKRRenderCommand::DRAW:
			ILOG("  Draw(%d)", cmd.draw.count);
			break;
		case VKRRenderCommand::DRAW_INDEXED:
			ILOG("  DrawIndexed(%d)", cmd.drawIndexed.count);
			break;
		case VKRRenderCommand::SCISSOR:
			ILOG("  Scissor(%d, %d, %d, %d)", (int)cmd.scissor.scissor.offset.x, (int)cmd.scissor.scissor.offset.y, (int)cmd.scissor.scissor.extent.width, (int)cmd.scissor.scissor.extent.height);
			break;
		case VKRRenderCommand::STENCIL:
			ILOG("  Stencil(ref=%d, compare=%d, write=%d)", cmd.stencil.stencilRef, cmd.stencil.stencilCompareMask, cmd.stencil.stencilWriteMask);
			break;
		case VKRRenderCommand::VIEWPORT:
			ILOG("  Viewport(%f, %f, %f, %f, %f, %f)", cmd.viewport.vp.x, cmd.viewport.vp.y, cmd.viewport.vp.width, cmd.viewport.vp.height, cmd.viewport.vp.minDepth, cmd.viewport.vp.maxDepth);
			break;
		case VKRRenderCommand::PUSH_CONSTANTS:
			ILOG("  PushConstants(%d)", cmd.push.size);
			break;
		}
	}
	ILOG("RenderPass End(%x)", fb);
}

void VulkanQueueRunner::LogCopy(const VKRStep &pass) {
	ILOG("Copy()");
}

void VulkanQueueRunner::LogBlit(const VKRStep &pass) {
	ILOG("Blit()");
}

void VulkanQueueRunner::LogReadback(const VKRStep &pass) {
	ILOG("Readback");
}

void VulkanQueueRunner::LogReadbackImage(const VKRStep &pass) {
	ILOG("ReadbackImage");
}

void VulkanQueueRunner::PerformRenderPass(const VKRStep &step, VkCommandBuffer cmd) {
	// TODO: If there are multiple, we can transition them together.
	for (const auto &iter : step.preTransitions) {
		if (iter.fb->color.layout != iter.targetLayout) {
			VkImageMemoryBarrier barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.oldLayout = iter.fb->color.layout;
			barrier.subresourceRange.layerCount = 1;
			barrier.subresourceRange.levelCount = 1;
			barrier.image = iter.fb->color.image;
			barrier.srcAccessMask = 0;
			VkPipelineStageFlags srcStage;
			VkPipelineStageFlags dstStage;
			switch (barrier.oldLayout) {
			case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
				barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
				srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				break;
			case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
				barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
				break;
			case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
				barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
				break;
			default:
				Crash();
				break;
			}
			barrier.newLayout = iter.targetLayout;
			switch (barrier.newLayout) {
			case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
				barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				break;
			default:
				Crash();
				break;
			}
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

			vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
			iter.fb->color.layout = barrier.newLayout;
		}
	}

	// Don't execute empty renderpasses.
	if (step.commands.empty() && step.render.color == VKRRenderPassAction::KEEP && step.render.depthStencil == VKRRenderPassAction::KEEP) {
		// Nothing to do.
		return;
	}

	// This is supposed to bind a vulkan render pass to the command buffer.
	PerformBindFramebufferAsRenderTarget(step, cmd);

	int curWidth = step.render.framebuffer ? step.render.framebuffer->width : vulkan_->GetBackbufferWidth();
	int curHeight = step.render.framebuffer ? step.render.framebuffer->height : vulkan_->GetBackbufferHeight();

	VKRFramebuffer *fb = step.render.framebuffer;

	VkPipeline lastPipeline = VK_NULL_HANDLE;
	VkDescriptorSet lastDescSet = VK_NULL_HANDLE;

	auto &commands = step.commands;

	// TODO: Dynamic state commands (SetViewport, SetScissor, SetBlendConstants, SetStencil*) are only
	// valid when a pipeline is bound with those as dynamic state. So we need to add some state tracking here
	// for this to be correct. This is a bit of a pain but also will let us eliminate redundant calls.

	for (const auto &c : commands) {
		switch (c.cmd) {
		case VKRRenderCommand::BIND_PIPELINE:
			if (c.pipeline.pipeline != lastPipeline) {
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, c.pipeline.pipeline);
				lastPipeline = c.pipeline.pipeline;
			}
			break;

		case VKRRenderCommand::VIEWPORT:
			vkCmdSetViewport(cmd, 0, 1, &c.viewport.vp);
			break;

		case VKRRenderCommand::SCISSOR:
			vkCmdSetScissor(cmd, 0, 1, &c.scissor.scissor);
			break;

		case VKRRenderCommand::BLEND:
			vkCmdSetBlendConstants(cmd, c.blendColor.color);
			break;

		case VKRRenderCommand::PUSH_CONSTANTS:
			vkCmdPushConstants(cmd, c.push.pipelineLayout, c.push.stages, c.push.offset, c.push.size, c.push.data);
			break;

		case VKRRenderCommand::STENCIL:
			vkCmdSetStencilWriteMask(cmd, VK_STENCIL_FRONT_AND_BACK, c.stencil.stencilWriteMask);
			vkCmdSetStencilCompareMask(cmd, VK_STENCIL_FRONT_AND_BACK, c.stencil.stencilCompareMask);
			vkCmdSetStencilReference(cmd, VK_STENCIL_FRONT_AND_BACK, c.stencil.stencilRef);
			break;

		case VKRRenderCommand::DRAW_INDEXED:
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, c.drawIndexed.pipelineLayout, 0, 1, &c.drawIndexed.ds, c.drawIndexed.numUboOffsets, c.drawIndexed.uboOffsets);
			vkCmdBindIndexBuffer(cmd, c.drawIndexed.ibuffer, c.drawIndexed.ioffset, c.drawIndexed.indexType);
			vkCmdBindVertexBuffers(cmd, 0, 1, &c.drawIndexed.vbuffer, &c.drawIndexed.voffset);
			vkCmdDrawIndexed(cmd, c.drawIndexed.count, c.drawIndexed.instances, 0, 0, 0);
			break;

		case VKRRenderCommand::DRAW:
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, c.draw.pipelineLayout, 0, 1, &c.draw.ds, c.draw.numUboOffsets, c.draw.uboOffsets);
			if (c.draw.vbuffer) {
				vkCmdBindVertexBuffers(cmd, 0, 1, &c.draw.vbuffer, &c.draw.voffset);
			}
			vkCmdDraw(cmd, c.draw.count, 1, 0, 0);
			break;

		case VKRRenderCommand::CLEAR:
		{
			int numAttachments = 0;
			VkClearRect rc{};
			rc.baseArrayLayer = 0;
			rc.layerCount = 1;
			rc.rect.extent.width = (uint32_t)curWidth;
			rc.rect.extent.height = (uint32_t)curHeight;
			VkClearAttachment attachments[2];
			if (c.clear.clearMask & VK_IMAGE_ASPECT_COLOR_BIT) {
				VkClearAttachment &attachment = attachments[numAttachments++];
				attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				attachment.colorAttachment = 0;
				Uint8x4ToFloat4(attachment.clearValue.color.float32, c.clear.clearColor);
			}
			if (c.clear.clearMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
				VkClearAttachment &attachment = attachments[numAttachments++];
				attachment.aspectMask = 0;
				if (c.clear.clearMask & VK_IMAGE_ASPECT_DEPTH_BIT) {
					attachment.clearValue.depthStencil.depth = c.clear.clearZ;
					attachment.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
				}
				if (c.clear.clearMask & VK_IMAGE_ASPECT_STENCIL_BIT) {
					attachment.clearValue.depthStencil.stencil = (uint32_t)c.clear.clearStencil;
					attachment.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
				}
			}
			if (numAttachments) {
				vkCmdClearAttachments(cmd, numAttachments, attachments, 1, &rc);
			}
			break;
		}
		default:
			ELOG("Unimpl queue command");
			;
		}
	}
	vkCmdEndRenderPass(cmd);

	// Transition the framebuffer if requested.
	if (fb && step.render.finalColorLayout != VK_IMAGE_LAYOUT_UNDEFINED) {
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = fb->color.layout;
		barrier.subresourceRange.layerCount = 1;
		barrier.subresourceRange.levelCount = 1;
		barrier.image = fb->color.image;
		barrier.srcAccessMask = 0;
		switch (barrier.oldLayout) {
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			break;
		default:
			Crash();
		}
		barrier.newLayout = step.render.finalColorLayout;
		switch (barrier.newLayout) {
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			break;
		default:
			Crash();
		}
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

		// we're between passes so it's OK.
		// ARM Best Practices guide recommends these stage bits.
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
		fb->color.layout = barrier.newLayout;
	}
}

void VulkanQueueRunner::PerformBindFramebufferAsRenderTarget(const VKRStep &step, VkCommandBuffer cmd) {
	VkRenderPass renderPass;
	int numClearVals = 0;
	VkClearValue clearVal[2]{};
	VkFramebuffer framebuf;
	int w;
	int h;
	if (step.render.framebuffer) {
		VKRFramebuffer *fb = step.render.framebuffer;
		framebuf = fb->framebuf;
		w = fb->width;
		h = fb->height;
		// Now, if the image needs transitioning, let's transition.
		// The backbuffer does not, that's handled by VulkanContext.
		if (step.render.framebuffer->color.layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
			VkAccessFlags srcAccessMask;
			VkPipelineStageFlags srcStage;
			switch (fb->color.layout) {
			case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
				srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
				srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				break;
			case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
				srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
				break;
			case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
				srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
				break;
			default:
				Crash();
				break;
			}

			// Due to a known bug in the Mali driver, pass the level count explicitly.
			TransitionImageLayout2(cmd, fb->color.image, VK_IMAGE_ASPECT_COLOR_BIT,
				fb->color.layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				srcStage, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				srcAccessMask, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, 1);
			fb->color.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}
		if (fb->depth.layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
			VkAccessFlags srcAccessMask;
			VkPipelineStageFlags srcStage;

			switch (fb->depth.layout) {
			case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
				srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
				srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				break;
			case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
				srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
				break;
			case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
				srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
				break;
			default:
				Crash();
				break;
			}

			// Due to a known bug in the Mali driver, pass the level count explicitly (last param).
			TransitionImageLayout2(cmd, fb->depth.image, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
				fb->depth.layout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
				srcStage, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				srcAccessMask, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
				1);
			fb->depth.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		}

		renderPass = renderPasses_[RPIndex(step.render.color, step.render.depthStencil)];
		if (step.render.color == VKRRenderPassAction::CLEAR) {
			Uint8x4ToFloat4(clearVal[0].color.float32, step.render.clearColor);
			numClearVals = 1;
		}
		if (step.render.depthStencil == VKRRenderPassAction::CLEAR) {
			clearVal[1].depthStencil.depth = step.render.clearDepth;
			clearVal[1].depthStencil.stencil = step.render.clearStencil;
			numClearVals = 2;
		}
	} else {
		framebuf = backbuffer_;
		w = vulkan_->GetBackbufferWidth();
		h = vulkan_->GetBackbufferHeight();
		renderPass = GetBackbufferRenderPass();
		assert(step.render.color == VKRRenderPassAction::CLEAR || step.render.color == VKRRenderPassAction::DONT_CARE);
		assert(step.render.depthStencil == VKRRenderPassAction::CLEAR || step.render.depthStencil == VKRRenderPassAction::DONT_CARE);
		Uint8x4ToFloat4(clearVal[0].color.float32, step.render.clearColor);
		numClearVals = 2;  // We don't bother with a depth buffer here.
		clearVal[1].depthStencil.depth = 0.0f;
		clearVal[1].depthStencil.stencil = 0;
	}

	VkRenderPassBeginInfo rp_begin = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	rp_begin.renderPass = renderPass;
	rp_begin.framebuffer = framebuf;
	rp_begin.renderArea.offset.x = 0;
	rp_begin.renderArea.offset.y = 0;
	rp_begin.renderArea.extent.width = w;
	rp_begin.renderArea.extent.height = h;
	rp_begin.clearValueCount = numClearVals;
	rp_begin.pClearValues = numClearVals ? clearVal : nullptr;
	vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
}

void VulkanQueueRunner::PerformCopy(const VKRStep &step, VkCommandBuffer cmd) {
	VKRFramebuffer *src = step.copy.src;
	VKRFramebuffer *dst = step.copy.dst;

	VkImageCopy copy{};
	copy.srcOffset.x = step.copy.srcRect.offset.x;
	copy.srcOffset.y = step.copy.srcRect.offset.y;
	copy.srcOffset.z = 0;
	copy.srcSubresource.mipLevel = 0;
	copy.srcSubresource.layerCount = 1;
	copy.dstOffset.x = step.copy.dstPos.x;
	copy.dstOffset.y = step.copy.dstPos.y;
	copy.dstOffset.z = 0;
	copy.dstSubresource.mipLevel = 0;
	copy.dstSubresource.layerCount = 1;
	copy.extent.width = step.copy.srcRect.extent.width;
	copy.extent.height = step.copy.srcRect.extent.height;
	copy.extent.depth = 1;

	VkImageMemoryBarrier srcBarriers[2]{};
	VkImageMemoryBarrier dstBarriers[2]{};
	int srcCount = 0;
	int dstCount = 0;

	VkPipelineStageFlags srcStage = 0;
	VkPipelineStageFlags dstStage = 0;
	// First source barriers.
	if (step.copy.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
		if (src->color.layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
			SetupTransitionToTransferSrc(src->color, srcBarriers[srcCount++], srcStage, VK_IMAGE_ASPECT_COLOR_BIT);
		}
		if (dst->color.layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			SetupTransitionToTransferDst(dst->color, dstBarriers[dstCount++], dstStage, VK_IMAGE_ASPECT_COLOR_BIT);
		}
	}

	// We can't copy only depth or only stencil unfortunately.
	if (step.copy.aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
		if (src->depth.layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
			SetupTransitionToTransferSrc(src->depth, srcBarriers[srcCount++], srcStage, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
		}
		if (dst->depth.layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			SetupTransitionToTransferDst(dst->depth, dstBarriers[dstCount++], dstStage, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
		}
	}

	if (srcCount) {
		vkCmdPipelineBarrier(cmd, srcStage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, srcCount, srcBarriers);
	}
	if (dstCount) {
		vkCmdPipelineBarrier(cmd, dstStage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, dstCount, dstBarriers);
	}

	if (step.copy.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
		copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		vkCmdCopyImage(cmd, src->color.image, src->color.layout, dst->color.image, dst->color.layout, 1, &copy);
	}
	if (step.copy.aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
		copy.srcSubresource.aspectMask = 0;
		copy.dstSubresource.aspectMask = 0;
		if (step.copy.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) {
			copy.srcSubresource.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
			copy.dstSubresource.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
		}
		if (step.copy.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) {
			copy.srcSubresource.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			copy.dstSubresource.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		vkCmdCopyImage(cmd, src->depth.image, src->depth.layout, dst->depth.image, dst->depth.layout, 1, &copy);
	}
}

void VulkanQueueRunner::PerformBlit(const VKRStep &step, VkCommandBuffer cmd) {
	VkImageMemoryBarrier srcBarriers[2]{};
	VkImageMemoryBarrier dstBarriers[2]{};

	VKRFramebuffer *src = step.blit.src;
	VKRFramebuffer *dst = step.blit.dst;

	// If any validation needs to be performed here, it should probably have been done
	// already when the blit was queued. So don't validate here.
	VkImageBlit blit{};
	blit.srcOffsets[0].x = step.blit.srcRect.offset.x;
	blit.srcOffsets[0].y = step.blit.srcRect.offset.y;
	blit.srcOffsets[0].z = 0;
	blit.srcOffsets[1].x = step.blit.srcRect.offset.x + step.blit.srcRect.extent.width;
	blit.srcOffsets[1].y = step.blit.srcRect.offset.y + step.blit.srcRect.extent.height;
	blit.srcOffsets[1].z = 1;
	blit.srcSubresource.mipLevel = 0;
	blit.srcSubresource.layerCount = 1;
	blit.dstOffsets[0].x = step.blit.dstRect.offset.x;
	blit.dstOffsets[0].y = step.blit.dstRect.offset.y;
	blit.dstOffsets[0].z = 0;
	blit.dstOffsets[1].x = step.blit.dstRect.offset.x + step.blit.dstRect.extent.width;
	blit.dstOffsets[1].y = step.blit.dstRect.offset.y + step.blit.dstRect.extent.height;
	blit.dstOffsets[1].z = 1;
	blit.dstSubresource.mipLevel = 0;
	blit.dstSubresource.layerCount = 1;

	VkPipelineStageFlags srcStage = 0;
	VkPipelineStageFlags dstStage = 0;

	int srcCount = 0;
	int dstCount = 0;

	// First source barriers.
	if (step.blit.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
		if (src->color.layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
			SetupTransitionToTransferSrc(src->color, srcBarriers[srcCount++], srcStage, VK_IMAGE_ASPECT_COLOR_BIT);
		}
		if (dst->color.layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			SetupTransitionToTransferDst(dst->color, dstBarriers[dstCount++], dstStage, VK_IMAGE_ASPECT_COLOR_BIT);
		}
	}

	// We can't copy only depth or only stencil unfortunately.
	if (step.blit.aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
		if (src->depth.layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
			SetupTransitionToTransferSrc(src->depth, srcBarriers[srcCount++], srcStage, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
		}
		if (dst->depth.layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			SetupTransitionToTransferDst(dst->depth, dstBarriers[dstCount++], dstStage, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
		}
	}

	if (srcCount) {
		vkCmdPipelineBarrier(cmd, srcStage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, srcCount, srcBarriers);
	}
	if (dstCount) {
		vkCmdPipelineBarrier(cmd, dstStage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, dstCount, dstBarriers);
	}

	if (step.blit.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		vkCmdBlitImage(cmd, src->color.image, src->color.layout, dst->color.image, dst->color.layout, 1, &blit, step.blit.filter);
	}

	// TODO: Need to check if the depth format is blittable.
	// Actually, we should probably almost always use copies rather than blits for depth buffers.
	if (step.blit.aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
		blit.srcSubresource.aspectMask = 0;
		blit.dstSubresource.aspectMask = 0;
		if (step.blit.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) {
			blit.srcSubresource.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
			blit.dstSubresource.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
		}
		if (step.blit.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) {
			blit.srcSubresource.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			blit.dstSubresource.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		vkCmdBlitImage(cmd, src->depth.image, src->depth.layout, dst->depth.image, dst->depth.layout, 1, &blit, step.blit.filter);
	}
}

void VulkanQueueRunner::SetupTransitionToTransferSrc(VKRImage &img, VkImageMemoryBarrier &barrier, VkPipelineStageFlags &stage, VkImageAspectFlags aspect) {
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = img.layout;
	barrier.subresourceRange.layerCount = 1;
	barrier.subresourceRange.levelCount = 1;
	barrier.image = img.image;
	barrier.srcAccessMask = 0;
	switch (img.layout) {
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
		stage |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		break;
	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		stage |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		break;
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		stage |= VK_PIPELINE_STAGE_TRANSFER_BIT;
		break;
	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		stage |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		break;
	default:
		Crash();
	}
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barrier.subresourceRange.aspectMask = aspect;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	img.layout = barrier.newLayout;
}

void VulkanQueueRunner::SetupTransitionToTransferDst(VKRImage &img, VkImageMemoryBarrier &barrier, VkPipelineStageFlags &stage, VkImageAspectFlags aspect) {
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = img.layout;
	barrier.subresourceRange.layerCount = 1;
	barrier.subresourceRange.levelCount = 1;
	barrier.image = img.image;
	barrier.srcAccessMask = 0;
	switch (img.layout) {
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		stage |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		break;
	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		stage |= VK_PIPELINE_STAGE_TRANSFER_BIT;
		break;
	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		stage |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		break;
	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		stage |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		break;
	default:
		Crash();
	}
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.subresourceRange.aspectMask = aspect;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	img.layout = barrier.newLayout;
}

void VulkanQueueRunner::PerformReadback(const VKRStep &step, VkCommandBuffer cmd) {
	ResizeReadbackBuffer(sizeof(uint32_t) * step.readback.srcRect.extent.width * step.readback.srcRect.extent.height);

	VkBufferImageCopy region{};
	region.imageOffset = { step.readback.srcRect.offset.x, step.readback.srcRect.offset.y, 0 };
	region.imageExtent = { step.readback.srcRect.extent.width, step.readback.srcRect.extent.height, 1 };
	region.imageSubresource.aspectMask = step.readback.aspectMask;
	region.imageSubresource.layerCount = 1;
	region.bufferOffset = 0;
	region.bufferRowLength = step.readback.srcRect.extent.width;
	region.bufferImageHeight = step.readback.srcRect.extent.height;

	VkImage image;
	VkImageLayout copyLayout;
	// Special case for backbuffer readbacks.
	if (step.readback.src == nullptr) {
		// We only take screenshots after the main render pass (anything else would be stupid) so we need to transition out of PRESENT,
		// and then back into it.
		TransitionImageLayout2(cmd, backbufferImage_, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, VK_ACCESS_TRANSFER_READ_BIT);
		copyLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		image = backbufferImage_;
	} else {
		VKRImage *srcImage;
		if (step.readback.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
			srcImage = &step.readback.src->color;
		}
		else if (step.readback.aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
			srcImage = &step.readback.src->depth;
		}
		else {
			assert(false);
		}

		VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
		VkPipelineStageFlags stage = 0;
		if (srcImage->layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
			SetupTransitionToTransferSrc(*srcImage, barrier, stage, step.readback.aspectMask);
			vkCmdPipelineBarrier(cmd, stage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
		}
		image = srcImage->image;
		copyLayout = srcImage->layout;
	}

	vkCmdCopyImageToBuffer(cmd, image, copyLayout, readbackBuffer_, 1, &region);

	// NOTE: Can't read the buffer using the CPU here - need to sync first.

	// If we copied from the backbuffer, transition it back.
	if (step.readback.src == nullptr) {
		// We only take screenshots after the main render pass (anything else would be stupid) so we need to transition out of PRESENT,
		// and then back into it.
		TransitionImageLayout2(cmd, backbufferImage_, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			VK_ACCESS_TRANSFER_READ_BIT, 0);
		copyLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	}
}

void VulkanQueueRunner::PerformReadbackImage(const VKRStep &step, VkCommandBuffer cmd) {
	// TODO: Clean this up - just reusing `SetupTransitionToTransferSrc`.
	VKRImage srcImage;
	srcImage.image = step.readback_image.image;
	srcImage.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	VkPipelineStageFlags stage = 0;
	SetupTransitionToTransferSrc(srcImage, barrier, stage, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCmdPipelineBarrier(cmd, stage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

	ResizeReadbackBuffer(sizeof(uint32_t) * step.readback_image.srcRect.extent.width * step.readback_image.srcRect.extent.height);

	VkBufferImageCopy region{};
	region.imageOffset = { step.readback_image.srcRect.offset.x, step.readback_image.srcRect.offset.y, 0 };
	region.imageExtent = { step.readback_image.srcRect.extent.width, step.readback_image.srcRect.extent.height, 1 };
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1;
	region.imageSubresource.mipLevel = step.readback_image.mipLevel;
	region.bufferOffset = 0;
	region.bufferRowLength = step.readback_image.srcRect.extent.width;
	region.bufferImageHeight = step.readback_image.srcRect.extent.height;
	vkCmdCopyImageToBuffer(cmd, step.readback_image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readbackBuffer_, 1, &region);

	// Now transfer it back to a texture.
	TransitionImageLayout2(cmd, step.readback_image.image,
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT);

	// NOTE: Can't read the buffer using the CPU here - need to sync first.
}

void VulkanQueueRunner::CopyReadbackBuffer(int width, int height, Draw::DataFormat srcFormat, Draw::DataFormat destFormat, int pixelStride, uint8_t *pixels) {
	// Read back to the requested address in ram from buffer.
	void *mappedData;
	const size_t srcPixelSize = DataFormatSizeInBytes(srcFormat);

	VkResult res = vkMapMemory(vulkan_->GetDevice(), readbackMemory_, 0, width * height * srcPixelSize, 0, &mappedData);
	assert(res == VK_SUCCESS);
	if (srcFormat == Draw::DataFormat::R8G8B8A8_UNORM) {
		ConvertFromRGBA8888(pixels, (const uint8_t *)mappedData, pixelStride, width, width, height, destFormat);
	} else if (srcFormat == Draw::DataFormat::B8G8R8A8_UNORM) {
		ConvertFromBGRA8888(pixels, (const uint8_t *)mappedData, pixelStride, width, width, height, destFormat);
	} else if (srcFormat == destFormat) {
		uint8_t *dst = pixels;
		const uint8_t *src = (const uint8_t *)mappedData;
		for (int y = 0; y < height; ++y) {
			memcpy(dst, src, width * srcPixelSize);
			src += width * srcPixelSize;
			dst += pixelStride * srcPixelSize;
		}
	} else if (destFormat == Draw::DataFormat::D32F) {
		ConvertToD32F(pixels, (const uint8_t *)mappedData, pixelStride, width, width, height, srcFormat);
	} else {
		// TODO: Maybe a depth conversion or something?
		assert(false);
	}
	vkUnmapMemory(vulkan_->GetDevice(), readbackMemory_);
}
