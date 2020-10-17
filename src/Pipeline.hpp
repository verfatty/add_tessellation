#pragma once

#include <vector>
#include "vulkan/vulkan.hpp"
#include "Hash.hpp"
#include "CreateInfo.hpp"
#include "Descriptor.hpp"
#include "Program.hpp"
#include "FixedVector.hpp"

#define VUK_MAX_SETS 8
#define VUK_MAX_ATTRIBUTES 8
#define VUK_MAX_COLOR_ATTACHMENTS 8
#define VUK_MAX_PUSHCONSTANT_RANGES 8

namespace vuk {
	enum class BlendPreset {
		eOff, eAlphaBlend, ePremultipliedAlphaBlend
	};

	struct PipelineLayoutCreateInfo {
		vk::PipelineLayoutCreateInfo plci;
		vuk::fixed_vector<vk::PushConstantRange, VUK_MAX_PUSHCONSTANT_RANGES> pcrs;
		vuk::fixed_vector<vuk::DescriptorSetLayoutCreateInfo, VUK_MAX_SETS> dslcis;

		bool operator==(const PipelineLayoutCreateInfo& o) const {
			return std::tie(plci.flags, pcrs, dslcis) == std::tie(o.plci.flags, o.pcrs, o.dslcis);
		}
	};

	template<> struct create_info<vk::PipelineLayout> {
		using type = vuk::PipelineLayoutCreateInfo;
	};

	struct Program;

	struct PipelineBaseCreateInfoBase {
		// 4 valid flags
		std::bitset<4 * VUK_MAX_SETS * VUK_MAX_BINDINGS> binding_flags = {};
		// set flags on specific descriptor in specific set
		void set_binding_flags(unsigned set, unsigned binding, vk::DescriptorBindingFlags flags) {
			unsigned f = static_cast<unsigned>(flags);
			binding_flags.set(set * 4 * VUK_MAX_BINDINGS + binding * 4 + 0, f & 0b1);
			binding_flags.set(set * 4 * VUK_MAX_BINDINGS + binding * 4 + 1, f & 0b10);
			binding_flags.set(set * 4 * VUK_MAX_BINDINGS + binding * 4 + 2, f & 0b100);
			binding_flags.set(set * 4 * VUK_MAX_BINDINGS + binding * 4 + 3, f & 0b1000);
		}
		// if the set has a variable count binding, the maximum number of bindings possible
		std::array<uint32_t, VUK_MAX_SETS> variable_count_max = {};
		void set_variable_count_binding(unsigned set, unsigned binding, uint32_t max_descriptors) {
			// clear all variable count bits
			for (unsigned i = 0; i < VUK_MAX_BINDINGS; i++) {
				binding_flags.set(set * 4 * VUK_MAX_BINDINGS + i * 4 + 3, 0);
			}
			// set variable count (0x8)
			binding_flags.set(set * 4 * VUK_MAX_BINDINGS + binding * 4 + 3, 1);
			variable_count_max[set] = max_descriptors;
		}
	};

	/* filled out by the user */
	struct PipelineBaseCreateInfo : PipelineBaseCreateInfoBase {
		friend class CommandBuffer;
		friend class Context;
	public:
        void add_shader(std::string source, std::string filename) {
            shaders.emplace_back(std::move(source));
            shader_paths.emplace_back(std::move(filename));
		}

		vk::PipelineRasterizationStateCreateInfo rasterization_state;
		vk::PipelineColorBlendStateCreateInfo color_blend_state;
		vuk::fixed_vector<vk::PipelineColorBlendAttachmentState, VUK_MAX_COLOR_ATTACHMENTS> color_blend_attachments;
		vk::PipelineDepthStencilStateCreateInfo depth_stencil_state;

		vuk::fixed_vector<std::string, 5> shaders;
		vuk::fixed_vector<std::string, 5> shader_paths;

		void set_blend(size_t attachment_index, BlendPreset);
		void set_blend(BlendPreset);
		
		friend struct std::hash<PipelineBaseCreateInfo>;
        friend class PerThreadContext;
	public:
		PipelineBaseCreateInfo();
		
		static vuk::fixed_vector<vuk::DescriptorSetLayoutCreateInfo, VUK_MAX_SETS> build_descriptor_layouts(const Program&, const PipelineBaseCreateInfoBase&);
		bool operator==(const PipelineBaseCreateInfo& o) const {
            return shaders == o.shaders && rasterization_state == o.rasterization_state && color_blend_state == o.color_blend_state &&
                   color_blend_attachments == o.color_blend_attachments && depth_stencil_state == o.depth_stencil_state && binding_flags == o.binding_flags && variable_count_max == o.variable_count_max;
		}
	};

	struct PipelineBaseInfo {
        std::string pipeline_name;
        vuk::Program reflection_info;
        std::vector<vk::PipelineShaderStageCreateInfo> psscis;
		vk::PipelineLayout pipeline_layout;
		std::array<DescriptorSetLayoutAllocInfo, VUK_MAX_SETS> layout_info;
        vk::PipelineRasterizationStateCreateInfo rasterization_state;
        vk::PipelineColorBlendStateCreateInfo color_blend_state;
        vuk::fixed_vector<vk::PipelineColorBlendAttachmentState, VUK_MAX_COLOR_ATTACHMENTS> color_blend_attachments;
        vk::PipelineDepthStencilStateCreateInfo depth_stencil_state;

        vuk::fixed_vector<vk::DynamicState, 8> dynamic_states = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        vk::PipelineViewportStateCreateInfo viewport_state{{}, 1, nullptr, 1, nullptr};

		// 4 valid flags
		std::bitset<4 * VUK_MAX_SETS * VUK_MAX_BINDINGS> binding_flags = {};
		// if the set has a variable count binding, the maximum number of bindings possible
		std::array<uint32_t, VUK_MAX_SETS> variable_count_max = {};
	};

	template<> struct create_info<PipelineBaseInfo> {
		using type = vuk::PipelineBaseCreateInfo;
	};

	struct ComputePipelineCreateInfo : PipelineBaseCreateInfoBase {
		friend class CommandBuffer;
		friend class Context;
	public:
        void add_shader(std::string source, std::string filename) {
            shader = std::move(source);
            shader_path = std::move(filename);
		}

		friend struct std::hash<ComputePipelineCreateInfo>;
        friend class PerThreadContext;
	private:
		std::string shader;
		std::string shader_path;

	public:
		bool operator==(const ComputePipelineCreateInfo& o) const {
            return shader == o.shader && binding_flags == o.binding_flags && variable_count_max == o.variable_count_max;
		}
	};

	struct PipelineInstanceCreateInfo {
        PipelineBaseInfo* base;
		vuk::fixed_vector<vk::VertexInputBindingDescription, VUK_MAX_ATTRIBUTES> binding_descriptions;
		vuk::fixed_vector<vk::VertexInputAttributeDescription, VUK_MAX_ATTRIBUTES> attribute_descriptions;
		vuk::fixed_vector<vk::PipelineColorBlendAttachmentState, VUK_MAX_COLOR_ATTACHMENTS> color_blend_attachments;
        vk::PipelineInputAssemblyStateCreateInfo input_assembly_state;
        vk::PipelineColorBlendStateCreateInfo color_blend_state;
		vk::PipelineVertexInputStateCreateInfo vertex_input_state;
		vk::PipelineMultisampleStateCreateInfo multisample_state;
		vk::PipelineDynamicStateCreateInfo dynamic_state;
		vk::RenderPass render_pass;
        uint32_t subpass;

        vk::GraphicsPipelineCreateInfo to_vk() const;
        PipelineInstanceCreateInfo();

        bool operator==(const PipelineInstanceCreateInfo& o) const {
            return base == o.base && binding_descriptions == o.binding_descriptions && attribute_descriptions == o.attribute_descriptions &&
                   color_blend_attachments == o.color_blend_attachments && color_blend_state == o.color_blend_state &&
                   vertex_input_state == o.vertex_input_state && multisample_state == o.multisample_state && dynamic_state == o.dynamic_state &&
                   render_pass == o.render_pass && subpass == o.subpass;
        }
    };

	struct PipelineInfo {
		vk::Pipeline pipeline;
		vk::PipelineLayout pipeline_layout;
		std::array<DescriptorSetLayoutAllocInfo, VUK_MAX_SETS> layout_info;
	};

	template<> struct create_info<PipelineInfo> {
		using type = vuk::PipelineInstanceCreateInfo;
	};

	struct ComputePipelineInfo : PipelineInfo {
		std::array<unsigned, 3> local_size;
	};

	template<> struct create_info<ComputePipelineInfo> {
		using type = vuk::ComputePipelineCreateInfo;
	};
}

namespace std {
	template <class BitType>
	struct hash<vk::Flags<BitType>> {
		size_t operator()(vk::Flags<BitType> const& x) const noexcept {
			return std::hash<typename vk::Flags<BitType>::MaskType>()((typename vk::Flags<BitType>::MaskType)x);
		}
	};
};

namespace std {	
	template <class T>
	struct hash<std::vector<T>> {
		size_t operator()(std::vector<T> const& x) const noexcept {
			size_t h = 0;
			for (auto& e : x) {
				hash_combine(h, e);
			}
			return h;
		}
	};

	template <class T, size_t N>
	struct hash<vuk::fixed_vector<T, N>> {
		size_t operator()(vuk::fixed_vector<T, N> const& x) const noexcept {
			size_t h = 0;
			for (auto& e : x) {
				hash_combine(h, e);
			}
			return h;
		}
	};

	template <>
	struct hash<vk::PipelineInputAssemblyStateCreateInfo> {
		size_t operator()(vk::PipelineInputAssemblyStateCreateInfo const& x) const noexcept {
			size_t h = 0;
			hash_combine(h, x.flags, x.primitiveRestartEnable, to_integral(x.topology));
			return h;
		}
	};

	template <>
	struct hash<vk::StencilOpState> {
		size_t operator()(vk::StencilOpState const& x) const noexcept {
			size_t h = 0;
			hash_combine(h, x.compareMask, to_integral(x.compareOp), to_integral(x.failOp), to_integral(x.depthFailOp), to_integral(x.passOp), x.reference, x.writeMask);
			return h;
		}
	};

	template <>
	struct hash<vk::PipelineDepthStencilStateCreateInfo> {
		size_t operator()(vk::PipelineDepthStencilStateCreateInfo const& x) const noexcept {
			size_t h = 0;
			hash_combine(h, x.flags, x.back, x.front, x.depthBoundsTestEnable, to_integral(x.depthCompareOp), x.depthTestEnable, x.depthWriteEnable, x.maxDepthBounds, x.minDepthBounds, x.stencilTestEnable);
			return h;
		}
	};

	template<>
	struct hash<vk::PipelineRasterizationStateCreateInfo> {
		size_t operator()(vk::PipelineRasterizationStateCreateInfo const& x) const noexcept {
			size_t h = 0;
			hash_combine(h, x.depthClampEnable, x.rasterizerDiscardEnable, x.polygonMode,
				x.cullMode, x.frontFace, x.depthBiasEnable,
				x.depthBiasConstantFactor, x.depthBiasClamp, x.depthBiasSlopeFactor, x.lineWidth);
			return h;
		}
	};

	template <>
	struct hash<vk::PipelineColorBlendAttachmentState> {
		size_t operator()(vk::PipelineColorBlendAttachmentState const& x) const noexcept {
			size_t h = 0;
			hash_combine(h, to_integral(x.alphaBlendOp), x.blendEnable, to_integral(x.colorBlendOp), to_integral(x.dstAlphaBlendFactor), to_integral(x.srcAlphaBlendFactor), to_integral(x.dstColorBlendFactor), to_integral(x.srcColorBlendFactor));
			return h;
		}
	};

	template <>
	struct hash<vk::PipelineColorBlendStateCreateInfo> {
		size_t operator()(vk::PipelineColorBlendStateCreateInfo const& x) const noexcept {
			size_t h = 0;
			hash_combine(h, x.flags, std::span(x.pAttachments, x.attachmentCount), x.blendConstants[0], x.blendConstants[1], x.blendConstants[2], x.blendConstants[3], to_integral(x.logicOp), x.logicOpEnable);
			return h;
		}
	};


	template <>
	struct hash<vuk::PipelineBaseCreateInfo> {
		size_t operator()(vuk::PipelineBaseCreateInfo const & x) const noexcept {
            size_t h = 0;
            hash_combine(h, x.shaders, x.color_blend_state, x.color_blend_attachments, x.depth_stencil_state, x.rasterization_state);
            return h;
		}
	};

	template <>
	struct hash<vuk::ComputePipelineCreateInfo> {
		size_t operator()(vuk::ComputePipelineCreateInfo const & x) const noexcept {
            size_t h = 0;
            hash_combine(h, x.shader);
            return h;
		}
	};

	template <>
	struct hash<vuk::PipelineInstanceCreateInfo> {
		size_t operator()(vuk::PipelineInstanceCreateInfo const & x) const noexcept {
            size_t h = 0;
            hash_combine(h, x.base, reinterpret_cast<uint64_t>((VkRenderPass)x.render_pass), x.subpass);
            return h;
		}
	};

	template <>
	struct hash<vk::PushConstantRange> {
		size_t operator()(vk::PushConstantRange const & x) const noexcept {
			size_t h = 0;
			hash_combine(h, x.offset, x.size, (VkShaderStageFlags)x.stageFlags);
			return h;
		}
	};


	template <>
	struct hash<vuk::PipelineLayoutCreateInfo> {
		size_t operator()(vuk::PipelineLayoutCreateInfo const & x) const noexcept {
			size_t h = 0;
			hash_combine(h, x.pcrs, x.dslcis);
			return h;
		}
	};

};
