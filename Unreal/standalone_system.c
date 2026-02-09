/*******************************************************************************
 * STANDALONE SYSTEM: Single-Room Installation
 * Complete self-contained projection system for single control room
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// Hardware abstraction layer
typedef struct {
    int projector_count;
    int screen_count;
    float room_width;
    float room_height;
    char projection_type[20];  // "planar", "curved", "dome", "multi-surface"
} RoomConfiguration;

// Display system components
typedef struct {
    int id;
    int resolution_x;
    int resolution_y;
    int refresh_rate;
    char display_type[20];  // "led_wall", "projector", "hologram", "floor", "dome"
    bool is_active;
    float brightness;
    float contrast;
} DisplayUnit;

// Projection management
typedef struct {
    DisplayUnit* displays;
    int display_count;
    RoomConfiguration config;
    bool system_active;
    pthread_mutex_t display_mutex;
} ProjectionSystem;

// Audio system
typedef struct {
    int channel_count;
    float volume_levels[8];  // 7.1 surround
    bool spatial_audio_enabled;
    char ambience_profile[32];
} AudioSystem;

// Main system controller
typedef struct {
    ProjectionSystem projection;
    AudioSystem audio;
    pthread_t update_thread;
    bool running;
    
    // Performance monitoring
    double frame_rate;
    double cpu_usage;
    double gpu_temperature;
} StandaloneSystem;

// Function prototypes
StandaloneSystem* create_standalone_system(RoomConfiguration config);
void initialize_displays(StandaloneSystem* system);
void calibrate_projectors(StandaloneSystem* system);
void* system_update_thread(void* arg);
void render_content(StandaloneSystem* system, const char* content_type);
void adjust_lighting(StandaloneSystem* system, float intensity);
void emergency_shutdown(StandaloneSystem* system);
void save_system_state(StandaloneSystem* system, const char* filename);

// Main system creation
StandaloneSystem* create_standalone_system(RoomConfiguration config) {
    StandaloneSystem* system = malloc(sizeof(StandaloneSystem));
    if (!system) return NULL;
    
    system->projection.config = config;
    system->projection.display_count = config.projector_count + config.screen_count + 2; // +2 for floor/dome
    system->projection.displays = malloc(sizeof(DisplayUnit) * system->projection.display_count);
    system->projection.system_active = false;
    pthread_mutex_init(&system->projection.display_mutex, NULL);
    
    // Initialize audio system
    system->audio.channel_count = 8;  // 7.1 surround
    memset(system->audio.volume_levels, 0.7f, sizeof(float) * 8);
    system->audio.spatial_audio_enabled = true;
    strcpy(system->audio.ambience_profile, "control_room");
    
    system->running = false;
    system->frame_rate = 60.0;
    system->cpu_usage = 0.0;
    system->gpu_temperature = 40.0;
    
    initialize_displays(system);
    return system;
}

// Display initialization
void initialize_displays(StandaloneSystem* system) {
    int display_idx = 0;
    
    // Main wall displays
    for (int i = 0; i < system->projection.config.screen_count; i++) {
        system->projection.displays[display_idx].id = display_idx;
        system->projection.displays[display_idx].resolution_x = 3840;
        system->projection.displays[display_idx].resolution_y = 2160;
        system->projection.displays[display_idx].refresh_rate = 120;
        strcpy(system->projection.displays[display_idx].display_type, "led_wall");
        system->projection.displays[display_idx].is_active = true;
        system->projection.displays[display_idx].brightness = 1.0f;
        system->projection.displays[display_idx].contrast = 1.0f;
        display_idx++;
    }
    
    // Holographic displays
    for (int i = 0; i < 2; i++) {
        system->projection.displays[display_idx].id = display_idx;
        system->projection.displays[display_idx].resolution_x = 1920;
        system->projection.displays[display_idx].resolution_y = 1080;
        system->projection.displays[display_idx].refresh_rate = 60;
        strcpy(system->projection.displays[display_idx].display_type, "hologram");
        system->projection.displays[display_idx].is_active = false;
        system->projection.displays[display_idx].brightness = 0.8f;
        system->projection.displays[display_idx].contrast = 1.2f;
        display_idx++;
    }
    
    // Floor projection
    system->projection.displays[display_idx].id = display_idx;
    system->projection.displays[display_idx].resolution_x = 4096;
    system->projection.displays[display_idx].resolution_y = 4096;
    system->projection.displays[display_idx].refresh_rate = 30;
    strcpy(system->projection.displays[display_idx].display_type, "floor");
    system->projection.displays[display_idx].is_active = false;
    display_idx++;
    
    // Dome projection
    system->projection.displays[display_idx].id = display_idx;
    system->projection.displays[display_idx].resolution_x = 4096;
    system->projection.displays[display_idx].resolution_y = 2048;
    system->projection.displays[display_idx].refresh_rate = 90;
    strcpy(system->projection.displays[display_idx].display_type, "dome");
    system->projection.displays[display_idx].is_active = false;
}

// System update thread
void* system_update_thread(void* arg) {
    StandaloneSystem* system = (StandaloneSystem*)arg;
    struct timespec last_time, current_time;
    clock_gettime(CLOCK_MONOTONIC, &last_time);
    
    while (system->running) {
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        double delta_time = (current_time.tv_sec - last_time.tv_sec) + 
                           (current_time.tv_nsec - last_time.tv_nsec) / 1e9;
        last_time = current_time;
        
        // Update frame rate calculation
        system->frame_rate = 0.9 * system->frame_rate + 0.1 * (1.0 / delta_time);
        
        // Monitor system health
        system->gpu_temperature += (rand() % 100 - 50) * 0.01f;
        if (system->gpu_temperature > 85.0) {
            system->gpu_temperature = 85.0;
        }
        
        // Adjust cooling based on temperature
        if (system->gpu_temperature > 75.0) {
            // Increase fan speed
            printf("[SYSTEM] High GPU temperature: %.1f°C\n", system->gpu_temperature);
        }
        
        usleep(16667);  // ~60Hz update rate
    }
    return NULL;
}

// Content rendering
void render_content(StandaloneSystem* system, const char* content_type) {
    pthread_mutex_lock(&system->projection.display_mutex);
    
    printf("[RENDER] Rendering %s content\n", content_type);
    
    // Activate appropriate displays based on content
    if (strcmp(content_type, "data_visualization") == 0) {
        for (int i = 0; i < system->projection.display_count; i++) {
            if (strcmp(system->projection.displays[i].display_type, "led_wall") == 0) {
                system->projection.displays[i].is_active = true;
                system->projection.displays[i].brightness = 1.0f;
            }
        }
    } else if (strcmp(content_type, "holographic") == 0) {
        for (int i = 0; i < system->projection.display_count; i++) {
            if (strcmp(system->projection.displays[i].display_type, "hologram") == 0) {
                system->projection.displays[i].is_active = true;
            }
        }
    } else if (strcmp(content_type, "immersive") == 0) {
        // Activate all displays
        for (int i = 0; i < system->projection.display_count; i++) {
            system->projection.displays[i].is_active = true;
        }
    }
    
    pthread_mutex_unlock(&system->projection.display_mutex);
}

// Emergency shutdown
void emergency_shutdown(StandaloneSystem* system) {
    printf("[EMERGENCY] Performing emergency shutdown\n");
    
    system->running = false;
    pthread_join(system->update_thread, NULL);
    
    // Turn off all displays
    for (int i = 0; i < system->projection.display_count; i++) {
        system->projection.displays[i].is_active = false;
        system->projection.displays[i].brightness = 0.0f;
    }
    
    // Disable audio
    memset(system->audio.volume_levels, 0.0f, sizeof(float) * 8);
    
    printf("[SYSTEM] Emergency shutdown complete\n");
}

// Main system control
void start_system(StandaloneSystem* system) {
    printf("[SYSTEM] Starting standalone projection system\n");
    
    calibrate_projectors(system);
    system->projection.system_active = true;
    system->running = true;
    
    // Start update thread
    pthread_create(&system->update_thread, NULL, system_update_thread, system);
    
    printf("[SYSTEM] System started successfully\n");
    printf("[SYSTEM] Frame rate: %.1f FPS\n", system->frame_rate);
}

int main() {
    // Configure room
    RoomConfiguration config = {
        .projector_count = 4,
        .screen_count = 6,
        .room_width = 12.0f,
        .room_height = 4.0f,
        .projection_type = "multi-surface"
    };
    
    // Create and start system
    StandaloneSystem* system = create_standalone_system(config);
    if (!system) {
        fprintf(stderr, "Failed to create system\n");
        return 1;
    }
    
    start_system(system);
    
    // Demo sequence
    sleep(2);
    render_content(system, "data_visualization");
    sleep(5);
    render_content(system, "holographic");
    sleep(6);
    
    // Emergency shutdown demo
    emergency_shutdown(system);
    
    // Cleanup
    free(system->projection.displays);
    free(system);
    
    return 0;
}