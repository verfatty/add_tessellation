#include "../src/RenderGraphUtil.hpp"
#include "example_runner.hpp"

std::vector<vuk::Name> chosen_resource;

vuk::ExampleRunner::ExampleRunner() {
	vkb::InstanceBuilder builder;
	builder.request_validation_layers()
	    .set_debug_callback([](VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	                           VkDebugUtilsMessageTypeFlagsEXT messageType,
	                           const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	                           void* pUserData) -> VkBool32 {
		    auto ms = vkb::to_string_message_severity(messageSeverity);
		    auto mt = vkb::to_string_message_type(messageType);
		    printf("[%s: %s](user defined)\n%s\n", ms, mt, pCallbackData->pMessage);
		    return VK_FALSE;
	    })
	    .set_app_name("vuk_example")
	    .set_engine_name("vuk")
	    .require_api_version(1, 2, 0)
	    .set_app_version(0, 1, 0);
	auto inst_ret = builder.build();
	if (!inst_ret.has_value()) {
		// error
	}
	vkbinstance = inst_ret.value();
	auto instance = vkbinstance.instance;
	vkb::PhysicalDeviceSelector selector{ vkbinstance };
	window = create_window_glfw("Vuk All Examples", false);
	surface = create_surface_glfw(vkbinstance.instance, window);
	selector.set_surface(surface).set_minimum_version(1, 0).add_required_extension(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
	auto phys_ret = selector.select();
	if (!phys_ret.has_value()) {
		// error
	}
	vkb::PhysicalDevice vkbphysical_device = phys_ret.value();
	physical_device = vkbphysical_device.physical_device;

	vkb::DeviceBuilder device_builder{ vkbphysical_device };
	VkPhysicalDeviceVulkan12Features vk12features{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
	vk12features.timelineSemaphore = true;
	vk12features.descriptorBindingPartiallyBound = true;
	vk12features.descriptorBindingUpdateUnusedWhilePending = true;
	vk12features.shaderSampledImageArrayNonUniformIndexing = true;
	vk12features.runtimeDescriptorArray = true;
	vk12features.descriptorBindingVariableDescriptorCount = true;
	vk12features.hostQueryReset = true;
	VkPhysicalDeviceVulkan11Features vk11features{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
	vk11features.shaderDrawParameters = true;
	VkPhysicalDeviceSynchronization2FeaturesKHR sync_feat{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR, .synchronization2 = true };
	auto dev_ret = device_builder.add_pNext(&vk12features).add_pNext(&vk11features).add_pNext(&sync_feat).build();
	if (!dev_ret.has_value()) {
		// error
	}
	vkbdevice = dev_ret.value();
	graphics_queue = vkbdevice.get_queue(vkb::QueueType::graphics).value();
	auto graphics_queue_family_index = vkbdevice.get_queue_index(vkb::QueueType::graphics).value();
	transfer_queue = vkbdevice.get_queue(vkb::QueueType::transfer).value();
	auto transfer_queue_family_index = vkbdevice.get_queue_index(vkb::QueueType::transfer).value();
	device = vkbdevice.device;

	context.emplace(ContextCreateParameters{ instance,
	                                         device,
	                                         physical_device,
	                                         graphics_queue,
	                                         graphics_queue_family_index,
	                                         VK_NULL_HANDLE,
	                                         VK_QUEUE_FAMILY_IGNORED,
	                                         transfer_queue,
	                                         transfer_queue_family_index });
	const unsigned num_inflight_frames = 3;
	xdev_rf_alloc.emplace(*context, num_inflight_frames);
	global.emplace(*xdev_rf_alloc);
	swapchain = context->add_swapchain(util::make_swapchain(vkbdevice));
}

bool render_all = false;

void vuk::ExampleRunner::render() {
	chosen_resource.resize(examples.size());

	vuk::wait_for_futures_explicit(*global, futures);
	futures.clear();

	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 352.f, 2));
		ImGui::SetNextWindowSize(ImVec2(350, 0));
		ImGui::Begin("Example selector", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize);
		ImGui::Checkbox("All", &render_all);
		ImGui::SameLine();

		static vuk::Example* item_current = examples[7]; // Here our selection is a single pointer stored outside the object.
		if (!render_all) {
			if (ImGui::BeginCombo("Examples", item_current->name.data(), ImGuiComboFlags_None)) {
				for (int n = 0; n < examples.size(); n++) {
					bool is_selected = (item_current == examples[n]);
					if (ImGui::Selectable(examples[n]->name.data(), is_selected))
						item_current = examples[n];
					if (is_selected)
						ImGui::SetItemDefaultFocus(); // Set the initial focus when opening the combo (scrolling + for keyboard navigation support in the upcoming
						                              // navigation branch)
				}
				ImGui::EndCombo();
			}
		}
		ImGui::End();

		auto& xdev_frame_resource = xdev_rf_alloc->get_next_frame();
		context->next_frame();
		Allocator frame_allocator(xdev_frame_resource);
		if (!render_all) { // render a single full window example
			RenderGraph rg("runner");
			vuk::Name attachment_name = item_current->name;
			rg.attach_swapchain("_swp", swapchain);
			rg.clear_image("_swp", attachment_name, vuk::ClearColor{ 0.3f, 0.5f, 0.3f, 1.0f });
			auto fut = item_current->render(*this, frame_allocator, Future{ rg, attachment_name });
			ImGui::Render();

			fut = util::ImGui_ImplVuk_Render(frame_allocator, std::move(fut), imgui_data, ImGui::GetDrawData(), sampled_images);
			present(frame_allocator, swapchain, std::move(fut));
			sampled_images.clear();
		} else { // render all examples as imgui windows
			RenderGraph rg("runner");
			plf::colony<vuk::Name> attachment_names;

			size_t i = 0;
			for (auto& ex : examples) {
				RenderGraph rgx(ex->name);
				ImGui::Begin(ex->name.data());
				auto size = ImGui::GetContentRegionAvail();
				rgx.attach_and_clear_image("_img",
				                           { .extent = vuk::Dimension3D::absolute((uint32_t)size.x, (uint32_t)size.y),
				                             .format = swapchain->format,
				                             .sample_count = vuk::Samples::e1,
				                             .level_count = 1,
				                             .layer_count = 1 },
				                           vuk::ClearColor(0.1f, 0.2f, 0.3f, 1.f));
				auto rg_frag_fut = ex->render(*this, frame_allocator, Future{ rgx, "_img" });
				Name attachment_name_in = Name(ex->name);
				Name& attachment_name_out = *attachment_names.emplace(std::string(ex->name) + "_final");
				auto& rg_frag = *rg_frag_fut.get_render_graph();
				rg_frag.compile(vuk::RenderGraphCompileOptions{});
				if (rg_frag.get_use_chains().size() > 1) {
					const auto& bound_attachments = rg_frag.get_bound_attachments();
					bool disable = false;
					for (const auto [key, use_refs] : rg_frag.get_use_chains()) {
						auto bound_it = bound_attachments.find(key);
						if (bound_it == bound_attachments.end())
							continue;
						auto samples = vuk::SampleCountFlagBits::e1;
						auto& att_info = (*bound_it).second;
						if (att_info.attachment.sample_count != vuk::Samples::eInfer)
							samples = att_info.attachment.sample_count.count;
						disable = disable || (samples != vuk::SampleCountFlagBits::e1);
					}

					for (const auto [key, use_refs] : rg_frag.get_use_chains()) {
						auto bound_it = bound_attachments.find(key);
						if (bound_it == bound_attachments.end())
							continue;
						std::string btn_id = "";
						bool prevent_disable = false;
						if (key.to_sv() == attachment_name_out) {
							prevent_disable = true;
							btn_id = "F";
						} else {
							auto usage = rg_frag.compute_usage(use_refs);
							if (usage & vuk::ImageUsageFlagBits::eColorAttachment) {
								btn_id += "C";
							} else if (usage & vuk::ImageUsageFlagBits::eDepthStencilAttachment) {
								btn_id += "D";
							} else if (usage & (vuk::ImageUsageFlagBits::eTransferSrc | vuk::ImageUsageFlagBits::eTransferDst)) {
								btn_id += "X";
							}
						}
						if (disable && !prevent_disable) {
							btn_id += " (MS)";
						} else {
							btn_id += "##" + std::string(key.to_sv());
						}
						if (disable && !prevent_disable) {
							ImGui::TextDisabled("%s", btn_id.c_str());
						} else {
							if (ImGui::Button(btn_id.c_str())) {
								if (key.to_sv() == ex->name) {
									chosen_resource[i] = attachment_name_out;
								} else {
									Name last_use = use_refs.back().out_name.is_invalid() ? use_refs.back().name : use_refs.back().out_name;
									chosen_resource[i] = last_use;
								}
							}
						}
						if (ImGui::IsItemHovered())
							ImGui::SetTooltip("%s", key.c_str());
						ImGui::SameLine();
					}
					ImGui::NewLine();
				}
				if (chosen_resource[i].is_invalid())
					chosen_resource[i] = attachment_name_out;

				if (chosen_resource[i] != attachment_name_out) {
					auto othfut = Future(rg_frag, chosen_resource[i]);
					rg.attach_in(attachment_name_out, std::move(othfut));
					rg.attach_in("_", std::move(rg_frag_fut));
				} else {
					rg.attach_in(attachment_name_out, std::move(rg_frag_fut));
				}
				// hacky way to reference image in the subgraph
				// TODO: a proper way to do this?
				auto si = vuk::make_sampled_image(rg.name.append("::").append(attachment_name_out), imgui_data.font_sci);
				ImGui::Image(&*sampled_images.emplace(si), ImGui::GetContentRegionAvail());
				ImGui::End();
				i++;
			}

			ImGui::Render();
			rg.clear_image("SWAPCHAIN", "SWAPCHAIN+", vuk::ClearColor{ 0.3f, 0.5f, 0.3f, 1.0f });
			rg.attach_swapchain("SWAPCHAIN", swapchain);
			auto fut = util::ImGui_ImplVuk_Render(frame_allocator, Future{ rg, "SWAPCHAIN+" }, imgui_data, ImGui::GetDrawData(), sampled_images);
			present(frame_allocator, swapchain, std::move(fut));
			sampled_images.clear();
		}
	}
}

int main() {
	vuk::ExampleRunner::get_runner().setup();
	vuk::ExampleRunner::get_runner().render();
	vuk::ExampleRunner::get_runner().cleanup();
}
