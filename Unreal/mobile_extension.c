/*******************************************************************************
 * MOBILE EXTENSION: Companion Apps for Remote Viewing
 * iOS/Android companion apps for remote monitoring and control
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>

#define MOBILE_PORT 9090
#define MAX_MOBILE_CLIENTS 50
#define MAX_FRAME_SIZE 1920*1080*3  // 1080p RGB
#define COMPRESSION_QUALITY 80
#define HEARTBEAT_INTERVAL 5

// Mobile client connection
typedef struct {
    int socket_fd;
    struct sockaddr_in address;
    time_t connect_time;
    time_t last_heartbeat;
    bool authenticated;
    char device_id[64];
    char platform[16];  // "ios", "android", "web"
    int screen_width;
    int screen_height;
    float bandwidth_limit;  // Mbps
    pthread_t client_thread;
    bool active;
} MobileClient;

// Video encoder for mobile streaming
typedef struct {
    int width;
    int height;
    int fps;
    int bitrate;
    int quality;
    void* encoder_context;
    pthread_mutex_t encoder_mutex;
} MobileEncoder;

// Remote control system
typedef struct {
    MobileClient* clients[MAX_MOBILE_CLIENTS];
    int client_count;
    MobileEncoder* encoder;
    int control_socket;
    bool streaming_active;
    pthread_t accept_thread;
    pthread_t stream_thread;
    bool running;
    
    // Statistics
    int total_connections;
    int current_streams;
    double total_data_sent;
    double average_latency;
} MobileExtension;

// Frame data for streaming
typedef struct {
    uint8_t* data;
    size_t size;
    int width;
    int height;
    int frame_number;
    uint64_t timestamp;
    bool keyframe;
} VideoFrame;

// Control commands from mobile
typedef struct {
    uint8_t command_type;
    uint32_t command_id;
    uint32_t data_size;
    uint8_t data[256];
} MobileCommand;

// Function prototypes
MobileExtension* create_mobile_extension();
bool start_mobile_server(MobileExtension* extension);
void* client_accept_thread(void* arg);
void* client_handler_thread(void* arg);
void* video_stream_thread(void* arg);
bool send_video_frame(MobileClient* client, VideoFrame* frame);
bool send_control_data(MobileClient* client, const char* data_type, void* data, size_t size);
bool handle_mobile_command(MobileClient* client, MobileCommand* cmd);
VideoFrame* capture_current_frame(int width, int height);
bool compress_frame(VideoFrame* frame, int quality);
void disconnect_client(MobileExtension* extension, MobileClient* client);

// Create mobile extension system
MobileExtension* create_mobile_extension() {
    MobileExtension* extension = malloc(sizeof(MobileExtension));
    if (!extension) return NULL;
    
    memset(extension, 0, sizeof(MobileExtension));
    
    // Initialize client array
    for (int i = 0; i < MAX_MOBILE_CLIENTS; i++) {
        extension->clients[i] = NULL;
    }
    
    // Create control socket
    extension->control_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (extension->control_socket < 0) {
        free(extension);
        return NULL;
    }
    
    // Set socket options
    int reuse = 1;
    setsockopt(extension->control_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    // Bind to mobile port
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(MOBILE_PORT);
    
    if (bind(extension->control_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(extension->control_socket);
        free(extension);
        return NULL;
    }
    
    // Listen for connections
    if (listen(extension->control_socket, 10) < 0) {
        perror("Listen failed");
        close(extension->control_socket);
        free(extension);
        return NULL;
    }
    
    // Create video encoder
    extension->encoder = malloc(sizeof(MobileEncoder));
    if (extension->encoder) {
        extension->encoder->width = 1920;
        extension->encoder->height = 1080;
        extension->encoder->fps = 30;
        extension->encoder->bitrate = 5000000;  // 5 Mbps
        extension->encoder->quality = COMPRESSION_QUALITY;
        extension->encoder->encoder_context = NULL;  // Would be codec context
        pthread_mutex_init(&extension->encoder->encoder_mutex, NULL);
    }
    
    extension->client_count = 0;
    extension->streaming_active = false;
    extension->running = false;
    extension->total_connections = 0;
    extension->current_streams = 0;
    extension->total_data_sent = 0.0;
    extension->average_latency = 0.0;
    
    printf("[MOBILE] Extension created on port %d\n", MOBILE_PORT);
    return extension;
}

// Start mobile server
bool start_mobile_server(MobileExtension* extension) {
    printf("[MOBILE] Starting mobile extension server\n");
    
    extension->running = true;
    
    // Start accept thread
    pthread_create(&extension->accept_thread, NULL, client_accept_thread, extension);
    
    // Start streaming thread
    pthread_create(&extension->stream_thread, NULL, video_stream_thread, extension);
    
    printf("[MOBILE] Server started. Waiting for mobile connections...\n");
    
    // Print server IP addresses
    print_network_interfaces();
    
    return true;
}

// Thread to accept new client connections
void* client_accept_thread(void* arg) {
    MobileExtension* extension = (MobileExtension*)arg;
    
    while (extension->running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        // Accept new connection
        int client_socket = accept(extension->control_socket, 
                                   (struct sockaddr*)&client_addr, &client_len);
        
        if (client_socket < 0) {
            if (extension->running) {
                perror("Accept failed");
            }
            continue;
        }
        
        // Check if we have room for more clients
        if (extension->client_count >= MAX_MOBILE_CLIENTS) {
            printf("[MOBILE] Maximum clients reached, rejecting connection\n");
            close(client_socket);
            continue;
        }
        
        // Create new client
        MobileClient* client = malloc(sizeof(MobileClient));
        if (!client) {
            close(client_socket);
            continue;
        }
        
        memset(client, 0, sizeof(MobileClient));
        client->socket_fd = client_socket;
        client->address = client_addr;
        client->connect_time = time(NULL);
        client->last_heartbeat = time(NULL);
        client->authenticated = false;
        client->active = true;
        
        // Add to client list
        for (int i = 0; i < MAX_MOBILE_CLIENTS; i++) {
            if (extension->clients[i] == NULL) {
                extension->clients[i] = client;
                extension->client_count++;
                extension->total_connections++;
                break;
            }
        }
        
        // Start client handler thread
        pthread_create(&client->client_thread, NULL, client_handler_thread, client);
        
        printf("[MOBILE] New connection from %s:%d (Total: %d)\n",
               inet_ntoa(client_addr.sin_addr), 
               ntohs(client_addr.sin_port),
               extension->client_count);
    }
    
    return NULL;
}

// Thread to handle individual client communication
void* client_handler_thread(void* arg) {
    MobileClient* client = (MobileClient*)arg;
    char buffer[1024];
    
    // Send welcome message
    const char* welcome = "PROJECTION_SYSTEM_MOBILE_v1.0\n";
    send(client->socket_fd, welcome, strlen(welcome), 0);
    
    while (client->active) {
        // Check for incoming data
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(client->socket_fd, &read_fds);
        
        struct timeval timeout = {1, 0};  // 1 second timeout
        
        int ready = select(client->socket_fd + 1, &read_fds, NULL, NULL, &timeout);
        
        if (ready < 0) {
            perror("Select error");
            break;
        }
        
        if (ready == 0) {
            // Timeout - check heartbeat
            if (time(NULL) - client->last_heartbeat > HEARTBEAT_INTERVAL * 2) {
                printf("[MOBILE] Client timeout, disconnecting\n");
                break;
            }
            continue;
        }
        
        // Read data from client
        ssize_t bytes_read = recv(client->socket_fd, buffer, sizeof(buffer) - 1, 0);
        
        if (bytes_read <= 0) {
            // Client disconnected
            break;
        }
        
        buffer[bytes_read] = '\0';
        
        // Parse command
        if (strncmp(buffer, "AUTH", 4) == 0) {
            // Authentication command
            char device_id[64];
            char platform[16];
            if (sscanf(buffer + 5, "%63s %15s", device_id, platform) == 2) {
                strncpy(client->device_id, device_id, 63);
                strncpy(client->platform, platform, 15);
                client->authenticated = true;
                
                const char* response = "AUTH_OK\n";
                send(client->socket_fd, response, strlen(response), 0);
                
                printf("[MOBILE] Client authenticated: %s (%s)\n", device_id, platform);
            }
        } else if (strncmp(buffer, "HEARTBEAT", 9) == 0) {
            // Heartbeat
            client->last_heartbeat = time(NULL);
            const char* response = "HEARTBEAT_ACK\n";
            send(client->socket_fd, response, strlen(response), 0);
        } else if (strncmp(buffer, "CONTROL", 7) == 0) {
            // Control command
            MobileCommand cmd;
            // Parse command from buffer
            // For demo, just acknowledge
            const char* response = "CONTROL_ACK\n";
            send(client->socket_fd, response, strlen(response), 0);
        } else if (strncmp(buffer, "STREAM_START", 12) == 0) {
            // Start video streaming
            int width, height, fps;
            if (sscanf(buffer + 13, "%d %d %d", &width, &height, &fps) == 3) {
                client->screen_width = width;
                client->screen_height = height;
                
                const char* response = "STREAM_STARTED\n";
                send(client->socket_fd, response, strlen(response), 0);
                
                printf("[MOBILE] Starting stream for %s: %dx%d@%dfps\n",
                       client->device_id, width, height, fps);
            }
        } else if (strncmp(buffer, "STREAM_STOP", 11) == 0) {
            // Stop video streaming
            const char* response = "STREAM_STOPPED\n";
            send(client->socket_fd, response, strlen(response), 0);
        }
    }
    
    // Cleanup client
    client->active = false;
    close(client->socket_fd);
    
    // Find and remove from extension client list
    // (In practice, would need to pass extension reference)
    
    printf("[MOBILE] Client disconnected: %s\n", client->device_id);
    free(client);
    
    return NULL;
}

// Thread to handle video streaming to all clients
void* video_stream_thread(void* arg) {
    MobileExtension* extension = (MobileExtension*)arg;
    int frame_counter = 0;
    
    while (extension->running) {
        // Only stream if we have active clients
        if (extension->client_count == 0) {
            usleep(100000);  // 100ms
            continue;
        }
        
        // Capture current frame (simulated)
        VideoFrame* frame = capture_current_frame(1920, 1080);
        if (!frame) {
            usleep(33333);  // ~30fps
            continue;
        }
        
        frame->frame_number = frame_counter++;
        frame->timestamp = get_current_timestamp();
        frame->keyframe = (frame_counter % 30 == 0);  // Keyframe every 30 frames
        
        // Compress frame
        compress_frame(frame, extension->encoder->quality);
        
        // Send to all connected clients
        for (int i = 0; i < MAX_MOBILE_CLIENTS; i++) {
            MobileClient* client = extension->clients[i];
            if (client && client->authenticated && client->active) {
                send_video_frame(client, frame);
                
                // Update statistics
                extension->total_data_sent += frame->size / 1024.0 / 1024.0;  // MB
                extension->current_streams++;
            }
        }
        
        // Free frame
        free(frame->data);
        free(frame);
        
        // Control frame rate
        usleep(33333);  // ~30fps
    }
    
    return NULL;
}

// Send video frame to mobile client
bool send_video_frame(MobileClient* client, VideoFrame* frame) {
    if (!client || !client->active || !frame) return false;
    
    // Create frame header
    char header[64];
    int header_size = snprintf(header, sizeof(header),
                               "FRAME %d %lu %d %zu %d\n",
                               frame->frame_number,
                               frame->timestamp,
                               frame->keyframe ? 1 : 0,
                               frame->size,
                               frame->width);
    
    // Send header
    if (send(client->socket_fd, header, header_size, 0) != header_size) {
        return false;
    }
    
    // Send frame data
    size_t total_sent = 0;
    while (total_sent < frame->size) {
        ssize_t sent = send(client->socket_fd, 
                           frame->data + total_sent, 
                           frame->size - total_sent, 
                           0);
        
        if (sent <= 0) {
            return false;
        }
        
        total_sent += sent;
    }
    
    return true;
}

// Capture current frame (simulated)
VideoFrame* capture_current_frame(int width, int height) {
    VideoFrame* frame = malloc(sizeof(VideoFrame));
    if (!frame) return NULL;
    
    frame->width = width;
    frame->height = height;
    frame->size = width * height * 3;  // RGB
    frame->data = malloc(frame->size);
    
    if (!frame->data) {
        free(frame);
        return NULL;
    }
    
    // Fill with test pattern (simulated video)
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int offset = (y * width + x) * 3;
            frame->data[offset] = (x + frame->frame_number) % 256;      // R
            frame->data[offset + 1] = (y + frame->frame_number) % 256;  // G
            frame->data[offset + 2] = (x * y + frame->frame_number) % 256; // B
        }
    }
    
    return frame;
}

// Print network interfaces (for mobile connection info)
void print_network_interfaces() {
    struct ifaddrs *ifaddr, *ifa;
    
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return;
    }
    
    printf("[MOBILE] Available network interfaces:\n");
    
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;
        
        int family = ifa->ifa_addr->sa_family;
        
        if (family == AF_INET) {  // IPv4
            struct sockaddr_in* addr = (struct sockaddr_in*)ifa->ifa_addr;
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
            
            printf("  %s: %s\n", ifa->ifa_name, ip);
        } else if (family == AF_INET6) {  // IPv6
            struct sockaddr_in6* addr = (struct sockaddr_in6*)ifa->ifa_addr;
            char ip[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, &addr->sin6_addr, ip, sizeof(ip));
            
            printf("  %s: %s\n", ifa->ifa_name, ip);
        }
    }
    
    freeifaddrs(ifaddr);
}

// Utility functions
uint64_t get_current_timestamp() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;  // microseconds
}

bool compress_frame(VideoFrame* frame, int quality) {
    // In real implementation, this would use libx264, libvpx, etc.
    // For demo, just simulate compression
    printf("[ENCODER] Compressing frame %d (quality: %d)\n", frame->frame_number, quality);
    return true;
}

int main() {
    printf("[MOBILE] Initializing mobile extension system\n");
    
    // Create mobile extension
    MobileExtension* mobile = create_mobile_extension();
    if (!mobile) {
        fprintf(stderr, "Failed to create mobile extension\n");
        return 1;
    }
    
    // Start server
    if (!start_mobile_server(mobile)) {
        fprintf(stderr, "Failed to start mobile server\n");
        free(mobile);
        return 1;
    }
    
    // Run for demonstration
    printf("[MOBILE] System running. Mobile clients can connect on port %d\n", MOBILE_PORT);
    printf("[MOBILE] Press Ctrl+C to stop\n");
    
    // Keep running
    while (mobile->running) {
        sleep(5);
        
        // Print statistics
        printf("[STATS] Clients: %d, Streams: %d, Data sent: %.2f MB\n",
               mobile->client_count,
               mobile->current_streams,
               mobile->total_data_sent);
        
        mobile->current_streams = 0;  // Reset counter
    }
    
    // Cleanup
    printf("[MOBILE] Shutting down mobile extension\n");
    
    mobile->running = false;
    pthread_join(mobile->accept_thread, NULL);
    pthread_join(mobile->stream_thread, NULL);
    
    // Cleanup clients
    for (int i = 0; i < MAX_MOBILE_CLIENTS; i++) {
        if (mobile->clients[i]) {
            free(mobile->clients[i]);
        }
    }
    
    // Cleanup encoder
    if (mobile->encoder) {
        pthread_mutex_destroy(&mobile->encoder->encoder_mutex);
        free(mobile->encoder);
    }
    
    close(mobile->control_socket);
    free(mobile);
    
    return 0;
}