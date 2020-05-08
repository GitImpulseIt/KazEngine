#include "Core.h"

namespace Engine
{
    void Core::Clear()
    {
        // Map
        if(this->map != nullptr) delete this->map;

        // Entity Render
        if(this->entity_render != nullptr) delete this->entity_render;

        // Camera
        Camera::DestroyInstance();

        if(Vulkan::HasInstance()) {
            vkDeviceWaitIdle(Vulkan::GetDevice());

            // Stop listening to window events
            Vulkan::GetInstance().GetDrawWindow()->RemoveListener(this);

            // Transfer command buffers
            for(auto& buffer : this->transfer_buffers)
                if(buffer.fence != nullptr) vkDestroyFence(Vulkan::GetDevice(), buffer.fence, nullptr);

            // Rendering resources
            this->DestroyRenderingResources();
        }

        // DataBank
        DataBank::DestroyInstance();

        this->graphics_command_pool = nullptr;
        this->map                   = nullptr;
        this->entity_render         = nullptr;
    }

    bool Core::Initialize()
    {
        // Clean all objects
        this->Clear();
        
        // Listen to window events
        Vulkan::GetInstance().GetDrawWindow()->AddListener(this);

        // Create a storage for engine objects
        DataBank::CreateInstance();

        // Alocate vulkan resources
        if(!this->AllocateRenderingResources()) return false;

        // Allocate transfer command buffers
        uint8_t frame_count = Vulkan::GetConcurrentFrameCount();
        this->transfer_buffers.resize(frame_count);
        if(!Vulkan::GetInstance().CreateCommandBuffer(this->graphics_command_pool, this->transfer_buffers)) {
            #if defined(DISPLAY_LOGS)
            std::cout << "Core::Initialize() => CreateCommandBuffer [transfer] : Failed" << std::endl;
            #endif
        }

        // Allocate data buffers
        DataBank::GetManagedBuffer().Allocate(SIZE_MEGABYTE(20), MULTI_USAGE_BUFFER_MASK, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                     frame_count, true, {Vulkan::GetGraphicsQueue().index});

        // Initialize Camera
        Camera::CreateInstance();

        // Create the map
        this->map = new Map(this->graphics_command_pool);

        // Create a renderer for entities
        this->entity_render = new EntityRender(this->graphics_command_pool);

        // Success
        return true;
    }

    bool Core::AllocateRenderingResources()
    {
        if(!Vulkan::GetInstance().CreateCommandPool(this->graphics_command_pool, Vulkan::GetInstance().GetGraphicsQueue().index)) return false;

        // On veut autant de ressources qu'il y a d'images dans la Swap Chain
        uint8_t frame_count = Vulkan::GetConcurrentFrameCount();
        this->resources.resize(frame_count);
        
        for(uint32_t i=0; i<this->resources.size(); i++) {
            
            // Frame Buffers
            if(!Vulkan::GetInstance().CreateFrameBuffer(this->resources[i].framebuffer, Vulkan::GetSwapChain().images[i].view)) return false;

            // Semaphores
            if(!Vulkan::GetInstance().CreateSemaphore(this->resources[i].semaphore)) return false;
        }

        // Graphics Command Buffers
        std::vector<Vulkan::COMMAND_BUFFER> graphics_buffers(frame_count);
        if(!Vulkan::GetInstance().CreateCommandBuffer(this->graphics_command_pool, graphics_buffers)) return false;
        for(uint32_t i=0; i<this->resources.size(); i++) this->resources[i].graphics_command_buffer = graphics_buffers[i];

        // SwapChain semaphores
        this->swap_chain_semaphores.resize(frame_count);
        for(auto& semaphore : this->swap_chain_semaphores)
            if(!Vulkan::GetInstance().CreateSemaphore(semaphore)) return false;

        return true;
    }

    void Core::DestroyRenderingResources()
    {
        // SwapChain semaphores
        for(auto& semaphore : this->swap_chain_semaphores) vkDestroySemaphore(Vulkan::GetDevice(), semaphore, nullptr);
        this->swap_chain_semaphores.clear();

        // Graphics Command Buffers
        std::vector<Vulkan::COMMAND_BUFFER> graphics_buffers(this->resources.size());
        for(uint32_t i=0; i<this->resources.size(); i++) graphics_buffers[i] = this->resources[i].graphics_command_buffer;
        Vulkan::GetInstance().ReleaseCommandBuffer(this->graphics_command_pool, graphics_buffers);

        for(uint32_t i=0; i<this->resources.size(); i++) {
            
            // Semaphore
            if(this->resources[i].semaphore != nullptr) vkDestroySemaphore(Vulkan::GetDevice(), this->resources[i].semaphore, nullptr);

            // Frame Buffer
            if(this->resources[i].semaphore != nullptr) vkDestroyFramebuffer(Vulkan::GetDevice(), this->resources[i].framebuffer, nullptr);
        }

        this->resources.clear();
    }

    bool Core::BuildRenderPass(uint32_t swap_chain_image_index)
    {
        Vulkan::RENDERING_RESOURCES const& resources = this->resources[swap_chain_image_index];
        VkCommandBuffer command_buffer = resources.graphics_command_buffer.handle;
        VkFramebuffer frame_buffer = resources.framebuffer;

        VkResult result = vkWaitForFences(Vulkan::GetDevice(), 1, &resources.graphics_command_buffer.fence, VK_FALSE, 1000000000);
        if(result != VK_SUCCESS) {
            #if defined(DISPLAY_LOGS)
            std::cout << "PresentImage => vkWaitForFences : Timeout" << std::endl;
            #endif
            return false;
        }
        vkResetFences(Vulkan::GetDevice(), 1, &resources.graphics_command_buffer.fence);

        VkCommandBufferBeginInfo command_buffer_begin_info;
        command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        command_buffer_begin_info.pNext = nullptr;
        command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        command_buffer_begin_info.pInheritanceInfo = nullptr;

        result = vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);
        if(result != VK_SUCCESS) {
            #if defined(DISPLAY_LOGS)
            std::cout << "BuildCommandBuffer[" << swap_chain_image_index << "] => vkBeginCommandBuffer : Failed" << std::endl;
            #endif
            return false;
        }

        std::array<VkClearValue, 2> clear_value = {};
        clear_value[0].color = { 0.1f, 0.2f, 0.3f, 0.0f };
        clear_value[1].depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo render_pass_begin_info = {};
        render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        render_pass_begin_info.pNext = nullptr;
        render_pass_begin_info.renderPass = Vulkan::GetRenderPass();
        render_pass_begin_info.framebuffer = frame_buffer;
        render_pass_begin_info.renderArea.offset.x = 0;
        render_pass_begin_info.renderArea.offset.y = 0;
        render_pass_begin_info.renderArea.extent.width = Vulkan::GetDrawSurface().width;
        render_pass_begin_info.renderArea.extent.height = Vulkan::GetDrawSurface().height;
        render_pass_begin_info.clearValueCount = static_cast<uint32_t>(clear_value.size());
        render_pass_begin_info.pClearValues = clear_value.data();

        // D�but de la render pass primaire
        vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

        // Ex�cution des render passes secondaires
        VkCommandBuffer command_buffers[2] = {
            this->entity_render->GetCommandBuffer(swap_chain_image_index, frame_buffer),
            this->map->GetCommandBuffer(swap_chain_image_index, frame_buffer)
        };
        vkCmdExecuteCommands(command_buffer, 2, command_buffers);

        // Fin de la render pass primaire
        vkCmdEndRenderPass(command_buffer);

        result = vkEndCommandBuffer(command_buffer);
        if(result != VK_SUCCESS) {
            #if defined(DISPLAY_LOGS)
            std::cout << "BuildCommandBuffer[" << swap_chain_image_index << "] => vkEndCommandBuffer : Failed" << std::endl;
            #endif
            return false;
        }

        return true;
    }

    bool Core::RebuildFrameBuffers()
    {
        if(Vulkan::HasInstance()) {
            for(uint32_t i=0; i<this->resources.size(); i++) {
                if(this->resources[i].framebuffer != nullptr) {

                    // Destruction du Frame Buffer
                    vkDestroyFramebuffer(Vulkan::GetDevice(), this->resources[i].framebuffer, nullptr);
                    this->resources[i].framebuffer = nullptr;

                    // Cr�ation du Frame Buffer
                    if(!Vulkan::GetInstance().CreateFrameBuffer(this->resources[i].framebuffer, Vulkan::GetSwapChain().images[i].view)) return false;
                }
            }
        }

        // Succ�s
        return true;
    }

    void Core::StateChanged(E_WINDOW_STATE window_state)
    {
    }

    void Core::SizeChanged(Area<uint32_t> size)
    {
        Vulkan::GetInstance().OnWindowSizeChanged();

        // On recr�� la matrice de projection en cas de changement de ratio
        auto& surface = Vulkan::GetDrawSurface();
        float aspect_ratio = static_cast<float>(surface.width) / static_cast<float>(surface.height);
        Camera::GetInstance().GetUniformBuffer().projection = Maths::Matrix4x4::PerspectiveProjectionMatrix(aspect_ratio, 60.0f, 0.1f, 2000.0f);

        // Reconstruction du Frame Buffer et des Command Buffers
        this->RebuildFrameBuffers();

        // Force rebuild map command buffer
        this->map->Refresh();
    }

    void Core::Loop()
    {
        static uint32_t current_semaphore_index = (current_semaphore_index + 1) % this->swap_chain_semaphores.size();
        VkSemaphore semaphore = this->swap_chain_semaphores[current_semaphore_index];

        // Acquire image
        uint32_t swap_chain_image_index;
        if(!Vulkan::GetInstance().AcquireNextImage(swap_chain_image_index, semaphore)) return;

        // Update global timer
        Timer::Update();

        // Update uniform buffer
        Camera::GetInstance().Update(swap_chain_image_index);
        this->map->Update(swap_chain_image_index);
        this->entity_render->Update(swap_chain_image_index);
        DataBank::GetManagedBuffer().Flush(this->transfer_buffers[swap_chain_image_index], swap_chain_image_index);

        // Build image
        this->BuildRenderPass(swap_chain_image_index);

        // Present image
        if(!Vulkan::GetInstance().PresentImage(this->resources[swap_chain_image_index], semaphore, swap_chain_image_index)) {

            // On recr�� la matrice de projection en cas de changement de ratio
            auto& surface = Vulkan::GetDrawSurface();
            float aspect_ratio = static_cast<float>(surface.width) / static_cast<float>(surface.height);
            Camera::GetInstance().GetUniformBuffer().projection = Maths::Matrix4x4::PerspectiveProjectionMatrix(aspect_ratio, 60.0f, 0.1f, 2000.0f);

            // Reconstruction du Frame Buffer et des Command Buffers
            this->RebuildFrameBuffers();

            // Force rebuild map command buffer
            this->map->Refresh();
        }
    }
}