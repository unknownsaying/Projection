/*******************************************************************************
 * CLOUD INTEGRATION: Hybrid Cloud/On-Premise Deployment
 * Mixed cloud and local processing for scalable deployment
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <curl/curl.h>
#include <jansson.h>
#include <openssl/sha.h>

#define CLOUD_ENDPOINT "https://api.projectionsystem.cloud/v1"
#define LOCAL_CACHE_SIZE 100
#define MAX_CONCURRENT_UPLOADS 5
#define CACHE_EXPIRY_SECONDS 3600

// Cloud service types
typedef enum {
    SERVICE_RENDERING = 0,
    SERVICE_STORAGE,
    SERVICE_ANALYTICS,
    SERVICE_STREAMING,
    SERVICE_SYNC
} CloudServiceType;

// Cache entry for hybrid operation
typedef struct {
    char content_id[64];
    unsigned char hash[SHA256_DIGEST_LENGTH];
    time_t last_access;
    time_t expiry_time;
    size_t size;
    void* data;
    bool is_dirty;  // Needs sync to cloud
} CacheEntry;

// Cloud connection manager
typedef struct {
    char api_key[128];
    char session_token[256];
    time_t token_expiry;
    bool cloud_connected;
    bool offline_mode;
    
    // Connection pools
    CURL** curl_handles;
    int curl_handle_count;
    
    // Local cache
    CacheEntry cache[LOCAL_CACHE_SIZE];
    int cache_size;
    pthread_mutex_t cache_mutex;
    
    // Statistics
    long total_upload_bytes;
    long total_download_bytes;
    int failed_requests;
    double average_latency_ms;
} CloudManager;

// Data processing job
typedef struct {
    char job_id[64];
    CloudServiceType service_type;
    json_t* parameters;
    void* input_data;
    size_t input_size;
    void* result_data;
    size_t result_size;
    bool is_complete;
    bool processing_locally;
    pthread_mutex_t job_mutex;
    pthread_cond_t job_cond;
} ProcessingJob;

// Hybrid rendering context
typedef struct {
    CloudManager* cloud;
    ProcessingJob* active_jobs[MAX_CONCURRENT_UPLOADS];
    int job_count;
    bool use_cloud_for_heavy;
    bool use_local_for_realtime;
    pthread_t upload_thread;
    pthread_t download_thread;
    bool running;
} HybridRenderer;

// Function prototypes
CloudManager* create_cloud_manager(const char* api_key);
bool connect_to_cloud(CloudManager* manager);
bool authenticate_cloud(CloudManager* manager);
void* cloud_upload_thread(void* arg);
void* cloud_download_thread(void* arg);
ProcessingJob* create_processing_job(CloudServiceType type, json_t* params);
bool submit_job_to_cloud(CloudManager* manager, ProcessingJob* job);
bool process_locally(ProcessingJob* job);
CacheEntry* find_in_cache(CloudManager* manager, const char* content_id);
void add_to_cache(CloudManager* manager, CacheEntry* entry);
bool sync_cache_to_cloud(CloudManager* manager);
size_t write_callback(void* ptr, size_t size, size_t nmemb, void* userdata);

// Create cloud manager
CloudManager* create_cloud_manager(const char* api_key) {
    CloudManager* manager = malloc(sizeof(CloudManager));
    if (!manager) return NULL;
    
    memset(manager, 0, sizeof(CloudManager));
    strncpy(manager->api_key, api_key, 127);
    manager->cloud_connected = false;
    manager->offline_mode = false;
    
    // Initialize cURL handles
    manager->curl_handle_count = 3;
    manager->curl_handles = malloc(sizeof(CURL*) * manager->curl_handle_count);
    
    for (int i = 0; i < manager->curl_handle_count; i++) {
        manager->curl_handles[i] = curl_easy_init();
        if (!manager->curl_handles[i]) {
            free(manager);
            return NULL;
        }
    }
    
    // Initialize cache
    manager->cache_size = 0;
    pthread_mutex_init(&manager->cache_mutex, NULL);
    
    // Initialize statistics
    manager->total_upload_bytes = 0;
    manager->total_download_bytes = 0;
    manager->failed_requests = 0;
    manager->average_latency_ms = 0.0;
    
    // Initialize cURL globally
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    return manager;
}

// Connect to cloud services
bool connect_to_cloud(CloudManager* manager) {
    printf("[CLOUD] Connecting to cloud services...\n");
    
    // First attempt authentication
    if (!authenticate_cloud(manager)) {
        printf("[CLOUD] Authentication failed, entering offline mode\n");
        manager->offline_mode = true;
        return false;
    }
    
    // Test connection with a simple ping
    CURL* curl = manager->curl_handles[0];
    char url[256];
    snprintf(url, sizeof(url), "%s/ping", CLOUD_ENDPOINT);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, create_auth_header(manager));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    CURLcode res = curl_easy_perform(curl);
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    if (res != CURLE_OK) {
        printf("[CLOUD] Connection test failed: %s\n", curl_easy_strerror(res));
        manager->offline_mode = true;
        return false;
    }
    
    double latency = (end.tv_sec - start.tv_sec) * 1000.0 +
                     (end.tv_nsec - start.tv_nsec) / 1000000.0;
    
    manager->average_latency_ms = latency;
    manager->cloud_connected = true;
    manager->offline_mode = false;
    
    printf("[CLOUD] Connected successfully. Latency: %.2fms\n", latency);
    return true;
}

// Authenticate with cloud
bool authenticate_cloud(CloudManager* manager) {
    printf("[CLOUD] Authenticating...\n");
    
    CURL* curl = manager->curl_handles[0];
    char url[256];
    snprintf(url, sizeof(url), "%s/auth", CLOUD_ENDPOINT);
    
    // Create auth request
    json_t* auth_request = json_object();
    json_object_set_new(auth_request, "api_key", json_string(manager->api_key));
    json_object_set_new(auth_request, "device_id", json_string("projection_system_001"));
    
    char* request_json = json_dumps(auth_request, 0);
    
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_json);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    
    char response_buffer[4096];
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response_buffer);
    
    CURLcode res = curl_easy_perform(curl);
    
    curl_slist_free_all(headers);
    free(request_json);
    json_decref(auth_request);
    
    if (res != CURLE_OK) {
        printf("[CLOUD] Authentication request failed\n");
        return false;
    }
    
    // Parse response
    json_error_t error;
    json_t* response = json_loads(response_buffer, 0, &error);
    
    if (!response) {
        printf("[CLOUD] Failed to parse auth response\n");
        return false;
    }
    
    json_t* token = json_object_get(response, "session_token");
    json_t* expiry = json_object_get(response, "expires_in");
    
    if (token && expiry) {
        strncpy(manager->session_token, json_string_value(token), 255);
        manager->token_expiry = time(NULL) + json_integer_value(expiry);
        printf("[CLOUD] Authentication successful\n");
        
        json_decref(response);
        return true;
    }
    
    json_decref(response);
    return false;
}

// Upload thread for background sync
void* cloud_upload_thread(void* arg) {
    HybridRenderer* renderer = (HybridRenderer*)arg;
    
    while (renderer->running) {
        // Check for dirty cache entries
        pthread_mutex_lock(&renderer->cloud->cache_mutex);
        
        for (int i = 0; i < renderer->cloud->cache_size; i++) {
            if (renderer->cloud->cache[i].is_dirty) {
                // Upload to cloud
                upload_cache_entry(renderer->cloud, &renderer->cloud->cache[i]);
                renderer->cloud->cache[i].is_dirty = false;
            }
        }
        
        pthread_mutex_unlock(&renderer->cloud->cache_mutex);
        
        // Sync jobs to cloud
        for (int i = 0; i < renderer->job_count; i++) {
            if (renderer->active_jobs[i] && 
                !renderer->active_jobs[i]->is_complete &&
                !renderer->active_jobs[i]->processing_locally) {
                
                submit_job_to_cloud(renderer->cloud, renderer->active_jobs[i]);
            }
        }
        
        sleep(5);  // Check every 5 seconds
    }
    
    return NULL;
}

// Download thread for background updates
void* cloud_download_thread(void* arg) {
    HybridRenderer* renderer = (HybridRenderer*)arg;
    
    while (renderer->running) {
        // Check for cloud updates
        if (renderer->cloud->cloud_connected) {
            // Poll for new content
            poll_cloud_for_updates(renderer->cloud);
            
            // Update local cache with new content
            sync_cache_from_cloud(renderer->cloud);
        }
        
        sleep(10);  // Check every 10 seconds
    }
    
    return NULL;
}

// Submit job to cloud or process locally based on type
bool submit_job_hybrid(HybridRenderer* renderer, ProcessingJob* job) {
    // Decision logic: what to process locally vs in cloud
    
    bool process_in_cloud = false;
    
    // Heavy rendering jobs go to cloud
    if (job->service_type == SERVICE_RENDERING) {
        if (job->input_size > 100000000) {  // 100MB threshold
            process_in_cloud = true;
        }
    }
    
    // Real-time jobs stay local
    if (job->service_type == SERVICE_STREAMING) {
        process_in_cloud = false;
    }
    
    // Check cloud connectivity
    if (!renderer->cloud->cloud_connected) {
        process_in_cloud = false;
    }
    
    // Submit job
    if (process_in_cloud) {
        printf("[HYBRID] Submitting job %s to cloud\n", job->job_id);
        job->processing_locally = false;
        return submit_job_to_cloud(renderer->cloud, job);
    } else {
        printf("[HYBRID] Processing job %s locally\n", job->job_id);
        job->processing_locally = true;
        return process_locally(job);
    }
}

// Create processing job
ProcessingJob* create_processing_job(CloudServiceType type, json_t* params) {
    ProcessingJob* job = malloc(sizeof(ProcessingJob));
    if (!job) return NULL;
    
    // Generate unique job ID
    snprintf(job->job_id, sizeof(job->job_id), "job_%ld_%d", 
             time(NULL), rand() % 10000);
    
    job->service_type = type;
    job->parameters = json_incref(params);  // Take ownership
    job->input_data = NULL;
    job->input_size = 0;
    job->result_data = NULL;
    job->result_size = 0;
    job->is_complete = false;
    job->processing_locally = false;
    
    pthread_mutex_init(&job->job_mutex, NULL);
    pthread_cond_init(&job->job_cond, NULL);
    
    return job;
}

// Find content in local cache
CacheEntry* find_in_cache(CloudManager* manager, const char* content_id) {
    pthread_mutex_lock(&manager->cache_mutex);
    
    for (int i = 0; i < manager->cache_size; i++) {
        if (strcmp(manager->cache[i].content_id, content_id) == 0) {
            // Check if entry is expired
            if (time(NULL) > manager->cache[i].expiry_time) {
                // Remove expired entry
                free(manager->cache[i].data);
                for (int j = i; j < manager->cache_size - 1; j++) {
                    manager->cache[j] = manager->cache[j + 1];
                }
                manager->cache_size--;
                pthread_mutex_unlock(&manager->cache_mutex);
                return NULL;
            }
            
            // Update last access time
            manager->cache[i].last_access = time(NULL);
            pthread_mutex_unlock(&manager->cache_mutex);
            return &manager->cache[i];
        }
    }
    
    pthread_mutex_unlock(&manager->cache_mutex);
    return NULL;
}

// Add content to cache
void add_to_cache(CloudManager* manager, CacheEntry* entry) {
    pthread_mutex_lock(&manager->cache_mutex);
    
    // Check if cache is full
    if (manager->cache_size >= LOCAL_CACHE_SIZE) {
        // Remove least recently used entry
        int lru_index = 0;
        time_t oldest = manager->cache[0].last_access;
        
        for (int i = 1; i < manager->cache_size; i++) {
            if (manager->cache[i].last_access < oldest) {
                oldest = manager->cache[i].last_access;
                lru_index = i;
            }
        }
        
        free(manager->cache[lru_index].data);
        
        // Shift entries
        for (int i = lru_index; i < manager->cache_size - 1; i++) {
            manager->cache[i] = manager->cache[i + 1];
        }
        
        manager->cache_size--;
    }
    
    // Add new entry
    manager->cache[manager->cache_size] = *entry;
    manager->cache_size++;
    
    pthread_mutex_unlock(&manager->cache_mutex);
}

int main() {
    printf("[CLOUD] Initializing hybrid cloud/on-premise system\n");
    
    // Initialize cloud manager (using demo API key)
    CloudManager* cloud = create_cloud_manager("demo_api_key_123456");
    if (!cloud) {
        fprintf(stderr, "Failed to create cloud manager\n");
        return 1;
    }
    
    // Connect to cloud
    if (!connect_to_cloud(cloud)) {
        printf("[CLOUD] Starting in offline mode\n");
    }
    
    // Create hybrid renderer
    HybridRenderer renderer;
    memset(&renderer, 0, sizeof(HybridRenderer));
    renderer.cloud = cloud;
    renderer.use_cloud_for_heavy = true;
    renderer.use_local_for_realtime = true;
    renderer.running = true;
    
    // Start background threads
    pthread_create(&renderer.upload_thread, NULL, cloud_upload_thread, &renderer);
    pthread_create(&renderer.download_thread, NULL, cloud_download_thread, &renderer);
    
    // Demo: Create some processing jobs
    printf("[DEMO] Creating sample processing jobs\n");
    
    // Job 1: Heavy rendering (should go to cloud)
    json_t* render_params = json_object();
    json_object_set_new(render_params, "resolution", json_string("8k"));
    json_object_set_new(render_params, "quality", json_string("high"));
    
    ProcessingJob* job1 = create_processing_job(SERVICE_RENDERING, render_params);
    job1->input_size = 150000000;  // 150MB
    
    // Job 2: Real-time streaming (should stay local)
    json_t* stream_params = json_object();
    json_object_set_new(stream_params, "stream_type", json_string("live"));
    json_object_set_new(stream_params, "latency", json_integer(100));
    
    ProcessingJob* job2 = create_processing_job(SERVICE_STREAMING, stream_params);
    
    // Submit jobs
    submit_job_hybrid(&renderer, job1);
    submit_job_hybrid(&renderer, job2);
    
    // Let system run for a bit
    printf("[SYSTEM] Hybrid system running for 30 seconds...\n");
    sleep(30);
    
    // Cleanup
    printf("[SYSTEM] Shutting down hybrid system\n");
    renderer.running = false;
    
    pthread_join(renderer.upload_thread, NULL);
    pthread_join(renderer.download_thread, NULL);
    
    // Cleanup jobs
    if (job1) {
        json_decref(job1->parameters);
        free(job1);
    }
    if (job2) {
        json_decref(job2->parameters);
        free(job2);
    }
    
    // Cleanup cloud
    for (int i = 0; i < cloud->curl_handle_count; i++) {
        curl_easy_cleanup(cloud->curl_handles[i]);
    }
    free(cloud->curl_handles);
    free(cloud);
    
    curl_global_cleanup();
    
    return 0;
}