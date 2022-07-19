#include "example_runner.hpp"
#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <stb_image.h>

/* 04_texture
 * In this example we will build on the previous examples (02_cube and 03_multipass), but we will make the cube textured.
 *
 * These examples are powered by the example framework, which hides some of the code required, as that would be repeated for each example.
 * Furthermore it allows launching individual examples and all examples with the example same code.
 * Check out the framework (example_runner_*) files if interested!
 */

namespace {
	float angle = 0.f;
	auto box = util::generate_cube();
	vuk::BufferGPU verts, inds;
	// A vuk::Texture is an owned pair of Image and ImageView
	// An optional is used here so that we can reset this on cleanup, despite being a global (which is to simplify the code here)
	std::optional<vuk::Texture> texture_of_doge;
	vuk::Unique<VkAccelerationStructureKHR> tlas, blas;
	vuk::Unique<vuk::BufferGPU> tlas_buf, blas_buf, tlas_scratch_buffer;

	vuk::Example x{
		.name = "12_rt_pipeline",
		.setup =
		    [](vuk::ExampleRunner& runner, vuk::Allocator& allocator) {
		      auto& ctx = allocator.get_context();
		      {
			      vuk::PipelineBaseCreateInfo pci;
			      pci.add_glsl(util::read_entire_file("../../examples/rt.rgen"), "rt.rgen");
			      pci.add_glsl(util::read_entire_file("../../examples/rt.rmiss"), "rt.rmiss");
			      pci.add_glsl(util::read_entire_file("../../examples/rt.rchit"), "rt.rchit");
			      pci.add_hit_group(vuk::HitGroup{ .type = vuk::HitGroupType::eTriangles, .closest_hit = 2 });
			      runner.context->create_named_pipeline("raytracing", pci);
		      }

		      // Use STBI to load the image
		      int x, y, chans;
		      auto doge_image = stbi_load("../../examples/doge.png", &x, &y, &chans, 4);

		      // Similarly to buffers, we allocate the image and enqueue the upload
		      auto [tex, tex_fut] = create_texture(allocator, vuk::Format::eR8G8B8A8Srgb, vuk::Extent3D{ (unsigned)x, (unsigned)y, 1u }, doge_image, true);
		      texture_of_doge = std::move(tex);
		      runner.enqueue_setup(std::move(tex_fut));
		      stbi_image_free(doge_image);

		      // We set up the cube data, same as in example 02_cube
		      auto [vert_buf, vert_fut] = create_buffer_gpu(allocator, vuk::DomainFlagBits::eTransferOnGraphics, std::span(box.first));
		      verts = *vert_buf;
		      auto [ind_buf, ind_fut] = create_buffer_gpu(allocator, vuk::DomainFlagBits::eTransferOnGraphics, std::span(box.second));
		      inds = *ind_buf;

		      // BLAS building
		      uint32_t maxPrimitiveCount = (uint32_t)box.second.size() / 3;

		      // Describe buffer as array of VertexObj.
		      VkAccelerationStructureGeometryTrianglesDataKHR triangles{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR };
		      triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT; // vec3 vertex position data.
		      triangles.vertexData.deviceAddress = verts.device_address;
		      triangles.vertexStride = sizeof(util::Vertex);
		      // Describe index data (32-bit unsigned int)
		      triangles.indexType = VK_INDEX_TYPE_UINT32;
		      triangles.indexData.deviceAddress = inds.device_address;
		      // Indicate identity transform by setting transformData to null device pointer.
		      triangles.transformData = {};
		      triangles.maxVertex = (uint32_t)box.first.size();

		      // Identify the above data as containing opaque triangles.
		      VkAccelerationStructureGeometryKHR as_geom{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
		      as_geom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
		      as_geom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
		      as_geom.geometry.triangles = triangles;

		      VkAccelerationStructureBuildGeometryInfoKHR blas_build_info{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
		      blas_build_info.dstAccelerationStructure = *blas;
		      blas_build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		      blas_build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		      blas_build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		      blas_build_info.geometryCount = 1;
		      blas_build_info.pGeometries = &as_geom;

		      VkAccelerationStructureBuildSizesInfoKHR blas_size_info{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };

		      ctx.vkGetAccelerationStructureBuildSizesKHR(
		          ctx.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &blas_build_info, &maxPrimitiveCount, &blas_size_info);

		      VkAccelerationStructureCreateInfoKHR blas_ci{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
		      blas_ci.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		      blas_ci.size = blas_size_info.accelerationStructureSize; // Will be used to allocate memory.
		      blas_buf = *vuk::allocate_buffer_gpu(allocator, { .mem_usage = vuk::MemoryUsage::eGPUonly, .size = blas_size_info.accelerationStructureSize });
		      blas_ci.buffer = blas_buf->buffer;
		      blas_ci.offset = blas_buf->offset;

		      blas = vuk::Unique<VkAccelerationStructureKHR>(allocator);
		      allocator.allocate_acceleration_structures({ &*blas, 1 }, { &blas_ci, 1 });

		      // Allocate the scratch memory
		      auto blas_scratch_buffer =
		          *vuk::allocate_buffer_gpu(allocator, vuk::BufferCreateInfo{ .mem_usage = vuk::MemoryUsage::eGPUonly, .size = blas_size_info.buildScratchSize });

		      // Update build information
		      blas_build_info.srcAccelerationStructure = VK_NULL_HANDLE;
		      blas_build_info.dstAccelerationStructure = *blas;
		      blas_build_info.scratchData.deviceAddress = blas_scratch_buffer->device_address;

		      // TLAS building
		      VkAccelerationStructureInstanceKHR rayInst{};
		      rayInst.transform = {};
		      rayInst.transform.matrix[0][0] = 1.f;
		      rayInst.transform.matrix[1][1] = 1.f;
		      rayInst.transform.matrix[2][2] = 1.f;
		      rayInst.instanceCustomIndex = 0; // gl_InstanceCustomIndexEXT
		      rayInst.accelerationStructureReference = blas_buf->device_address;
		      rayInst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
		      rayInst.mask = 0xFF;                                //  Only be hit if rayMask & instance.mask != 0
		      rayInst.instanceShaderBindingTableRecordOffset = 0; // We will use the same hit group for all objects

		      auto instances_buffer = vuk::create_buffer_cross_device(allocator, vuk::MemoryUsage::eCPUtoGPU, std::span{ &rayInst, 1 });
		      vuk::wait_for_futures(allocator, instances_buffer.second); // no-op

		      VkAccelerationStructureGeometryInstancesDataKHR instancesVk{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR };
		      instancesVk.data.deviceAddress = instances_buffer.first->device_address;

		      // Put the above into a VkAccelerationStructureGeometryKHR. We need to put the instances struct in a union and label it as instance data.
		      VkAccelerationStructureGeometryKHR topASGeometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
		      topASGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
		      topASGeometry.geometry.instances = instancesVk;

		      // Find sizes
		      VkAccelerationStructureBuildGeometryInfoKHR tlas_build_info{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
		      tlas_build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
		      tlas_build_info.geometryCount = 1;
		      tlas_build_info.pGeometries = &topASGeometry;
		      tlas_build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		      tlas_build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

		      uint32_t countInstance = 1;

		      VkAccelerationStructureBuildSizesInfoKHR tlas_size_info{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
		      ctx.vkGetAccelerationStructureBuildSizesKHR(
		          allocator.get_context().device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tlas_build_info, &countInstance, &tlas_size_info);

		      VkAccelerationStructureCreateInfoKHR tlas_ci{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
		      tlas_ci.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		      tlas_ci.size = tlas_size_info.accelerationStructureSize;
		      tlas_buf = *vuk::allocate_buffer_gpu(allocator, { .mem_usage = vuk::MemoryUsage::eGPUonly, .size = tlas_size_info.accelerationStructureSize });
		      tlas_ci.buffer = tlas_buf->buffer;
		      tlas_ci.offset = tlas_buf->offset;

		      tlas = vuk::Unique<VkAccelerationStructureKHR>(allocator);
		      allocator.allocate_acceleration_structures({ &*tlas, 1 }, { &tlas_ci, 1 });

		      // Allocate the scratch memory
		      tlas_scratch_buffer =
		          *vuk::allocate_buffer_gpu(allocator, vuk::BufferCreateInfo{ .mem_usage = vuk::MemoryUsage::eGPUonly, .size = tlas_size_info.buildScratchSize });

		      // Update build information
		      tlas_build_info.srcAccelerationStructure = VK_NULL_HANDLE;
		      tlas_build_info.dstAccelerationStructure = *tlas;
		      tlas_build_info.scratchData.deviceAddress = tlas_scratch_buffer->device_address;

		      // Build the TLAS
		      vuk::RenderGraph as_build("as_build");
		      as_build.attach_in("verts", std::move(vert_fut));
		      as_build.attach_in("inds", std::move(ind_fut));
		      as_build.attach_buffer("blas_buf", *blas_buf);
		      as_build.attach_buffer("tlas_buf", *tlas_buf);
		      as_build.add_pass({ .resources = { "blas_buf"_buffer >> vuk::eAccelerationStructureBuildWrite,
		                                         "verts"_buffer >> vuk::eAccelerationStructureBuildRead,
		                                         "inds"_buffer >> vuk::eAccelerationStructureBuildRead },
		                          .execute = [maxPrimitiveCount, as_geom, blas_build_info](vuk::CommandBuffer& command_buffer) mutable {
			                          blas_build_info.pGeometries = &as_geom;

			                          // The entire array will be used to build the BLAS.
			                          VkAccelerationStructureBuildRangeInfoKHR blas_offset;
			                          blas_offset.firstVertex = 0;
			                          blas_offset.primitiveCount = maxPrimitiveCount;
			                          blas_offset.primitiveOffset = 0;
			                          blas_offset.transformOffset = 0;
			                          const VkAccelerationStructureBuildRangeInfoKHR* pblas_offset = &blas_offset;
			                          command_buffer.build_acceleration_structures(1, &blas_build_info, &pblas_offset);
		                          } });
		      as_build.add_pass(
		          { .resources = { "blas_buf+"_buffer >> vuk::eAccelerationStructureBuildRead, "tlas_buf"_buffer >> vuk::eAccelerationStructureBuildWrite },
		            .execute = [countInstance, topASGeometry, tlas_build_info](vuk::CommandBuffer& command_buffer) mutable {
			            tlas_build_info.pGeometries = &topASGeometry;

			            // Build Offsets info: n instances
			            VkAccelerationStructureBuildRangeInfoKHR tlas_offset{ countInstance, 0, 0, 0 };
			            const VkAccelerationStructureBuildRangeInfoKHR* ptlas_offset = &tlas_offset;
			            command_buffer.build_acceleration_structures(1, &tlas_build_info, &ptlas_offset);
		            } });
		      // For the example, we just ask these that these uploads complete before moving on to rendering
		      // In an engine, you would integrate these uploads into some explicit system
		      runner.enqueue_setup(vuk::Future(std::make_shared<vuk::RenderGraph>(std::move(as_build)), "tlas_buf+"));
		    },
		.render =
		    [](vuk::ExampleRunner& runner, vuk::Allocator& frame_allocator, vuk::Future target) {
		      auto& ctx = frame_allocator.get_context();
		      struct VP {
			      glm::mat4 inv_view;
			      glm::mat4 inv_proj;
		      } vp;
		      vp.inv_view = glm::lookAt(glm::vec3(0, 1.5, 3.5), glm::vec3(0), glm::vec3(0, 1, 0));
		      vp.inv_proj = glm::perspective(glm::degrees(70.f), 1.f, 1.f, 100.f);
		      vp.inv_proj[1][1] *= -1;
		      vp.inv_view = glm::inverse(vp.inv_view);
		      vp.inv_proj = glm::inverse(vp.inv_proj);

		      auto [buboVP, uboVP_fut] = create_buffer_cross_device(frame_allocator, vuk::MemoryUsage::eCPUtoGPU, std::span(&vp, 1));
		      auto uboVP = *buboVP;

		      vuk::wait_for_futures(frame_allocator, uboVP_fut);

		      // TLAS building
		      VkAccelerationStructureInstanceKHR rayInst{};
		      rayInst.transform = {};
		      glm::mat4 model_transform = static_cast<glm::mat4>(glm::angleAxis(glm::radians(angle), glm::vec3(0.f, 1.f, 0.f)));
		      glm::mat3x4 reduced_model_transform = static_cast<glm::mat3x4>(model_transform);
		      memcpy(&rayInst.transform.matrix, &reduced_model_transform, sizeof(glm::mat3x4));
		      rayInst.instanceCustomIndex = 0; // gl_InstanceCustomIndexEXT
		      rayInst.accelerationStructureReference = blas_buf->device_address;
		      rayInst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
		      rayInst.mask = 0xFF;                                //  Only be hit if rayMask & instance.mask != 0
		      rayInst.instanceShaderBindingTableRecordOffset = 0; // We will use the same hit group for all objects

		      auto [instances_buffer, instances_fut] = vuk::create_buffer_cross_device(frame_allocator, vuk::MemoryUsage::eCPUtoGPU, std::span{ &rayInst, 1 });
		      vuk::wait_for_futures(frame_allocator, instances_fut); // no-op

		      vuk::RenderGraph rg("12");
		      rg.attach_in("12_rt", std::move(target));
		      rg.attach_buffer("tlas", *tlas_buf);
		      rg.add_pass({ .resources = { "tlas"_buffer >> vuk::eAccelerationStructureBuildWrite },
		                    .execute = [inst_buf = *instances_buffer](vuk::CommandBuffer& command_buffer) {
			                    // TLAS update
			                    VkAccelerationStructureGeometryInstancesDataKHR instancesVk{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR };
			                    instancesVk.data.deviceAddress = inst_buf.device_address;

			                    VkAccelerationStructureGeometryKHR topASGeometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
			                    topASGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
			                    topASGeometry.geometry.instances = instancesVk;

			                    VkAccelerationStructureBuildGeometryInfoKHR tlas_build_info{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
			                    tlas_build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
			                    tlas_build_info.geometryCount = 1;
			                    tlas_build_info.pGeometries = &topASGeometry;
			                    tlas_build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
			                    tlas_build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

			                    tlas_build_info.srcAccelerationStructure = *tlas;
			                    tlas_build_info.dstAccelerationStructure = *tlas;
			                    tlas_build_info.scratchData.deviceAddress = tlas_scratch_buffer->device_address;

			                    VkAccelerationStructureBuildRangeInfoKHR tlas_offset{ 1, 0, 0, 0 };
			                    const VkAccelerationStructureBuildRangeInfoKHR* ptlas_offset = &tlas_offset;
			                    command_buffer.build_acceleration_structures(1, &tlas_build_info, &ptlas_offset);
		                    } });

			  rg.attach_image("12_rt_target", vuk::ImageAttachment{ .format = vuk::Format::eR8G8B8A8Unorm, .sample_count = vuk::SampleCountFlagBits::e1, .layer_count = 1 });
		      rg.inference_rule("12_rt_target", vuk::same_shape_as("12_rt"));
		      rg.add_pass({ .resources = { "12_rt_target"_image >> vuk::eRayTracingWrite, "tlas+"_buffer >> vuk::eRayTracingRead },
		                    .execute = [uboVP](vuk::CommandBuffer& command_buffer) {
			                    command_buffer.bind_acceleration_structure(0, 0, *tlas)
			                        .bind_image(0, 1, "12_rt_target")
			                        .bind_buffer(0, 2, uboVP)
			                        .bind_ray_tracing_pipeline("raytracing");
			                    command_buffer.trace_rays(1024, 1024, 1);
		                    } });
		      rg.add_pass({ .resources = { "12_rt_target+"_image >> vuk::eTransferRead, "12_rt"_image >> vuk::eTransferWrite },
		                    .execute = [uboVP](vuk::CommandBuffer& command_buffer) {
			                    vuk::ImageBlit blit;
			                    blit.srcSubresource.aspectMask = vuk::ImageAspectFlagBits::eColor;
			                    blit.srcSubresource.baseArrayLayer = 0;
			                    blit.srcSubresource.layerCount = 1;
			                    blit.srcSubresource.mipLevel = 0;
			                    blit.dstSubresource = blit.srcSubresource;
			                    auto extent = command_buffer.get_resource_image_attachment("12_rt_target+")->extent;
			                    blit.srcOffsets[1] = vuk::Offset3D{ static_cast<int>(extent.extent.width), static_cast<int>(extent.extent.height), 1 };
			                    blit.dstOffsets[1] = blit.srcOffsets[1];
			                    command_buffer.blit_image("12_rt_target+", "12_rt", blit, vuk::Filter::eNearest);
		                    } });
		      angle += 20.f * ImGui::GetIO().DeltaTime;

		      return vuk::Future{ std::make_unique<vuk::RenderGraph>(std::move(rg)), "12_rt_final" };
		    },

		// Perform cleanup for the example
		.cleanup =
		    [](vuk::ExampleRunner& runner, vuk::Allocator& allocator) {
		      // We release the texture resources
		      texture_of_doge.reset();
		      tlas.reset();
		      tlas_buf.reset();
		      blas.reset();
		      blas_buf.reset();
		      tlas_scratch_buffer.reset();
		    }
	};

	REGISTER_EXAMPLE(x);
} // namespace