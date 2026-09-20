#include "helpers.h"
#include "../core/log.h"
#include "internal.h"

bool engine_upload_to_texture(Engine* self, SDL_GPUTexture* texture, u32 width, u32 height, void* data, size_t size) {
    void* mapped_memory;
    if (!SDL_CHECK(mapped_memory = SDL_MapGPUTransferBuffer(self->device, self->transfer_buffer, false))) {
        return false;
    }
    memcpy(mapped_memory, data, size);
    SDL_UnmapGPUTransferBuffer(self->device, self->transfer_buffer);

    SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(self->device);

    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(command_buffer);
    SDL_UploadToGPUTexture(
        copy_pass,
        &(SDL_GPUTextureTransferInfo){
            .transfer_buffer = self->transfer_buffer,
        },
        &(SDL_GPUTextureRegion){
            .texture = texture,
            .w = width,
            .h = height,
            .d = 1
        },
        false
    );
    SDL_EndGPUCopyPass(copy_pass);

    if (!SDL_CHECK(SDL_SubmitGPUCommandBuffer(command_buffer))) {
        return false;
    }
    return true;
}

bool engine_upload_to_buffer(Engine* self, SDL_GPUBuffer* buffer, void* data, size_t size) {
    void* mapped_memory;
    if (!SDL_CHECK(mapped_memory = SDL_MapGPUTransferBuffer(self->device, self->transfer_buffer, false))) {
        return false;
    }
    memcpy(mapped_memory, data, size);
    SDL_UnmapGPUTransferBuffer(self->device, self->transfer_buffer);

    SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(self->device);
    if (!command_buffer) {
        return false;
    }

    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(command_buffer);
    SDL_UploadToGPUBuffer(
        copy_pass,
        &(SDL_GPUTransferBufferLocation){
            .transfer_buffer = self->transfer_buffer,
            .offset = 0,
        },
        &(SDL_GPUBufferRegion){
            .buffer = buffer,
            .size = (u32)size,
        },
        false
    );
    SDL_EndGPUCopyPass(copy_pass);

    if (!SDL_CHECK(SDL_SubmitGPUCommandBuffer(command_buffer))) {
        return false;
    }
    return true;
}

