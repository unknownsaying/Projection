/*******************************************************************************
 * METAVERSE SPATIAL OPTIMIZATION
 * Advanced spatial partitioning and LOD systems for massive worlds
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <pthread.h>

// Spatial Partitioning Structures
typedef struct OctreeNode {
    float bounds[6];  // min_x, max_x, min_y, max_y, min_z, max_z
    struct OctreeNode* children[8];
    uint64_t* entity_ids;
    uint32_t entity_count;
    uint32_t entity_capacity;
    bool is_leaf;
    int depth;
} OctreeNode;

typedef struct QuadtreeNode {
    float bounds[4];  // min_x, max_x, min_y, max_y
    struct QuadtreeNode* children[4];
    uint64_t* entity_ids;
    uint32_t entity_count;
    uint32_t entity_capacity;
    bool is_leaf;
    int depth;
} QuadtreeNode;

typedef struct BIHNode {
    int axis;
    float split;
    struct BIHNode* left;
    struct BIHNode* right;
    uint64_t* entity_ids;
    uint32_t entity_count;
    uint32_t start_index;
    uint32_t end_index;
} BIHNode;

// LOD System
typedef struct LODLevel {
    uint32_t level;
    float distance_threshold;
    uint32_t vertex_count;
    uint32_t triangle_count;
    MeshData* mesh;
    TextureData* texture;
    bool loaded;
} LODLevel;

typedef struct LODObject {
    uint64_t object_id;
    Vector4 position;
    LODLevel* lod_levels;
    uint32_t lod_count;
    uint32_t current_lod;
    float last_distance;
    bool dynamic_lod;
} LODObject;

// Streaming System
typedef struct WorldChunk {
    int32_t x, y, z;
    uint8_t* data;
    size_t data_size;
    bool loaded;
    bool visible;
    bool dirty;
    LODObject* objects;
    uint32_t object_count;
    pthread_mutex_t chunk_mutex;
} WorldChunk;

typedef struct WorldStreamer {
    WorldChunk* chunks;
    uint32_t chunk_count;
    uint32_t chunk_capacity;
    
    // Streaming parameters
    int32_t view_distance;
    Vector4 viewer_position;
    uint32_t max_chunks_per_frame;
    
    // Threading
    pthread_t stream_thread;
    pthread_t load_thread;
    bool streaming_active;
    
    // Statistics
    uint32_t chunks_loaded;
    uint32_t chunks_unloaded;
    uint32_t memory_used;
} WorldStreamer;

// Function prototypes
OctreeNode* octree_create(float* bounds, int max_depth, int max_objects_per_node);
void octree_insert(OctreeNode* node, uint64_t entity_id, float* position, float radius);
void octree_remove(OctreeNode* node, uint64_t entity_id);
void octree_query_range(OctreeNode* node, float* center, float radius, 
                       uint64_t* results, uint32_t* result_count);
void octree_query_frustum(OctreeNode* node, float frustum[6][4],
                         uint64_t* results, uint32_t* result_count);
void octree_destroy(OctreeNode* node);

LODObject* lod_object_create(uint64_t object_id, Vector4 position, uint32_t lod_count);
void lod_object_update(LODObject* obj, Vector4 viewer_position);
void lod_object_set_mesh(LODObject* obj, uint32_t lod_level, MeshData* mesh);
void lod_object_destroy(LODObject* obj);

WorldStreamer* world_streamer_create(int32_t view_distance, uint32_t chunk_size);
void world_streamer_update(WorldStreamer* streamer, Vector4 viewer_position);
void world_streamer_load_chunk(WorldStreamer* streamer, int32_t x, int32_t y, int32_t z);
void world_streamer_unload_chunk(WorldStreamer* streamer, int32_t x, int32_t y, int32_t z);
bool world_streamer_is_chunk_visible(WorldStreamer* streamer, int32_t x, int32_t y, int32_t z);
void* world_streamer_thread(void* arg);

// Occlusion Culling System
typedef struct OcclusionBuffer {
    uint32_t width;
    uint32_t height;
    float* depth_buffer;
    bool* visibility_buffer;
    uint32_t* hierarchical_buffer;
    uint32_t hiz_levels;
} OcclusionBuffer;

OcclusionBuffer* occlusion_buffer_create(uint32_t width, uint32_t height);
void occlusion_buffer_clear(OcclusionBuffer* buffer);
bool occlusion_buffer_test_aabb(OcclusionBuffer* buffer, float aabb_min[3], float aabb_max[3]);
void occlusion_buffer_rasterize(OcclusionBuffer* buffer, float* vertices, uint32_t vertex_count);
void occlusion_buffer_update_hiz(OcclusionBuffer* buffer);
void occlusion_buffer_destroy(OcclusionBuffer* buffer);

// Octree implementation
OctreeNode* octree_create(float* bounds, int max_depth, int max_objects_per_node) {
    OctreeNode* node = malloc(sizeof(OctreeNode));
    
    memcpy(node->bounds, bounds, 6 * sizeof(float));
    
    for (int i = 0; i < 8; i++) {
        node->children[i] = NULL;
    }
    
    node->entity_capacity = max_objects_per_node;
    node->entity_ids = malloc(max_objects_per_node * sizeof(uint64_t));
    node->entity_count = 0;
    node->is_leaf = true;
    node->depth = 0;
    
    return node;
}

void octree_insert(OctreeNode* node, uint64_t entity_id, float* position, float radius) {
    // Check if entity fits in this node
    if (!aabb_contains_sphere(node->bounds, position, radius)) {
        return;  // Entity doesn't belong in this node
    }
    
    // If leaf node and has capacity, add entity
    if (node->is_leaf && node->entity_count < node->entity_capacity) {
        node->entity_ids[node->entity_count++] = entity_id;
        return;
    }
    
    // If leaf node but full, split if not at max depth
    if (node->is_leaf && node->depth < 8) {  // Max depth
        octree_split(node);
    }
    
    // If not leaf, insert into children
    if (!node->is_leaf) {
        for (int i = 0; i < 8; i++) {
            if (node->children[i]) {
                octree_insert(node->children[i], entity_id, position, radius);
            }
        }
    }
}

void octree_split(OctreeNode* node) {
    float mid_x = (node->bounds[0] + node->bounds[1]) * 0.5f;
    float mid_y = (node->bounds[2] + node->bounds[3]) * 0.5f;
    float mid_z = (node->bounds[4] + node->bounds[5]) * 0.5f;
    
    // Create 8 children
    for (int i = 0; i < 8; i++) {
        float child_bounds[6];
        
        // Determine child bounds based on octant
        child_bounds[0] = (i & 1) ? mid_x : node->bounds[0];  // min_x
        child_bounds[1] = (i & 1) ? node->bounds[1] : mid_x;  // max_x
        child_bounds[2] = (i & 2) ? mid_y : node->bounds[2];  // min_y
        child_bounds[3] = (i & 2) ? node->bounds[3] : mid_y;  // max_y
        child_bounds[4] = (i & 4) ? mid_z : node->bounds[4];  // min_z
        child_bounds[5] = (i & 4) ? node->bounds[5] : mid_z;  // max_z
        
        node->children[i] = octree_create(child_bounds, 8, node->entity_capacity);
        node->children[i]->depth = node->depth + 1;
    }
    
    // Redistribute entities to children
    for (uint32_t i = 0; i < node->entity_count; i++) {
        uint64_t entity_id = node->entity_ids[i];
        // In real implementation, would get entity position and radius
        // For demo, redistribute randomly
        int child_index = rand() % 8;
        node->children[child_index]->entity_ids[node->children[child_index]->entity_count++] = entity_id;
    }
    
    // Clear this node's entities
    node->entity_count = 0;
    node->is_leaf = false;
    
    // Free entity array (entities now in children)
    free(node->entity_ids);
    node->entity_ids = NULL;
}

void octree_query_range(OctreeNode* node, float* center, float radius,
                       uint64_t* results, uint32_t* result_count) {
    // Check if node intersects query sphere
    if (!aabb_intersects_sphere(node->bounds, center, radius)) {
        return;
    }
    
    // Add entities in this node
    for (uint32_t i = 0; i < node->entity_count; i++) {
        if (*result_count < 1024) {  // Safety limit
            results[*result_count] = node->entity_ids[i];
            (*result_count)++;
        }
    }
    
    // Query children
    if (!node->is_leaf) {
        for (int i = 0; i < 8; i++) {
            if (node->children[i]) {
                octree_query_range(node->children[i], center, radius, 
                                  results, result_count);
            }
        }
    }
}

// LOD Object implementation
LODObject* lod_object_create(uint64_t object_id, Vector4 position, uint32_t lod_count) {
    LODObject* obj = malloc(sizeof(LODObject));
    
    obj->object_id = object_id;
    obj->position = position;
    obj->lod_count = lod_count;
    obj->lod_levels = malloc(lod_count * sizeof(LODLevel));
    obj->current_lod = 0;
    obj->last_distance = 0.0f;
    obj->dynamic_lod = true;
    
    // Initialize LOD levels
    for (uint32_t i = 0; i < lod_count; i++) {
        obj->lod_levels[i].level = i;
        obj->lod_levels[i].distance_threshold = powf(2.0f, i) * 10.0f;
        obj->lod_levels[i].vertex_count = 0;
        obj->lod_levels[i].triangle_count = 0;
        obj->lod_levels[i].mesh = NULL;
        obj->lod_levels[i].texture = NULL;
        obj->lod_levels[i].loaded = false;
    }
    
    return obj;
}

void lod_object_update(LODObject* obj, Vector4 viewer_position) {
    if (!obj->dynamic_lod) return;
    
    // Calculate distance to viewer
    float dx = obj->position.x - viewer_position.x;
    float dy = obj->position.y - viewer_position.y;
    float dz = obj->position.z - viewer_position.z;
    float distance = sqrtf(dx*dx + dy*dy + dz*dz);
    
    obj->last_distance = distance;
    
    // Determine appropriate LOD level
    uint32_t new_lod = 0;
    for (uint32_t i = 0; i < obj->lod_count; i++) {
        if (distance <= obj->lod_levels[i].distance_threshold) {
            new_lod = i;
            break;
        }
    }
    
    // If we passed the last threshold, use lowest LOD
    if (distance > obj->lod_levels[obj->lod_count - 1].distance_threshold) {
        new_lod = obj->lod_count - 1;
    }
    
    // Update LOD if changed
    if (new_lod != obj->current_lod) {
        // Unload old LOD if needed
        if (obj->current_lod > new_lod) {
            // Moving to lower detail, can unload higher LODs
            for (uint32_t i = new_lod + 1; i <= obj->current_lod; i++) {
                if (obj->lod_levels[i].loaded) {
                    // In real implementation, unload mesh/texture from GPU
                    obj->lod_levels[i].loaded = false;
                }
            }
        }
        
        // Load new LOD if needed
        if (!obj->lod_levels[new_lod].loaded) {
            // In real implementation, load mesh/texture to GPU
            obj->lod_levels[new_lod].loaded = true;
        }
        
        obj->current_lod = new_lod;
    }
}

// World Streaming implementation
WorldStreamer* world_streamer_create(int32_t view_distance, uint32_t chunk_size) {
    WorldStreamer* streamer = malloc(sizeof(WorldStreamer));
    
    streamer->view_distance = view_distance;
    streamer->viewer_position = (Vector4){0, 0, 0, 0};
    streamer->max_chunks_per_frame = 4;
    
    // Calculate chunk capacity based on view distance
    int diameter = view_distance * 2 + 1;
    streamer->chunk_capacity = diameter * diameter * diameter;
    streamer->chunk_count = 0;
    streamer->chunks = malloc(streamer->chunk_capacity * sizeof(WorldChunk));
    
    // Initialize chunks
    for (uint32_t i = 0; i < streamer->chunk_capacity; i++) {
        streamer->chunks[i].loaded = false;
        streamer->chunks[i].visible = false;
        streamer->chunks[i].dirty = false;
        streamer->chunks[i].objects = NULL;
        streamer->chunks[i].object_count = 0;
        pthread_mutex_init(&streamer->chunks[i].chunk_mutex, NULL);
    }
    
    streamer->streaming_active = false;
    streamer->chunks_loaded = 0;
    streamer->chunks_unloaded = 0;
    streamer->memory_used = 0;
    
    return streamer;
}

void world_streamer_update(WorldStreamer* streamer, Vector4 viewer_position) {
    streamer->viewer_position = viewer_position;
    
    // Determine which chunks should be loaded
    int32_t viewer_chunk_x = (int32_t)(viewer_position.x / 16.0f);
    int32_t viewer_chunk_y = (int32_t)(viewer_position.y / 16.0f);
    int32_t viewer_chunk_z = (int32_t)(viewer_position.z / 16.0f);
    
    // Mark chunks for loading/unloading
    int chunks_to_load = 0;
    int chunks_to_unload = 0;
    
    for (int32_t dx = -streamer->view_distance; dx <= streamer->view_distance; dx++) {
        for (int32_t dy = -streamer->view_distance; dy <= streamer->view_distance; dy++) {
            for (int32_t dz = -streamer->view_distance; dz <= streamer->view_distance; dz++) {
                int32_t chunk_x = viewer_chunk_x + dx;
                int32_t chunk_y = viewer_chunk_y + dy;
                int32_t chunk_z = viewer_chunk_z + dz;
                
                // Check if chunk should be visible
                bool should_be_visible = world_streamer_is_chunk_visible(streamer, 
                                                                        chunk_x, chunk_y, chunk_z);
                
                // Find or create chunk
                WorldChunk* chunk = NULL;
                for (uint32_t i = 0; i < streamer->chunk_count; i++) {
                    if (streamer->chunks[i].x == chunk_x &&
                        streamer->chunks[i].y == chunk_y &&
                        streamer->chunks[i].z == chunk_z) {
                        chunk = &streamer->chunks[i];
                        break;
                    }
                }
                
                if (should_be_visible) {
                    if (!chunk) {
                        // Need to load this chunk
                        if (chunks_to_load < streamer->max_chunks_per_frame) {
                            world_streamer_load_chunk(streamer, chunk_x, chunk_y, chunk_z);
                            chunks_to_load++;
                        }
                    } else if (!chunk->visible) {
                        chunk->visible = true;
                    }
                } else {
                    if (chunk && chunk->visible) {
                        chunk->visible = false;
                        
                        // Unload if far enough
                        float dist_x = abs(chunk_x - viewer_chunk_x);
                        float dist_y = abs(chunk_y - viewer_chunk_y);
                        float dist_z = abs(chunk_z - viewer_chunk_z);
                        
                        if (dist_x > streamer->view_distance + 2 ||
                            dist_y > streamer->view_distance + 2 ||
                            dist_z > streamer->view_distance + 2) {
                            
                            if (chunks_to_unload < streamer->max_chunks_per_frame) {
                                world_streamer_unload_chunk(streamer, chunk_x, chunk_y, chunk_z);
                                chunks_to_unload++;
                            }
                        }
                    }
                }
            }
        }
    }
}

bool world_streamer_is_chunk_visible(WorldStreamer* streamer, 
                                     int32_t x, int32_t y, int32_t z) {
    // Simple spherical visibility check
    int32_t viewer_chunk_x = (int32_t)(streamer->viewer_position.x / 16.0f);
    int32_t viewer_chunk_y = (int32_t)(streamer->viewer_position.y / 16.0f);
    int32_t viewer_chunk_z = (int32_t)(streamer->viewer_position.z / 16.0f);
    
    float dx = (float)(x - viewer_chunk_x);
    float dy = (float)(y - viewer_chunk_y);
    float dz = (float)(z - viewer_chunk_z);
    
    float distance_squared = dx*dx + dy*dy + dz*dz;
    float view_distance_squared = streamer->view_distance * streamer->view_distance;
    
    return distance_squared <= view_distance_squared;
}

// Occlusion Buffer implementation
OcclusionBuffer* occlusion_buffer_create(uint32_t width, uint32_t height) {
    OcclusionBuffer* buffer = malloc(sizeof(OcclusionBuffer));
    
    buffer->width = width;
    buffer->height = height;
    
    // Create depth buffer
    buffer->depth_buffer = malloc(width * height * sizeof(float));
    
    // Create visibility buffer
    buffer->visibility_buffer = malloc(width * height * sizeof(bool));
    
    // Create hierarchical Z-buffer
    buffer->hiz_levels = (uint32_t)log2f((float)min(width, height)) + 1;
    buffer->hierarchical_buffer = malloc(buffer->hiz_levels * 
                                       (width * height / 4) * sizeof(uint32_t));
    
    return buffer;
}

bool occlusion_buffer_test_aabb(OcclusionBuffer* buffer, 
                               float aabb_min[3], float aabb_max[3]) {
    // Project AABB to screen space
    float screen_min[2], screen_max[2];
    float depth_min, depth_max;
    
    // Simplified projection - in reality would use view/projection matrices
    screen_min[0] = aabb_min[0];
    screen_min[1] = aabb_min[1];
    screen_max[0] = aabb_max[0];
    screen_max[1] = aabb_max[1];
    depth_min = aabb_min[2];
    depth_max = aabb_max[2];
    
    // Convert to pixel coordinates
    uint32_t px_min = (uint32_t)((screen_min[0] + 1.0f) * 0.5f * buffer->width);
    uint32_t py_min = (uint32_t)((screen_min[1] + 1.0f) * 0.5f * buffer->height);
    uint32_t px_max = (uint32_t)((screen_max[0] + 1.0f) * 0.5f * buffer->width);
    uint32_t py_max = (uint32_t)((screen_max[1] + 1.0f) * 0.5f * buffer->height);
    
    // Clamp to buffer bounds
    px_min = max(0, min(px_min, buffer->width - 1));
    py_min = max(0, min(py_min, buffer->height - 1));
    px_max = max(0, min(px_max, buffer->width - 1));
    py_max = max(0, min(py_max, buffer->height - 1));
    
    // Check hierarchical Z-buffer
    uint32_t level = 0;
    uint32_t step = 1;
    
    while (level < buffer->hiz_levels) {
        uint32_t lx_min = px_min / step;
        uint32_t ly_min = py_min / step;
        uint32_t lx_max = px_max / step;
        uint32_t ly_max = py_max / step;
        
        // Check if any tile in the region is visible
        bool visible = false;
        for (uint32_t y = ly_min; y <= ly_max; y++) {
            for (uint32_t x = lx_min; x <= lx_max; x++) {
                uint32_t idx = y * (buffer->width / step) + x;
                float hiz_depth = *(float*)&buffer->hierarchical_buffer[level * (buffer->width * buffer->height / (step * step)) + idx];
                
                if (depth_min < hiz_depth) {
                    visible = true;
                    break;
                }
            }
            if (visible) break;
        }
        
        if (!visible) {
            return false;  // Occluded
        }
        
        // Move to next finer level
        level++;
        step *= 2;
    }
    
    return true;  // Potentially visible
}

void occlusion_buffer_update_hiz(OcclusionBuffer* buffer) {
    // Build hierarchical Z-buffer from depth buffer
    uint32_t width = buffer->width;
    uint32_t height = buffer->height;
    
    // Level 0 is the original depth buffer
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t idx = y * width + x;
            buffer->hierarchical_buffer[idx] = 
                *(uint32_t*)&buffer->depth_buffer[idx];
        }
    }
    
    // Build coarser levels
    uint32_t src_width = width;
    uint32_t src_height = height;
    uint32_t dst_offset = width * height;
    
    for (uint32_t level = 1; level < buffer->hiz_levels; level++) {
        uint32_t dst_width = src_width / 2;
        uint32_t dst_height = src_height / 2;
        
        if (dst_width == 0 || dst_height == 0) break;
        
        uint32_t src_offset = dst_offset - (src_width * src_height);
        
        for (uint32_t y = 0; y < dst_height; y++) {
            for (uint32_t x = 0; x < dst_width; x++) {
                // Take min of 2x2 block
                uint32_t src_idx00 = src_offset + (y*2) * src_width + (x*2);
                uint32_t src_idx01 = src_offset + (y*2) * src_width + (x*2 + 1);
                uint32_t src_idx10 = src_offset + (y*2 + 1) * src_width + (x*2);
                uint32_t src_idx11 = src_offset + (y*2 + 1) * src_width + (x*2 + 1);
                
                float depth00 = *(float*)&buffer->hierarchical_buffer[src_idx00];
                float depth01 = *(float*)&buffer->hierarchical_buffer[src_idx01];
                float depth10 = *(float*)&buffer->hierarchical_buffer[src_idx10];
                float depth11 = *(float*)&buffer->hierarchical_buffer[src_idx11];
                
                float min_depth = fminf(fminf(depth00, depth01), 
                                       fminf(depth10, depth11));
                
                uint32_t dst_idx = dst_offset + y * dst_width + x;
                buffer->hierarchical_buffer[dst_idx] = *(uint32_t*)&min_depth;
            }
        }
        
        src_width = dst_width;
        src_height = dst_height;
        dst_offset += dst_width * dst_height;
    }
}

// Utility functions
bool aabb_contains_sphere(float* aabb, float* center, float radius) {
    float sphere_min[3] = {center[0] - radius, center[1] - radius, center[2] - radius};
    float sphere_max[3] = {center[0] + radius, center[1] + radius, center[2] + radius};
    
    return (sphere_min[0] >= aabb[0] && sphere_max[0] <= aabb[1] &&
            sphere_min[1] >= aabb[2] && sphere_max[1] <= aabb[3] &&
            sphere_min[2] >= aabb[4] && sphere_max[2] <= aabb[5]);
}

bool aabb_intersects_sphere(float* aabb, float* center, float radius) {
    float closest_x = fmaxf(aabb[0], fminf(center[0], aabb[1]));
    float closest_y = fmaxf(aabb[2], fminf(center[1], aabb[3]));
    float closest_z = fmaxf(aabb[4], fminf(center[2], aabb[5]));
    
    float dx = center[0] - closest_x;
    float dy = center[1] - closest_y;
    float dz = center[2] - closest_z;
    
    float distance_squared = dx*dx + dy*dy + dz*dz;
    return distance_squared <= radius * radius;
}

uint32_t min(uint32_t a, uint32_t b) { return a < b ? a : b; }
uint32_t max(uint32_t a, uint32_t b) { return a > b ? a : b; }

int main_spatial_test() {
    printf("Metaverse Spatial Optimization System\n");
    
    // Test octree
    float world_bounds[6] = {-1000, 1000, -1000, 1000, -1000, 1000};
    OctreeNode* octree = octree_create(world_bounds, 8, 32);
    
    printf("Octree created\n");
    
    // Test LOD system
    LODObject* lod_obj = lod_object_create(1, (Vector4){10, 0, 10, 0}, 4);
    printf("LOD object created with %d levels\n", lod_obj->lod_count);
    
    // Test world streaming
    WorldStreamer* streamer = world_streamer_create(4, 16);
    printf("World streamer created with view distance %d\n", streamer->view_distance);
    
    // Test occlusion buffer
    OcclusionBuffer* occlusion = occlusion_buffer_create(1920, 1080);
    printf("Occlusion buffer created: %dx%d\n", occlusion->width, occlusion->height);
    
    // Cleanup
    free(lod_obj->lod_levels);
    free(lod_obj);
    free(streamer->chunks);
    free(streamer);
    occlusion_buffer_destroy(occlusion);
    
    printf("Spatial optimization tests completed\n");
    return 0;
}