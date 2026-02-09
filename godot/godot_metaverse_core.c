/*******************************************************************************
 * GODOT METAVERSE CORE AMPLIFICATION
 * Direct C-level enhancements to Godot Engine for Metaverse applications
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

// Godot Engine interface structures
typedef struct {
    void* (*godot_alloc)(size_t size);
    void (*godot_free)(void* ptr);
    void (*godot_print)(const char* message);
    void (*godot_error)(const char* message);
    double (*godot_get_time)();
} GodotAPI;

// Metaverse spatial data structures
typedef struct {
    float x, y, z;
    float w;  // For quaternions
} Vector4;

typedef struct {
    Vector4 position;
    Vector4 rotation;
    Vector4 scale;
    uint64_t entity_id;
    uint8_t entity_type;
    uint32_t flags;
} MetaverseEntity;

typedef struct {
    float* vertex_data;
    float* normal_data;
    float* uv_data;
    uint32_t vertex_count;
    uint32_t triangle_count;
    uint32_t lod_level;
    bool dynamic;
    bool compressed;
} MeshData;

typedef struct {
    uint8_t* texture_data;
    int width;
    int height;
    int channels;
    GLuint gl_texture_id;
    bool mipmapped;
    bool compressed;
} TextureData;

// Spatial audio system
typedef struct {
    float position[3];
    float velocity[3];
    float orientation[4];
    float volume;
    float pitch;
    bool spatialized;
    bool looping;
    uint32_t source_id;
} AudioEmitter;

// Godot Metaverse Amplifier
typedef struct {
    GodotAPI godot;
    
    // Metaverse world state
    MetaverseEntity* entities;
    uint32_t entity_count;
    uint32_t entity_capacity;
    
    // Rendering enhancements
    MeshData* mesh_cache;
    TextureData* texture_cache;
    uint32_t cache_size;
    
    // Spatial audio
    AudioEmitter* audio_emitters;
    uint32_t emitter_count;
    
    // Networking
    pthread_t net_thread;
    bool network_active;
    uint32_t player_count;
    
    // Performance metrics
    double frame_time;
    double physics_time;
    double render_time;
    uint32_t fps;
    uint32_t draw_calls;
    
    // Synchronization
    pthread_mutex_t entity_mutex;
    pthread_mutex_t render_mutex;
    pthread_rwlock_t world_lock;
} MetaverseAmplifier;

// Function prototypes
MetaverseAmplifier* metaverse_amplifier_create(GodotAPI* api);
void metaverse_amplifier_init(MetaverseAmplifier* amp);
void metaverse_amplifier_destroy(MetaverseAmplifier* amp);
void metaverse_update_world(MetaverseAmplifier* amp, double delta_time);
void metaverse_render_enhanced(MetaverseAmplifier* amp);
void metaverse_process_input(MetaverseAmplifier* amp, float* input_state);
void metaverse_network_update(MetaverseAmplifier* amp);
void metaverse_spatial_audio_update(MetaverseAmplifier* amp);
void metaverse_physics_optimized(MetaverseAmplifier* amp, double delta_time);
MeshData* metaverse_mesh_optimize(MeshData* mesh, int target_vertices);
TextureData* metaverse_texture_compress(TextureData* texture, int quality);
void metaverse_entity_add(MetaverseAmplifier* amp, MetaverseEntity* entity);
void metaverse_entity_remove(MetaverseAmplifier* amp, uint64_t entity_id);
void metaverse_entity_update(MetaverseAmplifier* amp, MetaverseEntity* entity);

// Core amplifier creation
MetaverseAmplifier* metaverse_amplifier_create(GodotAPI* api) {
    MetaverseAmplifier* amp = malloc(sizeof(MetaverseAmplifier));
    if (!amp) return NULL;
    
    memset(amp, 0, sizeof(MetaverseAmplifier));
    amp->godot = *api;
    
    // Initialize entity storage
    amp->entity_capacity = 1024;
    amp->entities = malloc(sizeof(MetaverseEntity) * amp->entity_capacity);
    amp->entity_count = 0;
    
    // Initialize mesh/texture cache
    amp->cache_size = 128;
    amp->mesh_cache = malloc(sizeof(MeshData) * amp->cache_size);
    amp->texture_cache = malloc(sizeof(TextureData) * amp->cache_size);
    
    // Initialize audio emitters
    amp->emitter_count = 0;
    amp->audio_emitters = malloc(sizeof(AudioEmitter) * 64);
    
    // Initialize synchronization primitives
    pthread_mutex_init(&amp->entity_mutex, NULL);
    pthread_mutex_init(&amp->render_mutex, NULL);
    pthread_rwlock_init(&amp->world_lock, NULL);
    
    // Initialize network
    amp->network_active = false;
    amp->player_count = 1;
    
    // Performance metrics
    amp->frame_time = 0.016;  // 60 FPS target
    amp->physics_time = 0.0;
    amp->render_time = 0.0;
    amp->fps = 60;
    amp->draw_calls = 0;
    
    amp->godot.godot_print("Metaverse Amplifier created");
    return amp;
}

// Initialize amplifier subsystems
void metaverse_amplifier_init(MetaverseAmplifier* amp) {
    amp->godot.godot_print("Initializing Metaverse Amplifier subsystems...");
    
    // Initialize OpenGL extensions for enhanced rendering
    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        amp->godot.godot_error("Failed to initialize GLEW");
        return;
    }
    
    // Check for required OpenGL extensions
    if (!GLEW_ARB_buffer_storage) {
        amp->godot.godot_error("ARB_buffer_storage not supported");
    }
    
    if (!GLEW_ARB_multi_draw_indirect) {
        amp->godot.godot_error("ARB_multi_draw_indirect not supported");
    }
    
    if (!GLEW_ARB_bindless_texture) {
        amp->godot.godot_print("Note: ARB_bindless_texture not available");
    }
    
    // Setup enhanced rendering features
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Setup framebuffer for post-processing
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    
    amp->godot.godot_print("Metaverse Amplifier initialized successfully");
}

// World update with spatial partitioning
void metaverse_update_world(MetaverseAmplifier* amp, double delta_time) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    // Acquire read lock for world updates
    pthread_rwlock_rdlock(&amp->world_lock);
    
    // Update entity positions with SIMD-like optimization
    for (uint32_t i = 0; i < amp->entity_count; i++) {
        MetaverseEntity* entity = &amp->entities[i];
        
        // Simple physics simulation
        // In reality, this would use a proper physics engine
        
        // Update based on velocity (if present in flags)
        if (entity->flags & 0x01) {  // HAS_VELOCITY flag
            // Update position
            entity->position.x += 0.1f * delta_time;
            entity->position.y += 0.05f * delta_time;
        }
        
        // Apply gravity if needed
        if (entity->flags & 0x02) {  // HAS_GRAVITY flag
            entity->position.y -= 9.8f * delta_time * delta_time;
            
            // Simple ground collision
            if (entity->position.y < 0.0f) {
                entity->position.y = 0.0f;
            }
        }
    }
    
    pthread_rwlock_unlock(&amp->world_lock);
    
    // Update spatial audio
    metaverse_spatial_audio_update(amp);
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) + 
                    (end.tv_nsec - start.tv_nsec) / 1e9;
    amp->physics_time = 0.9 * amp->physics_time + 0.1 * elapsed;
}

// Enhanced rendering with batch optimization
void metaverse_render_enhanced(MetaverseAmplifier* amp) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    pthread_mutex_lock(&amp->render_mutex);
    
    // Clear buffers
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // Frustum culling preparation
    float frustum[6][4];
    calculate_frustum(frustum);
    
    uint32_t visible_count = 0;
    uint32_t draw_calls = 0;
    
    // Batch entities by material/shader
    EntityBatch batches[32];
    uint32_t batch_count = 0;
    
    pthread_rwlock_rdlock(&amp->world_lock);
    
    for (uint32_t i = 0; i < amp->entity_count; i++) {
        MetaverseEntity* entity = &amp->entities[i];
        
        // Frustum culling
        if (!is_in_frustum(entity->position, frustum)) {
            continue;
        }
        
        visible_count++;
        
        // Find or create batch for this entity type
        bool found_batch = false;
        for (uint32_t b = 0; b < batch_count; b++) {
            if (batches[b].entity_type == entity->entity_type) {
                batches[b].entities[batches[b].count++] = entity;
                found_batch = true;
                break;
            }
        }
        
        if (!found_batch && batch_count < 32) {
            batches[batch_count].entity_type = entity->entity_type;
            batches[batch_count].count = 1;
            batches[batch_count].entities[0] = entity;
            batch_count++;
        }
    }
    
    // Render batches
    for (uint32_t b = 0; b < batch_count; b++) {
        EntityBatch* batch = &batches[b];
        
        // Setup shader for this batch
        setup_shader_for_type(batch->entity_type);
        
        // Prepare instanced rendering data
        float* instance_data = prepare_instance_data(batch);
        
        // Single draw call for entire batch
        glDrawArraysInstanced(GL_TRIANGLES, 0, 
                             get_vertex_count_for_type(batch->entity_type),
                             batch->count);
        
        draw_calls++;
        
        free(instance_data);
    }
    
    pthread_rwlock_unlock(&amp->world_lock);
    
    // Post-processing effects
    if (amp->frame_time < 0.025) {  // Only if we have time
        apply_post_processing();
    }
    
    pthread_mutex_unlock(&amp->render_mutex);
    
    // Update performance metrics
    amp->draw_calls = draw_calls;
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) + 
                    (end.tv_nsec - start.tv_nsec) / 1e9;
    amp->render_time = 0.9 * amp->render_time + 0.1 * elapsed;
    amp->fps = (uint32_t)(1.0 / amp->frame_time);
}

// Mesh optimization with simplification
MeshData* metaverse_mesh_optimize(MeshData* mesh, int target_vertices) {
    if (!mesh || mesh->vertex_count <= target_vertices) {
        return mesh;
    }
    
    MeshData* optimized = malloc(sizeof(MeshData));
    optimized->vertex_count = target_vertices;
    optimized->triangle_count = target_vertices / 3;
    optimized->lod_level = mesh->lod_level + 1;
    optimized->dynamic = mesh->dynamic;
    optimized->compressed = true;
    
    // Allocate memory for optimized mesh
    size_t vertex_data_size = target_vertices * 3 * sizeof(float);
    size_t normal_data_size = target_vertices * 3 * sizeof(float);
    size_t uv_data_size = target_vertices * 2 * sizeof(float);
    
    optimized->vertex_data = malloc(vertex_data_size);
    optimized->normal_data = malloc(normal_data_size);
    optimized->uv_data = malloc(uv_data_size);
    
    // Simple mesh simplification (in reality, use QEM or similar)
    float reduction_ratio = (float)target_vertices / mesh->vertex_count;
    
    for (int i = 0; i < target_vertices; i++) {
        int src_idx = (int)(i / reduction_ratio);
        
        // Copy vertex
        optimized->vertex_data[i * 3] = mesh->vertex_data[src_idx * 3];
        optimized->vertex_data[i * 3 + 1] = mesh->vertex_data[src_idx * 3 + 1];
        optimized->vertex_data[i * 3 + 2] = mesh->vertex_data[src_idx * 3 + 2];
        
        // Copy normal
        optimized->normal_data[i * 3] = mesh->normal_data[src_idx * 3];
        optimized->normal_data[i * 3 + 1] = mesh->normal_data[src_idx * 3 + 1];
        optimized->normal_data[i * 3 + 2] = mesh->normal_data[src_idx * 3 + 2];
        
        // Copy UV
        optimized->uv_data[i * 2] = mesh->uv_data[src_idx * 2];
        optimized->uv_data[i * 2 + 1] = mesh->uv_data[src_idx * 2 + 1];
    }
    
    return optimized;
}

// Texture compression
TextureData* metaverse_texture_compress(TextureData* texture, int quality) {
    if (!texture || texture->compressed) {
        return texture;
    }
    
    TextureData* compressed = malloc(sizeof(TextureData));
    compressed->width = texture->width;
    compressed->height = texture->height;
    compressed->channels = texture->channels;
    compressed->mipmapped = texture->mipmapped;
    compressed->compressed = true;
    
    // Simple block compression simulation
    // In reality, use libsquish, etc10, or hardware compression
    size_t compressed_size = (texture->width * texture->height * texture->channels) / 4;
    compressed->texture_data = malloc(compressed_size);
    
    // Simulate compression by downsampling
    for (int y = 0; y < texture->height; y += 2) {
        for (int x = 0; x < texture->width; x += 2) {
            uint32_t src_idx = (y * texture->width + x) * texture->channels;
            uint32_t dst_idx = ((y / 2) * (texture->width / 2) + (x / 2)) * texture->channels;
            
            // Average 2x2 block
            for (int c = 0; c < texture->channels; c++) {
                uint32_t sum = texture->texture_data[src_idx + c] +
                              texture->texture_data[src_idx + texture->channels + c] +
                              texture->texture_data[src_idx + texture->width * texture->channels + c] +
                              texture->texture_data[src_idx + texture->width * texture->channels + texture->channels + c];
                compressed->texture_data[dst_idx + c] = sum / 4;
            }
        }
    }
    
    // Generate mipmaps if requested
    if (compressed->mipmapped) {
        generate_mipmaps(compressed);
    }
    
    return compressed;
}

// Spatial audio update with HRTF
void metaverse_spatial_audio_update(MetaverseAmplifier* amp) {
    // Listener position (camera/player)
    float listener_pos[3] = {0.0f, 1.7f, 0.0f};
    float listener_forward[3] = {0.0f, 0.0f, -1.0f};
    float listener_up[3] = {0.0f, 1.0f, 0.0f};
    
    for (uint32_t i = 0; i < amp->emitter_count; i++) {
        AudioEmitter* emitter = &amp->audio_emitters[i];
        
        if (!emitter->spatialized) continue;
        
        // Calculate distance
        float dx = emitter->position[0] - listener_pos[0];
        float dy = emitter->position[1] - listener_pos[1];
        float dz = emitter->position[2] - listener_pos[2];
        float distance = sqrtf(dx*dx + dy*dy + dz*dz);
        
        // Distance attenuation
        float distance_attenuation = 1.0f / (1.0f + distance * 0.1f);
        
        // Calculate direction vector
        float direction[3] = {dx, dy, dz};
        normalize_vector(direction);
        
        // Calculate stereo panning based on direction
        float dot_left = dot_product(direction, get_left_vector(listener_forward, listener_up));
        float dot_front = dot_product(direction, listener_forward);
        
        // Simple stereo panning
        float left_gain = 0.5f * (1.0f - dot_left);
        float right_gain = 0.5f * (1.0f + dot_left);
        
        // Apply distance attenuation
        left_gain *= distance_attenuation;
        right_gain *= distance_attenuation;
        
        // Apply Doppler effect based on velocity
        float relative_velocity = calculate_relative_velocity(emitter->velocity, listener_pos);
        float doppler_factor = 1.0f + relative_velocity / 343.0f;  // Speed of sound
        emitter->pitch *= doppler_factor;
        
        // Update audio source (in real implementation, would use OpenAL or similar)
        update_audio_source(emitter->source_id, left_gain, right_gain, emitter->pitch);
    }
}

// Physics optimization with spatial partitioning
void metaverse_physics_optimized(MetaverseAmplifier* amp, double delta_time) {
    // Simple grid-based spatial partitioning
    const int GRID_SIZE = 32;
    const float CELL_SIZE = 10.0f;
    
    // Create spatial grid
    EntityGridCell grid[GRID_SIZE][GRID_SIZE][GRID_SIZE];
    memset(grid, 0, sizeof(grid));
    
    // Assign entities to grid cells
    for (uint32_t i = 0; i < amp->entity_count; i++) {
        MetaverseEntity* entity = &amp->entities[i];
        
        int grid_x = (int)(entity->position.x / CELL_SIZE) + GRID_SIZE/2;
        int grid_y = (int)(entity->position.y / CELL_SIZE) + GRID_SIZE/2;
        int grid_z = (int)(entity->position.z / CELL_SIZE) + GRID_SIZE/2;
        
        if (grid_x >= 0 && grid_x < GRID_SIZE &&
            grid_y >= 0 && grid_y < GRID_SIZE &&
            grid_z >= 0 && grid_z < GRID_SIZE) {
            
            EntityGridCell* cell = &grid[grid_x][grid_y][grid_z];
            if (cell->count < MAX_CELL_ENTITIES) {
                cell->entities[cell->count++] = entity;
            }
        }
    }
    
    // Check collisions only within same or adjacent cells
    for (int x = 0; x < GRID_SIZE; x++) {
        for (int y = 0; y < GRID_SIZE; y++) {
            for (int z = 0; z < GRID_SIZE; z++) {
                EntityGridCell* cell = &grid[x][y][z];
                
                // Check collisions within cell
                for (int i = 0; i < cell->count; i++) {
                    for (int j = i + 1; j < cell->count; j++) {
                        check_collision(cell->entities[i], cell->entities[j], delta_time);
                    }
                }
                
                // Check collisions with adjacent cells
                for (int dx = -1; dx <= 1; dx++) {
                    for (int dy = -1; dy <= 1; dy++) {
                        for (int dz = -1; dz <= 1; dz++) {
                            if (dx == 0 && dy == 0 && dz == 0) continue;
                            
                            int nx = x + dx;
                            int ny = y + dy;
                            int nz = z + dz;
                            
                            if (nx >= 0 && nx < GRID_SIZE &&
                                ny >= 0 && ny < GRID_SIZE &&
                                nz >= 0 && nz < GRID_SIZE) {
                                
                                EntityGridCell* neighbor = &grid[nx][ny][nz];
                                
                                for (int i = 0; i < cell->count; i++) {
                                    for (int j = 0; j < neighbor->count; j++) {
                                        check_collision(cell->entities[i], neighbor->entities[j], delta_time);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

// Network thread for multiplayer
void* metaverse_network_thread(void* arg) {
    MetaverseAmplifier* amp = (MetaverseAmplifier*)arg;
    
    // Network setup (simplified)
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in server_addr;
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(7777);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    
    while (amp->network_active) {
        // Receive entity updates
        EntityUpdate updates[64];
        socklen_t addr_len = sizeof(struct sockaddr_in);
        
        ssize_t received = recvfrom(sockfd, updates, sizeof(updates), 0,
                                   NULL, &addr_len);
        
        if (received > 0) {
            pthread_mutex_lock(&amp->entity_mutex);
            
            int update_count = received / sizeof(EntityUpdate);
            for (int i = 0; i < update_count; i++) {
                // Find and update entity
                for (uint32_t j = 0; j < amp->entity_count; j++) {
                    if (amp->entities[j].entity_id == updates[i].entity_id) {
                        amp->entities[j].position = updates[i].position;
                        amp->entities[j].rotation = updates[i].rotation;
                        break;
                    }
                }
            }
            
            pthread_mutex_unlock(&amp->entity_mutex);
        }
        
        // Send updates to other players
        metaverse_send_updates(amp, sockfd);
        
        usleep(16667);  // ~60Hz network update
    }
    
    close(sockfd);
    return NULL;
}

// Cleanup
void metaverse_amplifier_destroy(MetaverseAmplifier* amp) {
    if (!amp) return;
    
    amp->network_active = false;
    
    // Free entities
    free(amp->entities);
    
    // Free cache
    for (uint32_t i = 0; i < amp->cache_size; i++) {
        if (amp->mesh_cache[i].vertex_data) {
            free(amp->mesh_cache[i].vertex_data);
        }
        if (amp->texture_cache[i].texture_data) {
            free(amp->texture_cache[i].texture_data);
        }
    }
    
    free(amp->mesh_cache);
    free(amp->texture_cache);
    free(amp->audio_emitters);
    
    // Destroy synchronization primitives
    pthread_mutex_destroy(&amp->entity_mutex);
    pthread_mutex_destroy(&amp->render_mutex);
    pthread_rwlock_destroy(&amp->world_lock);
    
    free(amp);
    
    printf("Metaverse Amplifier destroyed\n");
}

// Utility functions
float calculate_relative_velocity(float* emitter_vel, float* listener_pos) {
    // Simplified relative velocity calculation
    return sqrtf(emitter_vel[0]*emitter_vel[0] + 
                emitter_vel[1]*emitter_vel[1] + 
                emitter_vel[2]*emitter_vel[2]);
}

void normalize_vector(float* v) {
    float length = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    if (length > 0.0001f) {
        v[0] /= length;
        v[1] /= length;
        v[2] /= length;
    }
}

float dot_product(float* a, float* b) {
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}

void* get_left_vector(float* forward, float* up) {
    static float left[3];
    left[0] = forward[1]*up[2] - forward[2]*up[1];
    left[1] = forward[2]*up[0] - forward[0]*up[2];
    left[2] = forward[0]*up[1] - forward[1]*up[0];
    return left;
}

int main() {
    printf("Godot Metaverse Amplifier - C Core\n");
    
    // Simulate Godot API
    GodotAPI api = {
        .godot_alloc = malloc,
        .godot_free = free,
        .godot_print = printf,
        .godot_error = printf,
        .godot_get_time = get_time
    };
    
    // Create amplifier
    MetaverseAmplifier* amp = metaverse_amplifier_create(&api);
    if (!amp) {
        fprintf(stderr, "Failed to create amplifier\n");
        return 1;
    }
    
    // Initialize
    metaverse_amplifier_init(amp);
    
    // Main loop simulation
    printf("Starting metaverse simulation...\n");
    
    struct timespec last_frame, current_frame;
    clock_gettime(CLOCK_MONOTONIC, &last_frame);
    
    double accumulated_time = 0.0;
    int frame_count = 0;
    
    while (frame_count < 600) {  // Run for 10 seconds at 60fps
        clock_gettime(CLOCK_MONOTONIC, &current_frame);
        
        double delta_time = (current_frame.tv_sec - last_frame.tv_sec) + 
                           (current_frame.tv_nsec - last_frame.tv_nsec) / 1e9;
        last_frame = current_frame;
        
        // Update world
        metaverse_update_world(amp, delta_time);
        
        // Process input
        float input_state[16] = {0};
        metaverse_process_input(amp, input_state);
        
        // Update physics
        metaverse_physics_optimized(amp, delta_time);
        
        // Render
        metaverse_render_enhanced(amp);
        
        // Network update
        metaverse_network_update(amp);
        
        // Update frame timing
        amp->frame_time = 0.9 * amp->frame_time + 0.1 * delta_time;
        accumulated_time += delta_time;
        frame_count++;
        
        // Print stats every second
        if (accumulated_time >= 1.0) {
            printf("[STATS] FPS: %d, Draw Calls: %d, Physics: %.2fms, Render: %.2fms\n",
                   amp->fps, amp->draw_calls, 
                   amp->physics_time * 1000, amp->render_time * 1000);
            accumulated_time = 0.0;
        }
        
        // Sleep to maintain target framerate
        double target_time = 1.0/60.0;
        if (delta_time < target_time) {
            usleep((useconds_t)((target_time - delta_time) * 1000000));
        }
    }
    
    // Cleanup
    metaverse_amplifier_destroy(amp);
    
    return 0;
}

double get_time() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}