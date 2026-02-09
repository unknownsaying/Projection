/*******************************************************************************
 * NETWORKED DEPLOYMENT: Multi-Room Synchronized Systems
 * Synchronized projection across multiple control rooms
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define MAX_ROOMS 10
#define SYNC_PORT 8888
#define MAX_BUFFER 4096
#define SYNC_TOLERANCE_MS 16  // ~1 frame at 60Hz

// Network synchronization packet
typedef struct {
    uint32_t sequence_number;
    uint64_t timestamp_ns;
    uint8_t command_type;
    uint32_t data_size;
    uint8_t data[MAX_BUFFER - 20];
    uint32_t checksum;
} SyncPacket;

// Room node in network
typedef struct {
    int room_id;
    char room_name[32];
    struct sockaddr_in address;
    bool is_master;
    bool is_synchronized;
    int64_t time_offset;  // Offset from master in nanoseconds
    double last_sync_time;
    int packet_loss;
} RoomNode;

// Synchronization manager
typedef struct {
    RoomNode rooms[MAX_ROOMS];
    int room_count;
    int master_room_id;
    bool network_active;
    pthread_t sync_thread;
    pthread_t heartbeat_thread;
    int sync_socket;
    struct sockaddr_in broadcast_addr;
    
    // Statistics
    uint32_t total_packets;
    uint32_t dropped_packets;
    double average_latency_ms;
    double max_jitter_ms;
} NetworkSyncManager;

// Display command for synchronization
typedef struct {
    uint8_t display_id;
    uint8_t command;
    uint32_t param1;
    uint32_t param2;
    float float_params[4];
    char content_path[256];
} DisplayCommand;

// Function prototypes
NetworkSyncManager* create_network_manager(int local_room_id, const char* local_room_name);
bool join_network(NetworkSyncManager* manager, const char* master_ip);
void* synchronization_thread(void* arg);
void* heartbeat_thread(void* arg);
bool broadcast_command(NetworkSyncManager* manager, DisplayCommand* cmd);
bool sync_to_master(NetworkSyncManager* manager);
void calculate_time_offsets(NetworkSyncManager* manager);
bool apply_display_command(DisplayCommand* cmd, int room_id);
void handle_packet_loss(NetworkSyncManager* manager, int room_id);

// Create network manager
NetworkSyncManager* create_network_manager(int local_room_id, const char* local_room_name) {
    NetworkSyncManager* manager = malloc(sizeof(NetworkSyncManager));
    if (!manager) return NULL;
    
    memset(manager, 0, sizeof(NetworkSyncManager));
    
    // Initialize local room
    manager->rooms[0].room_id = local_room_id;
    strncpy(manager->rooms[0].room_name, local_room_name, 31);
    manager->rooms[0].is_master = false;
    manager->rooms[0].is_synchronized = false;
    manager->room_count = 1;
    
    // Create UDP socket for synchronization
    manager->sync_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (manager->sync_socket < 0) {
        free(manager);
        return NULL;
    }
    
    // Set socket options for broadcast
    int broadcast_enable = 1;
    setsockopt(manager->sync_socket, SOL_SOCKET, SO_BROADCAST, 
               &broadcast_enable, sizeof(broadcast_enable));
    
    // Configure broadcast address
    memset(&manager->broadcast_addr, 0, sizeof(struct sockaddr_in));
    manager->broadcast_addr.sin_family = AF_INET;
    manager->broadcast_addr.sin_port = htons(SYNC_PORT);
    manager->broadcast_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    
    manager->network_active = false;
    manager->master_room_id = -1;
    manager->total_packets = 0;
    manager->dropped_packets = 0;
    manager->average_latency_ms = 0.0;
    manager->max_jitter_ms = 0.0;
    
    return manager;
}

// Join existing network
bool join_network(NetworkSyncManager* manager, const char* master_ip) {
    printf("[NETWORK] Joining network at %s\n", master_ip);
    
    // Bind to synchronization port
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(SYNC_PORT);
    
    if (bind(manager->sync_socket, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        perror("[NETWORK] Bind failed");
        return false;
    }
    
    // Set receive timeout
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;  // 100ms timeout
    setsockopt(manager->sync_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    // Start synchronization threads
    manager->network_active = true;
    pthread_create(&manager->sync_thread, NULL, synchronization_thread, manager);
    pthread_create(&manager->heartbeat_thread, NULL, heartbeat_thread, manager);
    
    printf("[NETWORK] Network joined successfully\n");
    return true;
}

// Synchronization thread
void* synchronization_thread(void* arg) {
    NetworkSyncManager* manager = (NetworkSyncManager*)arg;
    SyncPacket packet;
    struct sockaddr_in sender_addr;
    socklen_t addr_len = sizeof(sender_addr);
    
    while (manager->network_active) {
        // Receive synchronization packets
        ssize_t received = recvfrom(manager->sync_socket, &packet, sizeof(packet), 0,
                                    (struct sockaddr*)&sender_addr, &addr_len);
        
        if (received > 0) {
            manager->total_packets++;
            
            // Process packet
            uint64_t receive_time = get_nanoseconds();
            uint64_t latency = receive_time - packet.timestamp_ns;
            
            // Update statistics
            double latency_ms = latency / 1000000.0;
            manager->average_latency_ms = 0.9 * manager->average_latency_ms + 0.1 * latency_ms;
            
            // Check if this is from master
            if (packet.command_type == 0x01) {  // Master heartbeat
                // Update master information
                for (int i = 0; i < manager->room_count; i++) {
                    if (manager->rooms[i].is_master) {
                        manager->rooms[i].last_sync_time = get_current_time();
                        break;
                    }
                }
                
                // Calculate time offset
                int64_t current_time = get_nanoseconds();
                int64_t offset = current_time - packet.timestamp_ns;
                
                // Store offset for averaging
                for (int i = 0; i < manager->room_count; i++) {
                    if (manager->rooms[i].room_id == packet.sequence_number) {
                        manager->rooms[i].time_offset = offset;
                        manager->rooms[i].is_synchronized = true;
                        break;
                    }
                }
            }
            
            // Process display commands
            if (packet.command_type == 0x02) {  // Display command
                DisplayCommand* cmd = (DisplayCommand*)packet.data;
                apply_display_command(cmd, manager->rooms[0].room_id);
            }
        }
        
        usleep(1000);  // 1ms sleep to prevent CPU hogging
    }
    
    return NULL;
}

// Heartbeat thread
void* heartbeat_thread(void* arg) {
    NetworkSyncManager* manager = (NetworkSyncManager*)arg;
    SyncPacket heartbeat;
    
    while (manager->network_active) {
        // Send heartbeat if this room is master
        for (int i = 0; i < manager->room_count; i++) {
            if (manager->rooms[i].is_master) {
                heartbeat.sequence_number = manager->rooms[i].room_id;
                heartbeat.timestamp_ns = get_nanoseconds();
                heartbeat.command_type = 0x01;  // Heartbeat
                heartbeat.data_size = 0;
                
                // Calculate checksum (simple XOR for demo)
                heartbeat.checksum = calculate_checksum(&heartbeat);
                
                // Broadcast heartbeat
                sendto(manager->sync_socket, &heartbeat, sizeof(heartbeat), 0,
                       (struct sockaddr*)&manager->broadcast_addr, 
                       sizeof(struct sockaddr_in));
                
                break;
            }
        }
        
        sleep(1);  // Send heartbeat every second
    }
    
    return NULL;
}

// Broadcast display command to all rooms
bool broadcast_command(NetworkSyncManager* manager, DisplayCommand* cmd) {
    if (!manager->network_active) {
        printf("[NETWORK] Network not active\n");
        return false;
    }
    
    SyncPacket packet;
    packet.sequence_number = manager->rooms[0].room_id;
    packet.timestamp_ns = get_nanoseconds();
    packet.command_type = 0x02;  // Display command
    packet.data_size = sizeof(DisplayCommand);
    memcpy(packet.data, cmd, sizeof(DisplayCommand));
    packet.checksum = calculate_checksum(&packet);
    
    // Broadcast to all rooms
    ssize_t sent = sendto(manager->sync_socket, &packet, sizeof(packet), 0,
                          (struct sockaddr*)&manager->broadcast_addr,
                          sizeof(struct sockaddr_in));
    
    if (sent < 0) {
        perror("[NETWORK] Broadcast failed");
        return false;
    }
    
    printf("[NETWORK] Command broadcast to %d rooms\n", manager->room_count);
    return true;
}

// Apply display command locally
bool apply_display_command(DisplayCommand* cmd, int room_id) {
    printf("[ROOM %d] Applying display command: %d\n", room_id, cmd->command);
    
    // Here you would interface with actual display hardware
    // For now, just simulate the action
    
    switch(cmd->command) {
        case 0x10:  // Change content
            printf("  Loading content: %s\n", cmd->content_path);
            break;
        case 0x11:  // Adjust brightness
            printf("  Setting brightness: %.2f\n", cmd->float_params[0]);
            break;
        case 0x12:  // Play video
            printf("  Playing video from: %s\n", cmd->content_path);
            break;
        case 0x13:  // Show hologram
            printf("  Activating hologram with params: %.2f, %.2f, %.2f, %.2f\n",
                   cmd->float_params[0], cmd->float_params[1],
                   cmd->float_params[2], cmd->float_params[3]);
            break;
    }
    
    return true;
}

// Utility functions
uint64_t get_nanoseconds() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

double get_current_time() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

uint32_t calculate_checksum(SyncPacket* packet) {
    uint32_t sum = 0;
    uint8_t* bytes = (uint8_t*)packet;
    
    // Simple XOR checksum for demonstration
    for (size_t i = 0; i < sizeof(SyncPacket) - sizeof(uint32_t); i++) {
        sum ^= bytes[i];
    }
    
    return sum;
}

int main() {
    printf("[NETWORK] Initializing networked deployment system\n");
    
    // Create network manager for this room
    NetworkSyncManager* manager = create_network_manager(101, "Control_Room_A");
    if (!manager) {
        fprintf(stderr, "Failed to create network manager\n");
        return 1;
    }
    
    // Join network (simulate connecting to master at 192.168.1.100)
    if (!join_network(manager, "192.168.1.100")) {
        fprintf(stderr, "Failed to join network\n");
        free(manager);
        return 1;
    }
    
    // Wait for network to stabilize
    sleep(2);
    
    // Example: Broadcast a display command
    DisplayCommand cmd;
    cmd.display_id = 1;
    cmd.command = 0x10;  // Change content
    cmd.param1 = 0;
    cmd.param2 = 0;
    cmd.float_params[0] = 1.0f;
    strcpy(cmd.content_path, "/content/data_visualization.dat");
    
    broadcast_command(manager, &cmd);
    
    // Keep running for demonstration
    printf("[SYSTEM] Network system running. Press Ctrl+C to exit.\n");
    
    // Simulate some activity
    for (int i = 0; i < 10; i++) {
        sleep(2);
        printf("[STATS] Total packets: %u, Latency: %.2fms\n", 
               manager->total_packets, manager->average_latency_ms);
    }
    
    // Cleanup
    manager->network_active = false;
    pthread_join(manager->sync_thread, NULL);
    pthread_join(manager->heartbeat_thread, NULL);
    close(manager->sync_socket);
    free(manager);
    
    return 0;
}