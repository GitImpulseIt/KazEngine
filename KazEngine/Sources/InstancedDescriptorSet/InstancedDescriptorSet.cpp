#include "InstancedDescriptorSet.h"
#include "../GlobalData/GlobalData.h"
#include "../LOD/LOD.h"
#include "IInstancedDescriptorListener.h"

namespace Engine
{
    InstancedDescriptorSet::InstancedDescriptorSet()
    {
        this->layout            = nullptr;
        this->pool              = nullptr;
    }

    InstancedDescriptorSet& InstancedDescriptorSet::operator=(InstancedDescriptorSet&& other)
    {
        if(&other != this) {
            this->pool              = other.pool;
            this->layout            = other.layout;
            this->sets              = std::move(other.sets);
            this->layout_bindings   = std::move(other.layout_bindings);
            this->bindings          = std::move(other.bindings);

            other.pool              = nullptr;
            other.layout            = nullptr;
        }

        return *this;
    }

    void InstancedDescriptorSet::Clear()
    {
        vkDeviceWaitIdle(Vulkan::GetDevice());

        if(this->layout != nullptr) vkDestroyDescriptorSetLayout(Vulkan::GetDevice(), this->layout, nullptr);
        if(this->pool != nullptr) vkDestroyDescriptorPool(Vulkan::GetDevice(), this->pool, nullptr);

        for(auto binding : this->bindings)
            GlobalData::GetInstance()->instanced_buffer.GetChunk()->FreeChild(binding.chunk);

        this->sets.clear();
        this->bindings.clear();

        this->layout            = nullptr;
        this->pool              = nullptr;
    }

    void InstancedDescriptorSet::WriteData(const void* data, VkDeviceSize size, size_t offset, uint8_t binding, uint8_t instance_id)
    {
        GlobalData::GetInstance()->instanced_buffer.WriteData(data, size, this->bindings[binding].chunk->offset + offset, instance_id);
    }

    VkDescriptorSetLayoutBinding InstancedDescriptorSet::CreateSimpleBinding(uint32_t binding, VkDescriptorType type, VkShaderStageFlags stage_flags)
    {
        VkDescriptorSetLayoutBinding layout_binding;
        layout_binding.binding = binding;
        layout_binding.descriptorType = type;
        layout_binding.stageFlags = stage_flags;
        layout_binding.descriptorCount = 1;
        layout_binding.pImmutableSamplers = nullptr;
        return layout_binding;
    }

    bool InstancedDescriptorSet::Create(std::vector<BINDING_INFOS> infos)
    {
        this->Clear();
        this->bindings.resize(infos.size());

        std::vector<VkDescriptorSetLayoutBinding> layout_bindings;
        for(uint32_t i=0; i<infos.size(); i++) {
            
            this->bindings[i].layout.binding = i;
            this->bindings[i].layout.descriptorType = infos[i].type;
            this->bindings[i].layout.stageFlags = infos[i].stage;
            this->bindings[i].layout.descriptorCount = 1;
            this->bindings[i].layout.pImmutableSamplers = nullptr;

            VkDeviceSize alignment = 0;
            switch(this->bindings[i].layout.descriptorType) {
                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER :
                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC :
                    alignment = Vulkan::UboAlignment();
                    break;

                case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER :
                case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC :
                    alignment = Vulkan::SboAlignment();
                    break;
            }

            this->bindings[i].chunk = GlobalData::GetInstance()->instanced_buffer.GetChunk()->ReserveRange(infos[i].size, alignment);
            this->bindings[i].need_update.resize(Vulkan::GetSwapChainImageCount());
            layout_bindings.push_back(this->bindings[i].layout);
        }

        ////////////////////////
        // Cr�ation du layout //
        ////////////////////////

        VkDescriptorSetLayoutCreateInfo descriptor_layout;
        descriptor_layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptor_layout.flags = 0;
        descriptor_layout.pNext = nullptr;
        descriptor_layout.bindingCount = static_cast<uint32_t>(layout_bindings.size());
        descriptor_layout.pBindings = layout_bindings.data();

        VkResult result = vkCreateDescriptorSetLayout(Vulkan::GetDevice(), &descriptor_layout, nullptr, &this->layout);
        if(result != VK_SUCCESS) {
            #if defined(DISPLAY_LOGS)
            std::cout << "DescriptorSet::PrepareDescriptor => vkCreateDescriptorSetLayout : Failed" << std::endl;
            #endif
            return false;
        }

        //////////////////////
        // Cr�ation du pool //
        //////////////////////

        std::vector<VkDescriptorPoolSize> pool_sizes(layout_bindings.size());
        for(uint8_t i=0; i<pool_sizes.size(); i++) {
            pool_sizes[i].type = layout_bindings[i].descriptorType;
            pool_sizes[i].descriptorCount = layout_bindings[i].descriptorCount * GlobalData::GetInstance()->instanced_buffer.GetInstanceCount();
        }

		VkDescriptorPoolCreateInfo poolInfo = {};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
		poolInfo.pPoolSizes = pool_sizes.data();
        poolInfo.maxSets = GlobalData::GetInstance()->instanced_buffer.GetInstanceCount();

		result = vkCreateDescriptorPool(Vulkan::GetDevice(), &poolInfo, nullptr, &this->pool);
		if(result != VK_SUCCESS) {
            #if defined(DISPLAY_LOGS)
            std::cout << "DescriptorSet::PrepareDescriptor => vkCreateDescriptorPool : Failed" << std::endl;
            #endif
            return false;
        }

        ///////////////////////////////////
        // Allocation du Descriptor Sets //
        ///////////////////////////////////

        std::vector<VkDescriptorSetLayout> layouts(GlobalData::GetInstance()->instanced_buffer.GetInstanceCount(), this->layout);
        VkDescriptorSetAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.pNext = nullptr;
        alloc_info.descriptorPool = this->pool;
        alloc_info.descriptorSetCount = GlobalData::GetInstance()->instanced_buffer.GetInstanceCount();
        alloc_info.pSetLayouts = layouts.data();

        this->sets.resize(GlobalData::GetInstance()->instanced_buffer.GetInstanceCount());
        result = vkAllocateDescriptorSets(Vulkan::GetDevice(), &alloc_info, this->sets.data());
        if(result != VK_SUCCESS) {
            this->Clear();
            #if defined(DISPLAY_LOGS)
            std::cout <<"DescriptorSet::Create => vkAllocateDescriptorSets : Failed" << std::endl;
            #endif
            return false;
        }

        ///////////////////////////////////
        // Mise � jour du Descriptor Set //
        ///////////////////////////////////

        std::vector<VkWriteDescriptorSet> writes;
        std::vector<VkDescriptorBufferInfo> buffer_infos(bindings.size() * GlobalData::GetInstance()->instanced_buffer.GetInstanceCount());

        for(uint8_t i=0; i<bindings.size(); i++) {

            VkWriteDescriptorSet write;
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.pNext = nullptr;
            write.dstBinding = this->bindings[i].layout.binding;
            write.dstArrayElement = 0;
            write.descriptorCount = this->bindings[i].layout.descriptorCount;
            write.descriptorType = this->bindings[i].layout.descriptorType;
            write.pTexelBufferView = nullptr;
            write.pImageInfo = nullptr;

            for(uint32_t j=0; j<GlobalData::GetInstance()->instanced_buffer.GetInstanceCount(); j++) {
                buffer_infos[i * GlobalData::GetInstance()->instanced_buffer.GetInstanceCount() + j] = GlobalData::GetInstance()->instanced_buffer.GetBufferInfos(this->bindings[i].chunk, j);
                if(buffer_infos[i * GlobalData::GetInstance()->instanced_buffer.GetInstanceCount() + j].range > 0) {
                    write.dstSet = this->sets[j];
                    write.pBufferInfo = &buffer_infos[i * GlobalData::GetInstance()->instanced_buffer.GetInstanceCount() + j];
                    writes.push_back(write);
                }
            }
        }
        
        // Mise � jour
        vkUpdateDescriptorSets(Vulkan::GetDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

        #if defined(DISPLAY_LOGS)
        std::cout <<"DescriptorSet::Create : Success" << std::endl;
        #endif

        return true;
    }

    std::shared_ptr<Chunk> InstancedDescriptorSet::ReserveRange(size_t size, uint8_t binding)
    {
        std::shared_ptr<Chunk> chunk = this->bindings[binding].chunk->ReserveRange(size);

        if(chunk == nullptr) {

            VkDeviceSize binding_alignment = 0;
            switch(this->bindings[binding].layout.descriptorType) {
                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER :
                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC :
                    binding_alignment = Vulkan::UboAlignment();
                    break;

                case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER :
                case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC :
                    binding_alignment = Vulkan::SboAlignment();
                    break;
            }

            bool relocated;
            size_t old_offset = this->bindings[binding].chunk->offset;
            size_t old_size = this->bindings[binding].chunk->range;
            if(GlobalData::GetInstance()->instanced_buffer.GetChunk()->ResizeChild(this->bindings[binding].chunk,
                                                     this->bindings[binding].chunk->range + size,
                                                     relocated, binding_alignment)) {
                if(relocated) GlobalData::GetInstance()->instanced_buffer.MoveData(old_offset, this->bindings[binding].chunk->offset, old_size);
                chunk = this->bindings[binding].chunk->ReserveRange(size);
                std::fill(this->bindings[binding].need_update.begin(), this->bindings[binding].need_update.end(), true);
                for(auto listener : this->Listeners) listener->InstancedDescriptorSetUpdated(this, binding);
            }else{
                return nullptr;
            }
        }

        return chunk;
    }

    bool InstancedDescriptorSet::Update(uint8_t instance_id)
    {
        bool updated = false;
        for(auto& binding : this->bindings) {

            if(!binding.need_update[instance_id]) continue;

            VkDescriptorBufferInfo buffer_infos = GlobalData::GetInstance()->instanced_buffer.GetBufferInfos(binding.chunk, instance_id);

            VkWriteDescriptorSet write;
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.pNext = nullptr;
            write.dstBinding = binding.layout.binding;
            write.dstArrayElement = 0;
            write.descriptorCount = binding.layout.descriptorCount;
            write.descriptorType = binding.layout.descriptorType;
            write.pTexelBufferView = nullptr;
            write.pImageInfo = nullptr;
            write.dstSet = this->sets[instance_id];
            write.pBufferInfo = &buffer_infos;

            vkUpdateDescriptorSets(Vulkan::GetDevice(), 1, &write, 0, nullptr);
            binding.need_update[instance_id] = false;
            updated = true;
        }

        return updated;
    }
}