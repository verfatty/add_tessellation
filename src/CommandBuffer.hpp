#pragma once

#include <vulkan/vulkan.hpp>
#include <utility>
#include <optional>
#include "Allocator.hpp"

#define VUK_MAX_SETS 8

namespace vuk {
	class Context;
	class PerThreadContext;
	class Buffer;

	enum class RelativeTo {
		eFramebuffer, eSwapchain
	};

	struct Area {
		Area(int32_t x, int32_t y, uint32_t width, uint32_t height) : offset{ x, y }, extent { width, height } {}

		struct Framebuffer {
			float x = 0.f;
			float y = 0.f;
			float width = 1.0f;
			float height = 1.0f;
		};

		vk::Offset2D offset;
		vk::Extent2D extent;
	};

	struct CommandBuffer {
		vk::CommandBuffer command_buffer;
		vuk::PerThreadContext& ptc;

		CommandBuffer(vuk::PerThreadContext& ptc, vk::CommandBuffer cb) : ptc(ptc), command_buffer(cb) {}

		std::optional<std::pair<RenderGraph::RenderPassInfo&, uint32_t>> ongoing_renderpass;
		std::optional<vk::Viewport> next_viewport;
		std::optional<vk::Rect2D> next_scissor;
		std::optional<vuk::PipelineInfo> current_pipeline;

		// global memory barrier
		bool global_memory_barrier_inserted_since_last_draw = false;
		unsigned src_access_mask = 0;
		unsigned dst_access_mask = 0;
		// buffer barriers
		struct QueueXFer {
			QueueID from;
			QueueID to;
		};
		std::vector<QueueXFer> queue_transfers;

		CommandBuffer& set_viewport(unsigned index, vk::Viewport vp);	
		CommandBuffer& set_viewport(unsigned index, Area area);
		CommandBuffer& set_viewport(unsigned index, Area::Framebuffer area);
		CommandBuffer& set_scissor(unsigned index, vk::Rect2D vp);
		CommandBuffer& set_scissor(unsigned index, Area area);
		CommandBuffer& set_scissor(unsigned index, Area::Framebuffer area);

		CommandBuffer& bind_pipeline(vuk::PipelineCreateInfo gpci);
		CommandBuffer& bind_pipeline(Name p);

		CommandBuffer& bind_vertex_buffer(Allocator::Buffer&);
		CommandBuffer& bind_index_buffer(Allocator::Buffer&, vk::IndexType type);

		CommandBuffer& bind_sampled_image(unsigned set, unsigned binding, vuk::ImageView iv, vk::SamplerCreateInfo sampler_create_info);
		
		CommandBuffer& push_constants(vk::ShaderStageFlags stages, size_t offset, void * data, size_t size);
		template<class T>
		CommandBuffer& push_constants(vk::ShaderStageFlags stages, size_t offset, gsl::span<T> span) {
			return push_constants(stages, offset, (void*)span.data(), sizeof(T) * span.size());
		}

		template<class T>
		CommandBuffer& push_constants(vk::ShaderStageFlags stages, size_t offset, T value) {
			return push_constants(stages, offset, (void*)&value, sizeof(T));
		}


		std::bitset<VUK_MAX_SETS> sets_used = {};
		std::array<SetBinding, VUK_MAX_SETS> set_bindings = {};

		CommandBuffer& bind_uniform_buffer(unsigned set, unsigned binding, Allocator::Buffer buffer);

		CommandBuffer& draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance);
		CommandBuffer& draw_indexed(uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance);
		void _bind_graphics_pipeline_state();
	};
}

