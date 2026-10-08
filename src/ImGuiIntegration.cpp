#include "ImGuiIntegration.hpp"

#include <AgniEngine.hpp>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <Editor/EditorTheme.hpp>

#include <Initializers.hpp>
#include <VulkanTools.hpp>

// Descriptor pool handle stored here (not in engine)
static VkDescriptorPool s_imguiPool = VK_NULL_HANDLE;

void ImGuiIntegration::init(AgniEngine& engine)
{
	// Create descriptor pool for ImGui
	VkDescriptorPoolSize poolSizes[] = {
	    {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
	    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
	    {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
	    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
	    {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
	    {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
	    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
	    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
	    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
	    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
	    {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	poolInfo.maxSets       = 1000;
	poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
	poolInfo.pPoolSizes    = poolSizes;

	VK_CHECK(vkCreateDescriptorPool(engine.m_device, &poolInfo, nullptr, &s_imguiPool));

	// Create ImGui context
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

	// Apply editor theme
	agni::editor::ThemeConfig themeConfig;
	themeConfig.fontSize = 15.0f;
	agni::editor::ConfigureFonts(io, themeConfig);
	agni::editor::ApplyDarkModernTheme(themeConfig);

	// Initialize SDL3 backend
	ImGui_ImplSDL3_InitForVulkan(engine.m_window);

	// Initialize Vulkan backend
	ImGui_ImplVulkan_InitInfo initInfo = {};
	initInfo.ApiVersion          = VK_API_VERSION_1_4;
	initInfo.Instance            = engine.m_instance;
	initInfo.PhysicalDevice      = engine.m_chosenGPU;
	initInfo.Device              = engine.m_device;
	initInfo.QueueFamily         = engine.m_graphicsQueueFamily;
	initInfo.Queue               = engine.m_graphicsQueue;
	initInfo.DescriptorPool      = s_imguiPool;
	initInfo.MinImageCount       = 3;
	initInfo.ImageCount          = 3;
	initInfo.UseDynamicRendering = true;

	initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = {
	    .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
	initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	VkFormat swapchainFormat = engine.m_swapchainManager.getSwapchainImageFormat();
	initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swapchainFormat;
	initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&initInfo);
}

void ImGuiIntegration::cleanup(AgniEngine& engine)
{
	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();
	vkDestroyDescriptorPool(engine.m_device, s_imguiPool, nullptr);
	s_imguiPool = VK_NULL_HANDLE;
}

void ImGuiIntegration::processEvent(SDL_Event& event)
{
	ImGui_ImplSDL3_ProcessEvent(&event);
}

void ImGuiIntegration::beginFrame()
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();
	ImGui::DockSpaceOverViewport(
	    0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
}

void ImGuiIntegration::endFrame()
{
	ImGui::Render();
}

void ImGuiIntegration::draw(VkCommandBuffer cmd, VkImageView targetView, AgniEngine& engine)
{
	VkRenderingAttachmentInfo colorAttachment = vkinit::attachmentInfo(
	    targetView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingInfo renderInfo = vkinit::renderingInfo(
	    engine.m_swapchainManager.getSwapchainExtent(), &colorAttachment, nullptr);

	vkCmdBeginRendering(cmd, &renderInfo);
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
	vkCmdEndRendering(cmd);
}
