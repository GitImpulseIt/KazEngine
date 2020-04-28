#include "ManagedBuffer.h"

namespace Engine
{
    void ManagedBuffer::Clear()
    {
        if(Vulkan::HasInstance()) {
            if(this->buffer.memory != VK_NULL_HANDLE) vkFreeMemory(Vulkan::GetDevice(), this->buffer.memory, nullptr);
            if(this->buffer.handle != VK_NULL_HANDLE) vkDestroyBuffer(Vulkan::GetDevice(), this->buffer.handle, nullptr);
        }

        this->sub_buffer.clear();
        this->free_chunks.clear();
        this->buffer = {};
        this->need_flush = false;
        this->flush_range_start = 0;
        this->flush_range_end = 0;
        this->chunck_alignment = 0;
    }

    bool ManagedBuffer::Create(Vulkan::STAGING_BUFFER staging_buffer, VkDeviceSize size, VkBufferUsageFlags usage,
                               VkFlags requirement, std::vector<uint32_t> const& queue_families)
    {
        if(!Vulkan::GetInstance().CreateDataBuffer(this->buffer, size, usage, requirement, queue_families)) {
            this->Clear();
            return false;
        }

        this->staging_buffer = staging_buffer;

        return true;
    }

    inline void ManagedBuffer::UpdateFlushRange(size_t start_offset, size_t data_size)
    {
        size_t end_offset = start_offset + data_size;
        this->need_flush = true;
        if(start_offset < this->flush_range_start) this->flush_range_start = start_offset;
        if(end_offset > this->flush_range_end) this->flush_range_end = end_offset;
    }

    void ManagedBuffer::WriteData(const void* data, VkDeviceSize data_size, VkDeviceSize global_offset)
    {
        std::memcpy(this->staging_buffer.pointer + global_offset, data, data_size);
        this->UpdateFlushRange(global_offset, data_size);
    }

    void ManagedBuffer::WriteData(const void* data, VkDeviceSize data_size, VkDeviceSize relative_offset, uint8_t sub_buffer_id)
    {
        size_t start_offset = relative_offset + this->sub_buffer[sub_buffer_id].offset;
        std::memcpy(this->staging_buffer.pointer + start_offset, data, data_size);
        this->UpdateFlushRange(start_offset, data_size);
    }

    bool ManagedBuffer::Flush(Vulkan::COMMAND_BUFFER const& command_buffer)
    {
        if(this->need_flush) {
            size_t bytes_sent = Vulkan::GetInstance().SendToBuffer(this->buffer, command_buffer, this->staging_buffer,
                                                                   this->flush_range_end - this->flush_range_start, this->flush_range_start);

            if(!bytes_sent) {
                this->need_flush = false;
                this->flush_range_start = 0;
                this->flush_range_end = 0;
                return false;
            }
        }

        this->need_flush = false;
        this->flush_range_start = 0;
        this->flush_range_end = 0;
        return true;
    }

    bool ManagedBuffer::ReserveChunk(VkDeviceSize& offset, size_t size)
    {
        if(!this->free_chunks.size()) return false;

        VkDeviceSize claimed_range = size;
        if(this->chunck_alignment > 0) claimed_range = static_cast<uint32_t>((size + this->chunck_alignment - 1) & ~(this->chunck_alignment - 1));

        for(auto chunk = this->free_chunks.begin(); chunk<this->free_chunks.end(); chunk++) {
            if(chunk->range > claimed_range) {
                offset = chunk->offset;
                chunk->offset += static_cast<uint32_t>(claimed_range);
                chunk->range -= claimed_range;
                return true;
            }else if(chunk->range == claimed_range) {
                offset = chunk->offset;
                this->free_chunks.erase(chunk);
                return true;
            }
        }

        return false;
    }

    bool ManagedBuffer::ReserveChunk(uint32_t& offset, size_t size, uint8_t sub_buffer_id)
    {
        if(!this->free_chunks.size()) return false;

        VkDeviceSize claimed_range = size;
        if(this->chunck_alignment > 0) claimed_range = static_cast<uint32_t>((size + this->chunck_alignment - 1) & ~(this->chunck_alignment - 1));

        for(auto chunk = this->free_chunks.begin(); chunk<this->free_chunks.end(); chunk++) {

            VkDeviceSize available_range;
            if(chunk->offset + chunk->range < this->sub_buffer[sub_buffer_id].offset
            || chunk->offset > this->sub_buffer[sub_buffer_id].offset + this->sub_buffer[sub_buffer_id].range) {
                // Le chunk ne se trouve pas dans la zone du sub-buffer, on passe au suivant
                continue;
            }else{

                // Taille du segment disponible � l'allocation
                available_range = chunk->range;

                // Le chunk d�marre avant le sub-buffer
                if(chunk->offset < this->sub_buffer[sub_buffer_id].offset)
                    available_range -= this->sub_buffer[sub_buffer_id].offset - chunk->offset;

                // Le chunk se termine apr�s le sub-buffer
                if(chunk->offset + chunk->range > this->sub_buffer[sub_buffer_id].offset + this->sub_buffer[sub_buffer_id].range)
                    available_range -= (chunk->offset + chunk->range) - (this->sub_buffer[sub_buffer_id].offset + this->sub_buffer[sub_buffer_id].range);
            }

            if(available_range >= claimed_range) {
                
                bool split_chunk = false;
                CHUNK new_chunk;

                // Le chunk d�marre avant le sub-buffer
                if(chunk->offset < this->sub_buffer[sub_buffer_id].offset) {
                    new_chunk.offset = chunk->offset;
                    new_chunk.range = this->sub_buffer[sub_buffer_id].offset - chunk->offset;
                    chunk->offset = this->sub_buffer[sub_buffer_id].offset;
                    chunk->range -= new_chunk.range;
                    split_chunk = true;
                }

                offset = static_cast<uint32_t>(chunk->offset - this->sub_buffer[sub_buffer_id].offset);
                if(chunk->range == claimed_range) {

                    // Le chunk est occup� en totalit�, il est d�truit
                    this->free_chunks.erase(chunk);
                }else{

                    // Le chunk est r�duit en taille
                    chunk->offset += static_cast<uint32_t>(claimed_range);
                    chunk->range -= claimed_range;
                }

                // Le chunk a �t� s�par� en deux
                if(split_chunk) this->free_chunks.push_back(new_chunk);

                // Le chunk est r�serv�
                return true;
            }
        }

        return false;
    }
}