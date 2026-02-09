/*******************************************************************************
 * METAVERSE AUDIO & IMMERSION SYSTEM
 * Spatial audio, environmental sounds, and immersion enhancements
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <OpenAL/al.h>
#include <OpenAL/alc.h>

// Audio Engine Structures
typedef struct {
    ALCdevice* device;
    ALCcontext* context;
    bool initialized;
    float master_volume;
    float speed_of_sound;
    float air_absorption;
} AudioEngine;

// HRTF (Head-Related Transfer Function) Data
typedef struct {
    float* left_ear_ir;   // Impulse response for left ear
    float* right_ear_ir;  // Impulse response for right ear
    int ir_length;
    int sample_rate;
    float elevation;
    float azimuth;
} HRTFPoint;

typedef struct {
    HRTFPoint* points;
    int point_count;
    int current_point;
    bool enabled;
} HRTFDatabase;

// Spatial Audio Source
typedef struct {
    ALuint source_id;
    ALuint buffer_ids[4];  // For streaming
    uint64_t entity_id;
    
    // Spatial properties
    float position[3];
    float velocity[3];
    float direction[3];
    float inner_cone_angle;
    float outer_cone_angle;
    float outer_cone_gain;
    
    // Audio properties
    float gain;
    float pitch;
    float reference_distance;
    float max_distance;
    float rolloff_factor;
    
    // State
    bool playing;
    bool looping;
    bool spatialized;
    bool hrtf_enabled;
    
    // Streaming
    bool streaming;
    int current_buffer;
    int buffer_count;
    pthread_t stream_thread;
} SpatialAudioSource;

// Environmental Audio Zone
typedef struct {
    float bounds[6];  // AABB: min_x, max_x, min_y, max_y, min_z, max_z
    ALuint effect_slot;
    ALuint effect_id;
    ALuint filter_id;
    
    // Environmental properties
    float reverb_density;
    float reverb_diffusion;
    float reverb_gain;
    float reverb_gain_hf;
    float reverb_decay_time;
    float reverb_decay_hf_ratio;
    
    // Occlusion
    float occlusion_factor;
    float transmission_factor;
    
    // Zone-specific sounds
    SpatialAudioSource** ambient_sources;
    int ambient_source_count;
} EnvironmentalZone;

// Audio Mixer
typedef struct {
    SpatialAudioSource** sources;
    int source_count;
    int source_capacity;
    
    EnvironmentalZone** zones;
    int zone_count;
    int zone_capacity;
    
    // Listener
    float listener_position[3];
    float listener_velocity[3];
    float listener_orientation[6];  // at_x, at_y, at_z, up_x, up_y, up_z
    
    // HRTF
    HRTFDatabase hrtf;
    
    // Threading
    pthread_t update_thread;
    bool audio_active;
    
    // Performance
    int active_sources;
    int max_sources;
    float cpu_usage;
} AudioMixer;

// Function prototypes
AudioMixer* audio_mixer_create(int max_sources);
bool audio_mixer_init(AudioMixer* mixer);
void audio_mixer_destroy(AudioMixer* mixer);
SpatialAudioSource* audio_create_source(AudioMixer* mixer, uint64_t entity_id);
bool audio_source_set_position(SpatialAudioSource* source, float x, float y, float z);
bool audio_source_set_buffer(SpatialAudioSource* source, ALuint buffer);
bool audio_source_play(SpatialAudioSource* source);
bool audio_source_stop(SpatialAudioSource* source);
void audio_update_listener(AudioMixer* mixer, float* position, float* orientation);
EnvironmentalZone* audio_create_zone(AudioMixer* mixer, float* bounds);
void audio_zone_set_reverb(EnvironmentalZone* zone, float density, float diffusion, 
                          float decay_time, float hf_ratio);
void audio_zone_set_occlusion(EnvironmentalZone* zone, float occlusion, float transmission);
void* audio_update_thread(void* arg);
void audio_apply_hrtf(SpatialAudioSource* source, HRTFDatabase* hrtf, float* direction);
void audio_calculate_doppler(SpatialAudioSource* source, float* listener_velocity);
float audio_calculate_attenuation(float distance, float ref_distance, 
                                 float max_distance, float rolloff);

// Create audio mixer
AudioMixer* audio_mixer_create(int max_sources) {
    AudioMixer* mixer = malloc(sizeof(AudioMixer));
    if (!mixer) return NULL;
    
    memset(mixer, 0, sizeof(AudioMixer));
    
    // Initialize OpenAL
    mixer->max_sources = max_sources;
    
    // Allocate source array
    mixer->source_capacity = max_sources;
    mixer->source_count = 0;
    mixer->sources = malloc(sizeof(SpatialAudioSource*) * mixer->source_capacity);
    
    // Allocate zone array
    mixer->zone_capacity = 32;
    mixer->zone_count = 0;
    mixer->zones = malloc(sizeof(EnvironmentalZone*) * mixer->zone_capacity);
    
    // Initialize listener
    mixer->listener_position[0] = 0.0f;
    mixer->listener_position[1] = 0.0f;
    mixer->listener_position[2] = 0.0f;
    
    mixer->listener_velocity[0] = 0.0f;
    mixer->listener_velocity[1] = 0.0f;
    mixer->listener_velocity[2] = 0.0f;
    
    mixer->listener_orientation[0] = 0.0f;  // at_x
    mixer->listener_orientation[1] = 0.0f;  // at_y
    mixer->listener_orientation[2] = -1.0f; // at_z
    mixer->listener_orientation[3] = 0.0f;  // up_x
    mixer->listener_orientation[4] = 1.0f;  // up_y
    mixer->listener_orientation[5] = 0.0f;  // up_z
    
    // Initialize HRTF
    mixer->hrtf.enabled = false;
    mixer->hrtf.point_count = 0;
    mixer->hrtf.points = NULL;
    mixer->hrtf.current_point = 0;
    
    mixer->audio_active = false;
    mixer->active_sources = 0;
    mixer->cpu_usage = 0.0f;
    
    return mixer;
}

// Initialize audio system
bool audio_mixer_init(AudioMixer* mixer) {
    printf("[AUDIO] Initializing OpenAL audio system...\n");
    
    // Open default device
    ALCdevice* device = alcOpenDevice(NULL);
    if (!device) {
        fprintf(stderr, "[AUDIO] Failed to open audio device\n");
        return false;
    }
    
    // Create context
    ALCcontext* context = alcCreateContext(device, NULL);
    if (!context) {
        fprintf(stderr, "[AUDIO] Failed to create audio context\n");
        alcCloseDevice(device);
        return false;
    }
    
    // Make context current
    if (!alcMakeContextCurrent(context)) {
        fprintf(stderr, "[AUDIO] Failed to make context current\n");
        alcDestroyContext(context);
        alcCloseDevice(device);
        return false;
    }
    
    // Check for OpenAL extensions
    const ALCchar* extensions = alcGetString(device, ALC_EXTENSIONS);
    if (strstr(extensions, "ALC_EXT_EFX")) {
        printf("[AUDIO] EFX extension available\n");
    }
    
    if (strstr(extensions, "AL_SOFT_HRTF")) {
        printf("[AUDIO] HRTF extension available\n");
        mixer->hrtf.enabled = true;
    }
    
    // Set listener properties
    alListener3f(AL_POSITION, 0.0f, 0.0f, 0.0f);
    alListener3f(AL_VELOCITY, 0.0f, 0.0f, 0.0f);
    
    float orientation[6] = {0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f};
    alListenerfv(AL_ORIENTATION, orientation);
    
    // Check for errors
    ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        fprintf(stderr, "[AUDIO] OpenAL error: %d\n", error);
        return false;
    }
    
    printf("[AUDIO] Audio system initialized successfully\n");
    printf("[AUDIO] Device: %s\n", alcGetString(device, ALC_DEVICE_SPECIFIER));
    printf("[AUDIO] Renderer: %s\n", alGetString(AL_RENDERER));
    printf("[AUDIO] Version: %s\n", alGetString(AL_VERSION));
    
    // Start update thread
    mixer->audio_active = true;
    pthread_create(&mixer->update_thread, NULL, audio_update_thread, mixer);
    
    return true;
}

// Create spatial audio source
SpatialAudioSource* audio_create_source(AudioMixer* mixer, uint64_t entity_id) {
    if (mixer->source_count >= mixer->source_capacity) {
        fprintf(stderr, "[AUDIO] Maximum sources reached\n");
        return NULL;
    }
    
    SpatialAudioSource* source = malloc(sizeof(SpatialAudioSource));
    if (!source) return NULL;
    
    memset(source, 0, sizeof(SpatialAudioSource));
    
    // Generate OpenAL source
    alGenSources(1, &source->source_id);
    
    ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        fprintf(stderr, "[AUDIO] Failed to generate source: %d\n", error);
        free(source);
        return NULL;
    }
    
    // Set default properties
    source->entity_id = entity_id;
    
    source->position[0] = 0.0f;
    source->position[1] = 0.0f;
    source->position[2] = 0.0f;
    
    source->velocity[0] = 0.0f;
    source->velocity[1] = 0.0f;
    source->velocity[2] = 0.0f;
    
    source->direction[0] = 0.0f;
    source->direction[1] = 0.0f;
    source->direction[2] = 0.0f;
    
    source->gain = 1.0f;
    source->pitch = 1.0f;
    source->reference_distance = 1.0f;
    source->max_distance = 100.0f;
    source->rolloff_factor = 1.0f;
    
    source->inner_cone_angle = 360.0f;
    source->outer_cone_angle = 360.0f;
    source->outer_cone_gain = 0.0f;
    
    source->playing = false;
    source->looping = false;
    source->spatialized = true;
    source->hrtf_enabled = false;
    source->streaming = false;
    
    // Apply default OpenAL properties
    alSourcef(source->source_id, AL_GAIN, source->gain);
    alSourcef(source->source_id, AL_PITCH, source->pitch);
    alSourcef(source->source_id, AL_REFERENCE_DISTANCE, source->reference_distance);
    alSourcef(source->source_id, AL_MAX_DISTANCE, source->max_distance);
    alSourcef(source->source_id, AL_ROLLOFF_FACTOR, source->rolloff_factor);
    
    alSource3f(source->source_id, AL_POSITION, 
               source->position[0], source->position[1], source->position[2]);
    alSource3f(source->source_id, AL_VELOCITY,
               source->velocity[0], source->velocity[1], source->velocity[2]);
    alSource3f(source->source_id, AL_DIRECTION,
               source->direction[0], source->direction[1], source->direction[2]);
    
    alSourcef(source->source_id, AL_CONE_INNER_ANGLE, source->inner_cone_angle);
    alSourcef(source->source_id, AL_CONE_OUTER_ANGLE, source->outer_cone_angle);
    alSourcef(source->source_id, AL_CONE_OUTER_GAIN, source->outer_cone_gain);
    
    // Enable HRTF if available
    if (mixer->hrtf.enabled) {
        alSourcei(source->source_id, AL_DIRECT_FILTER, AL_FILTER_NULL);
        source->hrtf_enabled = true;
    }
    
    // Add to mixer
    mixer->sources[mixer->source_count++] = source;
    mixer->active_sources++;
    
    printf("[AUDIO] Created audio source %u for entity %lu\n", 
           source->source_id, entity_id);
    
    return source;
}

// Set source position
bool audio_source_set_position(SpatialAudioSource* source, float x, float y, float z) {
    source->position[0] = x;
    source->position[1] = y;
    source->position[2] = z;
    
    alSource3f(source->source_id, AL_POSITION, x, y, z);
    
    ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        fprintf(stderr, "[AUDIO] Failed to set source position: %d\n", error);
        return false;
    }
    
    return true;
}

// Set source audio buffer
bool audio_source_set_buffer(SpatialAudioSource* source, ALuint buffer) {
    alSourcei(source->source_id, AL_BUFFER, buffer);
    
    ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        fprintf(stderr, "[AUDIO] Failed to set source buffer: %d\n", error);
        return false;
    }
    
    return true;
}

// Play source
bool audio_source_play(SpatialAudioSource* source) {
    alSourcePlay(source->source_id);
    
    ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        fprintf(stderr, "[AUDIO] Failed to play source: %d\n", error);
        return false;
    }
    
    source->playing = true;
    return true;
}

// Update listener position/orientation
void audio_update_listener(AudioMixer* mixer, float* position, float* orientation) {
    if (position) {
        memcpy(mixer->listener_position, position, 3 * sizeof(float));
        alListener3f(AL_POSITION, position[0], position[1], position[2]);
    }
    
    if (orientation) {
        memcpy(mixer->listener_orientation, orientation, 6 * sizeof(float));
        alListenerfv(AL_ORIENTATION, orientation);
    }
}

// Create environmental zone
EnvironmentalZone* audio_create_zone(AudioMixer* mixer, float* bounds) {
    if (mixer->zone_count >= mixer->zone_capacity) {
        fprintf(stderr, "[AUDIO] Maximum zones reached\n");
        return NULL;
    }
    
    EnvironmentalZone* zone = malloc(sizeof(EnvironmentalZone));
    if (!zone) return NULL;
    
    memcpy(zone->bounds, bounds, 6 * sizeof(float));
    
    // Generate OpenAL effect slot and effect
    alGenAuxiliaryEffectSlots(1, &zone->effect_slot);
    alGenEffects(1, &zone->effect_id);
    
    // Set effect type to reverb
    alEffecti(zone->effect_id, AL_EFFECT_TYPE, AL_EFFECT_REVERB);
    
    // Set default reverb properties
    zone->reverb_density = 1.0f;
    zone->reverb_diffusion = 1.0f;
    zone->reverb_gain = 0.32f;
    zone->reverb_gain_hf = 0.89f;
    zone->reverb_decay_time = 1.49f;
    zone->reverb_decay_hf_ratio = 0.83f;
    
    // Apply reverb properties
    alEffectf(zone->effect_id, AL_REVERB_DENSITY, zone->reverb_density);
    alEffectf(zone->effect_id, AL_REVERB_DIFFUSION, zone->reverb_diffusion);
    alEffectf(zone->effect_id, AL_REVERB_GAIN, zone->reverb_gain);
    alEffectf(zone->effect_id, AL_REVERB_GAINHF, zone->reverb_gain_hf);
    alEffectf(zone->effect_id, AL_REVERB_DECAY_TIME, zone->reverb_decay_time);
    alEffectf(zone->effect_id, AL_REVERB_DECAY_HFRATIO, zone->reverb_decay_hf_ratio);
    
    // Attach effect to effect slot
    alAuxiliaryEffectSloti(zone->effect_slot, AL_EFFECTSLOT_EFFECT, zone->effect_id);
    
    // Create filter for occlusion
    alGenFilters(1, &zone->filter_id);
    alFilteri(zone->filter_id, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
    
    zone->occlusion_factor = 0.0f;
    zone->transmission_factor = 1.0f;
    
    zone->ambient_sources = NULL;
    zone->ambient_source_count = 0;
    
    // Add to mixer
    mixer->zones[mixer->zone_count++] = zone;
    
    printf("[AUDIO] Created environmental zone\n");
    return zone;
}

// Set zone reverb properties
void audio_zone_set_reverb(EnvironmentalZone* zone, float density, float diffusion,
                          float decay_time, float hf_ratio) {
    zone->reverb_density = density;
    zone->reverb_diffusion = diffusion;
    zone->reverb_decay_time = decay_time;
    zone->reverb_decay_hf_ratio = hf_ratio;
    
    alEffectf(zone->effect_id, AL_REVERB_DENSITY, density);
    alEffectf(zone->effect_id, AL_REVERB_DIFFUSION, diffusion);
    alEffectf(zone->effect_id, AL_REVERB_DECAY_TIME, decay_time);
    alEffectf(zone->effect_id, AL_REVERB_DECAY_HFRATIO, hf_ratio);
    
    // Update effect slot
    alAuxiliaryEffectSloti(zone->effect_slot, AL_EFFECTSLOT_EFFECT, zone->effect_id);
}

// Audio update thread
void* audio_update_thread(void* arg) {
    AudioMixer* mixer = (AudioMixer*)arg;
    struct timespec last_update, current_update;
    clock_gettime(CLOCK_MONOTONIC, &last_update);
    
    while (mixer->audio_active) {
        clock_gettime(CLOCK_MONOTONIC, &current_update);
        
        double elapsed = (current_update.tv_sec - last_update.tv_sec) + 
                        (current_update.tv_nsec - last_update.tv_nsec) / 1e9;
        last_update = current_update;
        
        // Update active source count
        int active = 0;
        for (int i = 0; i < mixer->source_count; i++) {
            if (mixer->sources[i] && mixer->sources[i]->playing) {
                ALint state;
                alGetSourcei(mixer->sources[i]->source_id, AL_SOURCE_STATE, &state);
                mixer->sources[i]->playing = (state == AL_PLAYING);
                
                if (mixer->sources[i]->playing) {
                    active++;
                    
                    // Update spatial properties
                    audio_update_source_spatial(mixer, mixer->sources[i]);
                }
            }
        }
        
        mixer->active_sources = active;
        
        // Update environmental zones
        for (int i = 0; i < mixer->zone_count; i++) {
            audio_update_zone(mixer, mixer->zones[i]);
        }
        
        // Update CPU usage metric
        static double update_time = 0.0;
        update_time = 0.9 * update_time + 0.1 * elapsed;
        mixer->cpu_usage = update_time / (1.0 / 60.0) * 100.0;  // Percentage of frame
        
        // Sleep to maintain update rate
        double target_time = 1.0 / 60.0;  // 60Hz audio update
        if (elapsed < target_time) {
            usleep((useconds_t)((target_time - elapsed) * 1000000));
        }
    }
    
    return NULL;
}

// Update source spatial properties
void audio_update_source_spatial(AudioMixer* mixer, SpatialAudioSource* source) {
    if (!source->spatialized) return;
    
    // Calculate distance to listener
    float dx = source->position[0] - mixer->listener_position[0];
    float dy = source->position[1] - mixer->listener_position[1];
    float dz = source->position[2] - mixer->listener_position[2];
    float distance = sqrtf(dx*dx + dy*dy + dz*dz);
    
    // Calculate attenuation
    float attenuation = audio_calculate_attenuation(distance,
                                                   source->reference_distance,
                                                   source->max_distance,
                                                   source->rolloff_factor);
    
    // Apply distance attenuation
    alSourcef(source->source_id, AL_GAIN, source->gain * attenuation);
    
    // Calculate Doppler effect
    audio_calculate_doppler(source, mixer->listener_velocity);
    
    // Calculate direction vector for HRTF
    if (source->hrtf_enabled && mixer->hrtf.enabled) {
        float direction[3] = {dx, dy, dz};
        normalize_vector(direction);
        audio_apply_hrtf(source, &mixer->hrtf, direction);
    }
    
    // Check occlusion
    float occlusion = audio_calculate_occlusion(mixer, source);
    if (occlusion > 0.0f) {
        // Apply low-pass filter for occlusion
        float gain_hf = 1.0f - occlusion;
        alFilterf(source->filter_id, AL_LOWPASS_GAIN, 1.0f);
        alFilterf(source->filter_id, AL_LOWPASS_GAINHF, gain_hf);
        alSourcei(source->source_id, AL_DIRECT_FILTER, source->filter_id);
    } else {
        alSourcei(source->source_id, AL_DIRECT_FILTER, AL_FILTER_NULL);
    }
}

// Calculate attenuation using inverse distance model
float audio_calculate_attenuation(float distance, float ref_distance,
                                 float max_distance, float rolloff) {
    if (distance <= ref_distance) {
        return 1.0f;
    }
    
    if (distance >= max_distance) {
        return 0.0f;
    }
    
    // Inverse distance attenuation
    float attenuation = ref_distance / (ref_distance + rolloff * (distance - ref_distance));
    return attenuation;
}

// Calculate Doppler effect
void audio_calculate_doppler(SpatialAudioSource* source, float* listener_velocity) {
    // Calculate relative velocity
    float rel_velocity[3] = {
        source->velocity[0] - listener_velocity[0],
        source->velocity[1] - listener_velocity[1],
        source->velocity[2] - listener_velocity[2]
    };
    
    // Calculate direction vector
    float direction[3] = {
        source->position[0] - listener_velocity[0],
        source->position[1] - listener_velocity[1],
        source->position[2] - listener_velocity[2]
    };
    
    normalize_vector(direction);
    
    // Project relative velocity onto direction
    float projected_velocity = 
        rel_velocity[0] * direction[0] +
        rel_velocity[1] * direction[1] +
        rel_velocity[2] * direction[2];
    
    // Calculate Doppler factor (simplified)
    float speed_of_sound = 343.0f;  // m/s
    float doppler_factor = (speed_of_sound + projected_velocity) / speed_of_sound;
    
    // Clamp to reasonable range
    doppler_factor = fmaxf(0.5f, fminf(doppler_factor, 2.0f));
    
    // Apply Doppler pitch shift
    alSourcef(source->source_id, AL_PITCH, source->pitch * doppler_factor);
}

// Calculate occlusion
float audio_calculate_occlusion(AudioMixer* mixer, SpatialAudioSource* source) {
    float total_occlusion = 0.0f;
    int zone_count = 0;
    
    // Check which zones the listener and source are in
    for (int i = 0; i < mixer->zone_count; i++) {
        EnvironmentalZone* zone = mixer->zones[i];
        
        bool listener_in_zone = aabb_contains_point(zone->bounds, mixer->listener_position);
        bool source_in_zone = aabb_contains_point(zone->bounds, source->position);
        
        if (listener_in_zone || source_in_zone) {
            total_occlusion += zone->occlusion_factor;
            zone_count++;
        }
    }
    
    if (zone_count > 0) {
        return total_occlusion / zone_count;
    }
    
    return 0.0f;
}

// Apply HRTF
void audio_apply_hrtf(SpatialAudioSource* source, HRTFDatabase* hrtf, float* direction) {
    if (!hrtf->enabled || hrtf->point_count == 0) return;
    
    // Convert direction to spherical coordinates
    float azimuth = atan2f(direction[0], direction[2]);
    float elevation = asinf(direction[1]);
    
    // Convert to degrees
    azimuth = azimuth * 180.0f / M_PI;
    elevation = elevation * 180.0f / M_PI;
    
    // Find nearest HRTF point (simplified - would use interpolation)
    int nearest_point = 0;
    float min_distance = FLT_MAX;
    
    for (int i = 0; i < hrtf->point_count; i++) {
        float az_diff = fabsf(hrtf->points[i].azimuth - azimuth);
        float el_diff = fabsf(hrtf->points[i].elevation - elevation);
        float distance = sqrtf(az_diff*az_diff + el_diff*el_diff);
        
        if (distance < min_distance) {
            min_distance = distance;
            nearest_point = i;
        }
    }
    
    hrtf->current_point = nearest_point;
    
    // In real implementation, would apply HRTF filter to source
    // This is simplified
}

// Cleanup
void audio_mixer_destroy(AudioMixer* mixer) {
    if (!mixer) return;
    
    mixer->audio_active = false;
    if (mixer->update_thread) {
        pthread_join(mixer->update_thread, NULL);
    }
    
    // Delete all sources
    for (int i = 0; i < mixer->source_count; i++) {
        if (mixer->sources[i]) {
            if (mixer->sources[i]->source_id) {
                alDeleteSources(1, &mixer->sources[i]->source_id);
            }
            free(mixer->sources[i]);
        }
    }
    
    // Delete all zones
    for (int i = 0; i < mixer->zone_count; i++) {
        if (mixer->zones[i]) {
            if (mixer->zones[i]->effect_slot) {
                alDeleteAuxiliaryEffectSlots(1, &mixer->zones[i]->effect_slot);
            }
            if (mixer->zones[i]->effect_id) {
                alDeleteEffects(1, &mixer->zones[i]->effect_id);
            }
            if (mixer->zones[i]->filter_id) {
                alDeleteFilters(1, &mixer->zones[i]->filter_id);
            }
            free(mixer->zones[i]);
        }
    }
    
    // Destroy OpenAL context
    ALCcontext* context = alcGetCurrentContext();
    ALCdevice* device = alcGetContextsDevice(context);
    
    alcMakeContextCurrent(NULL);
    if (context) alcDestroyContext(context);
    if (device) alcCloseDevice(device);
    
    free(mixer->sources);
    free(mixer->zones);
    free(mixer);
    
    printf("[AUDIO] Audio system destroyed\n");
}

// Utility functions
void normalize_vector(float* v) {
    float length = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    if (length > 0.0001f) {
        v[0] /= length;
        v[1] /= length;
        v[2] /= length;
    }
}

bool aabb_contains_point(float* bounds, float* point) {
    return (point[0] >= bounds[0] && point[0] <= bounds[1] &&
            point[1] >= bounds[2] && point[1] <= bounds[3] &&
            point[2] >= bounds[4] && point[2] <= bounds[5]);
}

int main_audio_test() {
    printf("Metaverse Audio System Test\n");
    
    // Create audio mixer
    AudioMixer* mixer = audio_mixer_create(32);
    if (!mixer) {
        fprintf(stderr, "Failed to create audio mixer\n");
        return 1;
    }
    
    // Initialize audio system
    if (!audio_mixer_init(mixer)) {
        fprintf(stderr, "Failed to initialize audio system\n");
        free(mixer);
        return 1;
    }
    
    // Create test source
    SpatialAudioSource* source = audio_create_source(mixer, 1);
    if (!source) {
        fprintf(stderr, "Failed to create audio source\n");
        audio_mixer_destroy(mixer);
        return 1;
    }
    
    // Set source position
    audio_source_set_position(source, 10.0f, 0.0f, 0.0f);
    
    // Create test buffer with sine wave
    ALuint buffer;
    alGenBuffers(1, &buffer);
    
    // Generate 440Hz sine wave for 1 second
    const int SAMPLE_RATE = 44100;
    const int NUM_SAMPLES = SAMPLE_RATE;
    short samples[NUM_SAMPLES];
    
    for (int i = 0; i < NUM_SAMPLES; i++) {
        samples[i] = (short)(32767.0f * sinf(2.0f * M_PI * 440.0f * i / SAMPLE_RATE));
    }
    
    alBufferData(buffer, AL_FORMAT_MONO16, samples, NUM_SAMPLES * sizeof(short), SAMPLE_RATE);
    
    // Set buffer and play
    audio_source_set_buffer(source, buffer);
    audio_source_play(source);
    
    // Create environmental zone
    float zone_bounds[6] = {-20, 20, -5, 5, -20, 20};
    EnvironmentalZone* zone = audio_create_zone(mixer, zone_bounds);
    if (zone) {
        audio_zone_set_reverb(zone, 0.8f, 0.7f, 2.0f, 0.7f);
    }
    
    // Simulate movement for 10 seconds
    printf("Playing audio test for 10 seconds...\n");
    
    float listener_pos[3] = {0, 0, 0};
    float listener_ori[6] = {0, 0, -1, 0, 1, 0};
    
    for (int i = 0; i < 100; i++) {  // 10 seconds at 10Hz update
        // Move listener in circle
        float angle = i * 0.1f;
        listener_pos[0] = cosf(angle) * 5.0f;
        listener_pos[2] = sinf(angle) * 5.0f;
        
        // Update listener orientation to face source
        float dx = 10.0f - listener_pos[0];
        float dz = 0.0f - listener_pos[2];
        listener_ori[0] = dx;
        listener_ori[2] = dz;
        normalize_vector(listener_ori);
        
        audio_update_listener(mixer, listener_pos, listener_ori);
        
        // Print stats occasionally
        if (i % 10 == 0) {
            printf("[AUDIO] Active sources: %d, CPU: %.1f%%\n", 
                   mixer->active_sources, mixer->cpu_usage);
        }
        
        usleep(100000);  // 100ms
    }
    
    // Cleanup
    alDeleteBuffers(1, &buffer);
    audio_mixer_destroy(mixer);
    
    printf("Audio test completed\n");
    return 0;
}