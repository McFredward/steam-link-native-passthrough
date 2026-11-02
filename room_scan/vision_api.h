// Private versioned ABI shared by the owned camera consumer and room controller.
#pragma once
#include <stddef.h>
#include <stdint.h>
#if defined(__GNUC__)
#define SFNP_VISION_EXPORT __attribute__((visibility("default")))
#else
#define SFNP_VISION_EXPORT
#endif
#ifdef __cplusplus
extern "C" {
#endif
SFNP_VISION_EXPORT uint32_t sfnp_room_vision_abi(void);
SFNP_VISION_EXPORT const char* sfnp_room_vision_error(void);
SFNP_VISION_EXPORT void* sfnp_stereo_create(void);
SFNP_VISION_EXPORT void sfnp_stereo_destroy(void* context);
// Exactly 320x240 rectified luma/support inputs and 160x120 metric outputs.
SFNP_VISION_EXPORT int sfnp_stereo_compute(void* context,const uint8_t* left,const uint8_t* right,
        const uint8_t* left_support,const uint8_t* right_support,float focal,float baseline,
        float* depth,float* confidence);
SFNP_VISION_EXPORT void* sfnp_fusion_create(float voxel,uint32_t max_blocks,uint32_t max_vertices,uint32_t max_triangles);
SFNP_VISION_EXPORT void sfnp_fusion_destroy(void* context);
// The pose is a row-major rigid 3x4 room-from-camera transform in XR axes.
SFNP_VISION_EXPORT int sfnp_fusion_integrate(void* context,uint32_t width,uint32_t height,
        double fx,double fy,double cx,double cy,const float* depth,const float* confidence,
        const double* room_from_camera,uint64_t sequence);
SFNP_VISION_EXPORT int sfnp_fusion_rebind(void* context);
SFNP_VISION_EXPORT int sfnp_fusion_mesh_budget(void* context,uint32_t vertices,uint32_t triangles);
SFNP_VISION_EXPORT int sfnp_fusion_mesh_size(void* context,uint32_t* vertices,uint32_t* triangles);
SFNP_VISION_EXPORT int sfnp_fusion_copy_mesh(void* context,float* vertices,uint32_t vertex_capacity,
        uint32_t* triangles,uint32_t triangle_capacity,uint8_t* quality);
SFNP_VISION_EXPORT uint32_t sfnp_fusion_block_count(void* context);
#ifdef __cplusplus
}
#endif
