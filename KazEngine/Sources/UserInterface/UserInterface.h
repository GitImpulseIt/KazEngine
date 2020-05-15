#pragma once

#include "../Vulkan/Vulkan.h"
#include "../DescriptorSet/DescriptorSet.h"
#include "../DataBank/DataBank.h"
#include "../Platform/Common/Mouse/Mouse.h"
#include "../Camera/Camera.h"
#include "IUserInteraction.h"
#include <EventEmitter.hpp>

namespace Engine
{
    class UserInterface : public IMouseListener, public Tools::EventEmitter<IUserInteraction>
    {
        public :

            void Clear();
            inline ~UserInterface() { this->Clear(); }
            UserInterface(VkCommandPool command_pool);
            VkCommandBuffer BuildCommandBuffer(uint8_t frame_index, VkFramebuffer framebuffer);
            void Update(uint8_t frame_index);
            inline void Refresh() { for(int i=0; i<this->need_update.size(); i++) this->need_update[i] = true; }

            //////////////////////////////
            // IMouseListener interface //
            //////////////////////////////

            virtual void MouseMove(unsigned int x, unsigned int y);
            virtual void MouseButtonDown(MOUSE_BUTTON button);
            virtual void MouseButtonUp(MOUSE_BUTTON button);
            inline virtual void MouseWheelUp() {};
            inline virtual void MouseWheelDown() {};

        private :

            struct MOUSE_SELECTION_SQUARE {
                Point<uint32_t> start;
                Point<uint32_t> end;
                uint32_t display;
            };

            VkCommandPool command_pool;
            std::vector<VkCommandBuffer> command_buffers;
            Vulkan::PIPELINE pipeline;
            DescriptorSet texture_descriptor;
            VkRenderPass render_pass;
            std::vector<bool> need_update;
            Chunk ui_vbo_chunk;
            MOUSE_SELECTION_SQUARE selection_square;
            DescriptorSet selection_descriptor;
            std::vector<bool> update_selection_square;

            bool SetupPipeline();
            bool SetupRenderPass();
            bool UpdateVertexBuffer(uint8_t frame_index);
    };
}