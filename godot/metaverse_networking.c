/*******************************************************************************
 * METAVERSE NETWORKING & MULTIPLAYER
 * Low-latency networking for massive multiplayer Metaverse
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>

// Network Protocol Definitions
#define METAVERSE_PROTOCOL_VERSION 1
#define MAX_PACKET_SIZE 1400  // MTU safe
#define MAX_PLAYERS 1024
#define MAX_ENTITIES_PER_PACKET 64
#define NETWORK_TICK_RATE 60  // Hz
#define SNAPSHOT_INTERVAL 2   // Send snapshot every 2 ticks

// Packet Types
typedef enum {
    PACKET_CONNECT = 0,
    PACKET_DISCONNECT,
    PACKET_ENTITY_UPDATE,
    PACKET_ENTITY_CREATE,
    PACKET_ENTITY_DESTROY,
    PACKET_CHAT_MESSAGE,
    PACKET_VOICE_DATA,
    PACKET_SNAPSHOT,
    PACKET_INPUT,
    PACKET_RPC,
    PACKET_PING,
    PACKET_PONG
} PacketType;

// Network Entity
typedef struct {
    uint64_t entity_id;
    uint32_t owner_id;
    uint8_t entity_type;
    uint32_t flags;
    Vector4 position;
    Vector4 rotation;
    Vector4 velocity;
    uint32_t last_update;
    uint32_t interpolation_time;
} NetworkEntity;

// Network Player
typedef struct {
    uint32_t player_id;
    char username[32];
    struct sockaddr_in address;
    time_t connect_time;
    time_t last_packet_time;
    uint32_t ping;
    uint8_t sequence_number;
    bool authenticated;
    bool connected;
    
    // Input state
    uint32_t input_sequence;
    uint8_t input_state[32];
    
    // Entity ownership
    uint64_t owned_entities[64];
    uint32_t owned_entity_count;
} NetworkPlayer;

// Network Snapshot
typedef struct {
    uint32_t snapshot_id;
    uint32_t timestamp;
    uint32_t entity_count;
    NetworkEntity entities[MAX_ENTITIES_PER_PACKET];
    uint32_t player_count;
    uint32_t player_ids[MAX_PLAYERS / 32];  // Bitmask
} NetworkSnapshot;

// Reliable Message Queue
typedef struct {
    uint16_t sequence;
    uint16_t ack;
    uint32_t ack_bitfield;
    uint8_t packet_type;
    uint8_t data[MAX_PACKET_SIZE];
    uint16_t data_length;
    time_t send_time;
    bool acked;
    uint8_t retry_count;
} ReliablePacket;

// Network Manager
typedef struct {
    // Server/Client state
    bool is_server;
    bool is_connected;
    
    // Socket
    int udp_socket;
    struct sockaddr_in server_address;
    
    // Players
    NetworkPlayer players[MAX_PLAYERS];
    uint32_t player_count;
    uint32_t local_player_id;
    
    // Entities
    NetworkEntity* entities;
    uint32_t entity_count;
    uint32_t entity_capacity;
    
    // Snapshots
    NetworkSnapshot snapshots[64];
    uint32_t snapshot_head;
    uint32_t snapshot_tail;
    
    // Reliable messaging
    ReliablePacket sent_packets[1024];
    uint16_t next_send_sequence;
    uint16_t last_received_sequence;
    
    // Threading
    pthread_t receive_thread;
    pthread_t send_thread;
    pthread_t update_thread;
    bool network_active;
    
    // Statistics
    uint32_t packets_sent;
    uint32_t packets_received;
    uint32_t packets_lost;
    uint32_t bytes_sent;
    uint32_t bytes_received;
    float average_ping;
    float packet_loss_rate;
    
    // Synchronization
    pthread_mutex_t entity_mutex;
    pthread_mutex_t player_mutex;
    pthread_mutex_t network_mutex;
} NetworkManager;

// RPC System
typedef struct {
    uint32_t rpc_id;
    uint32_t source_player;
    uint32_t target_player;  // 0 = broadcast
    char function_name[64];
    uint8_t parameters[256];
    uint16_t parameter_size;
    bool reliable;
    time_t timestamp;
} RPCMessage;

// Voice Chat System
typedef struct {
    uint32_t player_id;
    uint16_t sequence;
    uint32_t timestamp;
    uint8_t codec;  // 0 = Opus, 1 = Speex, 2 = PCM
    uint8_t channels;
    uint16_t sample_rate;
    uint16_t data_size;
    uint8_t audio_data[1200];  // MTU safe
} VoicePacket;

// Function prototypes
NetworkManager* network_manager_create(bool is_server, const char* server_ip, int port);
bool network_manager_connect(NetworkManager* manager);
void network_manager_disconnect(NetworkManager* manager);
void* network_receive_thread(void* arg);
void* network_send_thread(void* arg);
void* network_update_thread(void* arg);
void network_process_packet(NetworkManager* manager, uint8_t* data, int length, 
                           struct sockaddr_in* from_addr);
void network_send_entity_update(NetworkManager* manager, NetworkEntity* entity);
void network_send_snapshot(NetworkManager* manager);
void network_send_rpc(NetworkManager* manager, const char* function_name, 
                     void* parameters, uint16_t param_size, uint32_t target_player);
void network_send_voice(NetworkManager* manager, uint8_t* audio_data, 
                       uint16_t data_size, uint8_t channels, uint16_t sample_rate);
void network_interpolate_entities(NetworkManager* manager);
void network_reconcile_state(NetworkManager* manager);
void network_handle_packet_loss(NetworkManager* manager);

// Create network manager
NetworkManager* network_manager_create(bool is_server, const char* server_ip, int port) {
    NetworkManager* manager = malloc(sizeof(NetworkManager));
    if (!manager) return NULL;
    
    memset(manager, 0, sizeof(NetworkManager));
    
    manager->is_server = is_server;
    manager->is_connected = false;
    manager->network_active = false;
    
    // Create UDP socket
    manager->udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (manager->udp_socket < 0) {
        free(manager);
        return NULL;
    }
    
    // Set socket options
    int broadcast_enable = 1;
    setsockopt(manager->udp_socket, SOL_SOCKET, SO_BROADCAST, 
               &broadcast_enable, sizeof(broadcast_enable));
    
    int reuse_addr = 1;
    setsockopt(manager->udp_socket, SOL_SOCKET, SO_REUSEADDR,
               &reuse_addr, sizeof(reuse_addr));
    
    // Non-blocking mode
    int flags = fcntl(manager->udp_socket, F_GETFL, 0);
    fcntl(manager->udp_socket, F_SETFL, flags | O_NONBLOCK);
    
    // Configure server address
    memset(&manager->server_address, 0, sizeof(struct sockaddr_in));
    manager->server_address.sin_family = AF_INET;
    manager->server_address.sin_port = htons(port);
    
    if (is_server) {
        // Server binds to all interfaces
        manager->server_address.sin_addr.s_addr = htonl(INADDR_ANY);
        
        if (bind(manager->udp_socket, (struct sockaddr*)&manager->server_address,
                sizeof(manager->server_address)) < 0) {
            perror("Bind failed");
            close(manager->udp_socket);
            free(manager);
            return NULL;
        }
    } else {
        // Client connects to specific server
        struct hostent* server = gethostbyname(server_ip);
        if (!server) {
            close(manager->udp_socket);
            free(manager);
            return NULL;
        }
        
        memcpy(&manager->server_address.sin_addr.s_addr,
               server->h_addr, server->h_length);
    }
    
    // Initialize entity storage
    manager->entity_capacity = 1024;
    manager->entity_count = 0;
    manager->entities = malloc(sizeof(NetworkEntity) * manager->entity_capacity);
    
    // Initialize player array
    manager->player_count = 0;
    manager->local_player_id = 0;
    
    // Initialize snapshot buffer
    manager->snapshot_head = 0;
    manager->snapshot_tail = 0;
    
    // Initialize reliable messaging
    manager->next_send_sequence = 0;
    manager->last_received_sequence = 0;
    
    // Initialize statistics
    manager->packets_sent = 0;
    manager->packets_received = 0;
    manager->packets_lost = 0;
    manager->bytes_sent = 0;
    manager->bytes_received = 0;
    manager->average_ping = 0.0f;
    manager->packet_loss_rate = 0.0f;
    
    // Initialize mutexes
    pthread_mutex_init(&manager->entity_mutex, NULL);
    pthread_mutex_init(&manager->player_mutex, NULL);
    pthread_mutex_init(&manager->network_mutex, NULL);
    
    return manager;
}

// Network receive thread
void* network_receive_thread(void* arg) {
    NetworkManager* manager = (NetworkManager*)arg;
    uint8_t buffer[MAX_PACKET_SIZE];
    struct sockaddr_in from_addr;
    socklen_t addr_len = sizeof(from_addr);
    
    while (manager->network_active) {
        // Receive packet
        ssize_t received = recvfrom(manager->udp_socket, buffer, sizeof(buffer), 0,
                                   (struct sockaddr*)&from_addr, &addr_len);
        
        if (received > 0) {
            manager->packets_received++;
            manager->bytes_received += received;
            
            // Process packet
            network_process_packet(manager, buffer, (int)received, &from_addr);
        } else if (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("Receive error");
        }
        
        usleep(1000);  // 1ms to prevent CPU hogging
    }
    
    return NULL;
}

// Network send thread
void* network_send_thread(void* arg) {
    NetworkManager* manager = (NetworkManager*)arg;
    struct timespec last_tick, current_tick;
    clock_gettime(CLOCK_MONOTONIC, &last_tick);
    
    while (manager->network_active) {
        clock_gettime(CLOCK_MONOTONIC, &current_tick);
        
        double elapsed = (current_tick.tv_sec - last_tick.tv_sec) + 
                        (current_tick.tv_nsec - last_tick.tv_nsec) / 1e9;
        
        // Send at network tick rate
        if (elapsed >= 1.0 / NETWORK_TICK_RATE) {
            last_tick = current_tick;
            
            if (manager->is_server) {
                // Server sends snapshots to all clients
                network_send_snapshot(manager);
            } else {
                // Client sends input to server
                network_send_client_input(manager);
            }
            
            // Send reliable packet retries
            network_send_reliable_retries(manager);
            
            // Send ping if needed
            static time_t last_ping = 0;
            time_t now = time(NULL);
            if (now - last_ping >= 1) {  // Every second
                network_send_ping(manager);
                last_ping = now;
            }
        }
        
        usleep(1000);  // 1ms
    }
    
    return NULL;
}

// Process incoming packet
void network_process_packet(NetworkManager* manager, uint8_t* data, int length,
                           struct sockaddr_in* from_addr) {
    if (length < 4) return;  // Minimum packet size
    
    // Parse packet header
    uint8_t protocol_version = data[0];
    uint8_t packet_type = data[1];
    uint16_t sequence = *(uint16_t*)(data + 2);
    
    if (protocol_version != METAVERSE_PROTOCOL_VERSION) {
        printf("Protocol version mismatch\n");
        return;
    }
    
    // Update sequence tracking
    pthread_mutex_lock(&manager->network_mutex);
    
    uint16_t expected_sequence = manager->last_received_sequence + 1;
    if (sequence != expected_sequence) {
        // Packet loss detected
        uint16_t lost_packets = sequence - expected_sequence;
        manager->packets_lost += lost_packets;
        manager->packet_loss_rate = 0.9 * manager->packet_loss_rate + 
                                   0.1 * (lost_packets / (float)sequence);
    }
    
    manager->last_received_sequence = sequence;
    pthread_mutex_unlock(&manager->network_mutex);
    
    // Process based on packet type
    switch (packet_type) {
        case PACKET_CONNECT:
            network_handle_connect(manager, data + 4, length - 4, from_addr);
            break;
            
        case PACKET_DISCONNECT:
            network_handle_disconnect(manager, data + 4, length - 4, from_addr);
            break;
            
        case PACKET_ENTITY_UPDATE:
            network_handle_entity_update(manager, data + 4, length - 4, from_addr);
            break;
            
        case PACKET_SNAPSHOT:
            network_handle_snapshot(manager, data + 4, length - 4, from_addr);
            break;
            
        case PACKET_INPUT:
            network_handle_input(manager, data + 4, length - 4, from_addr);
            break;
            
        case PACKET_CHAT_MESSAGE:
            network_handle_chat(manager, data + 4, length - 4, from_addr);
            break;
            
        case PACKET_VOICE_DATA:
            network_handle_voice(manager, data + 4, length - 4, from_addr);
            break;
            
        case PACKET_RPC:
            network_handle_rpc(manager, data + 4, length - 4, from_addr);
            break;
            
        case PACKET_PING:
            network_handle_ping(manager, data + 4, length - 4, from_addr);
            break;
            
        case PACKET_PONG:
            network_handle_pong(manager, data + 4, length - 4, from_addr);
            break;
    }
}

// Send entity update
void network_send_entity_update(NetworkManager* manager, NetworkEntity* entity) {
    if (!manager->is_connected) return;
    
    uint8_t packet[MAX_PACKET_SIZE];
    
    // Packet header
    packet[0] = METAVERSE_PROTOCOL_VERSION;
    packet[1] = PACKET_ENTITY_UPDATE;
    
    pthread_mutex_lock(&manager->network_mutex);
    *(uint16_t*)(packet + 2) = manager->next_send_sequence++;
    pthread_mutex_unlock(&manager->network_mutex);
    
    // Entity data
    uint8_t* ptr = packet + 4;
    
    *(uint64_t*)ptr = entity->entity_id; ptr += 8;
    *(uint32_t*)ptr = entity->owner_id; ptr += 4;
    *ptr++ = entity->entity_type;
    *(uint32_t*)ptr = entity->flags; ptr += 4;
    
    memcpy(ptr, &entity->position, sizeof(Vector4)); ptr += sizeof(Vector4);
    memcpy(ptr, &entity->rotation, sizeof(Vector4)); ptr += sizeof(Vector4);
    memcpy(ptr, &entity->velocity, sizeof(Vector4)); ptr += sizeof(Vector4);
    
    *(uint32_t*)ptr = (uint32_t)time(NULL);  // timestamp
    
    size_t packet_size = ptr - packet + 4;
    
    // Send packet
    if (manager->is_server) {
        // Broadcast to all connected clients
        for (uint32_t i = 0; i < manager->player_count; i++) {
            if (manager->players[i].connected) {
                sendto(manager->udp_socket, packet, packet_size, 0,
                      (struct sockaddr*)&manager->players[i].address,
                      sizeof(struct sockaddr_in));
            }
        }
    } else {
        // Send to server
        sendto(manager->udp_socket, packet, packet_size, 0,
              (struct sockaddr*)&manager->server_address,
              sizeof(struct sockaddr_in));
    }
    
    manager->packets_sent++;
    manager->bytes_sent += packet_size;
}

// Send snapshot (server only)
void network_send_snapshot(NetworkManager* manager) {
    if (!manager->is_server) return;
    
    static uint32_t snapshot_counter = 0;
    
    // Create snapshot
    NetworkSnapshot snapshot;
    snapshot.snapshot_id = snapshot_counter++;
    snapshot.timestamp = (uint32_t)time(NULL);
    
    pthread_mutex_lock(&manager->entity_mutex);
    
    // Add entities to snapshot
    snapshot.entity_count = 0;
    for (uint32_t i = 0; i < manager->entity_count; i++) {
        if (snapshot.entity_count >= MAX_ENTITIES_PER_PACKET) break;
        
        // Only send entities that have changed recently
        uint32_t now = (uint32_t)time(NULL);
        if (now - manager->entities[i].last_update <= 2) {  // Changed in last 2 seconds
            snapshot.entities[snapshot.entity_count++] = manager->entities[i];
        }
    }
    
    pthread_mutex_unlock(&manager->entity_mutex);
    
    // Add player presence bitmask
    pthread_mutex_lock(&manager->player_mutex);
    
    snapshot.player_count = manager->player_count;
    memset(snapshot.player_ids, 0, sizeof(snapshot.player_ids));
    
    for (uint32_t i = 0; i < manager->player_count; i++) {
        if (manager->players[i].connected) {
            uint32_t word = i / 32;
            uint32_t bit = i % 32;
            snapshot.player_ids[word] |= (1 << bit);
        }
    }
    
    pthread_mutex_unlock(&manager->player_mutex);
    
    // Store snapshot for delta compression
    manager->snapshots[manager->snapshot_head] = snapshot;
    manager->snapshot_head = (manager->snapshot_head + 1) % 64;
    
    // Send snapshot to all clients
    uint8_t packet[MAX_PACKET_SIZE];
    
    packet[0] = METAVERSE_PROTOCOL_VERSION;
    packet[1] = PACKET_SNAPSHOT;
    
    pthread_mutex_lock(&manager->network_mutex);
    *(uint16_t*)(packet + 2) = manager->next_send_sequence++;
    pthread_mutex_unlock(&manager->network_mutex);
    
    // Serialize snapshot
    uint8_t* ptr = packet + 4;
    
    *(uint32_t*)ptr = snapshot.snapshot_id; ptr += 4;
    *(uint32_t*)ptr = snapshot.timestamp; ptr += 4;
    *(uint32_t*)ptr = snapshot.entity_count; ptr += 4;
    
    // Serialize entities
    for (uint32_t i = 0; i < snapshot.entity_count; i++) {
        NetworkEntity* entity = &snapshot.entities[i];
        
        // Delta compression: only send changed fields
        uint8_t change_mask = 0;
        uint8_t* mask_ptr = ptr++;
        
        // Check which fields changed
        // In real implementation, compare with previous state
        
        // For now, send all fields
        change_mask = 0xFF;
        *mask_ptr = change_mask;
        
        *(uint64_t*)ptr = entity->entity_id; ptr += 8;
        *(uint32_t*)ptr = entity->owner_id; ptr += 4;
        *ptr++ = entity->entity_type;
        
        if (change_mask & 0x01) {
            memcpy(ptr, &entity->position, sizeof(Vector4)); 
            ptr += sizeof(Vector4);
        }
        if (change_mask & 0x02) {
            memcpy(ptr, &entity->rotation, sizeof(Vector4)); 
            ptr += sizeof(Vector4);
        }
        if (change_mask & 0x04) {
            memcpy(ptr, &entity->velocity, sizeof(Vector4)); 
            ptr += sizeof(Vector4);
        }
    }
    
    // Send player bitmask
    memcpy(ptr, snapshot.player_ids, sizeof(snapshot.player_ids));
    ptr += sizeof(snapshot.player_ids);
    
    size_t packet_size = ptr - packet;
    
    // Send to all connected clients
    for (uint32_t i = 0; i < manager->player_count; i++) {
        if (manager->players[i].connected) {
            sendto(manager->udp_socket, packet, packet_size, 0,
                  (struct sockaddr*)&manager->players[i].address,
                  sizeof(struct sockaddr_in));
            
            manager->packets_sent++;
            manager->bytes_sent += packet_size;
        }
    }
}

// Entity interpolation (client side)
void network_interpolate_entities(NetworkManager* manager) {
    if (manager->is_server) return;
    
    uint32_t now = (uint32_t)time(NULL);
    
    pthread_mutex_lock(&manager->entity_mutex);
    
    for (uint32_t i = 0; i < manager->entity_count; i++) {
        NetworkEntity* entity = &manager->entities[i];
        
        // Skip entities we own (client-side prediction handles these)
        if (entity->owner_id == manager->local_player_id) continue;
        
        // Check if we have recent enough data
        if (now - entity->last_update > 1000) {  // 1 second old
            // Entity is stale, hide or extrapolate
            continue;
        }
        
        // Simple linear interpolation
        // In real implementation, would use buffered states
        if (entity->interpolation_time > 0) {
            float alpha = (float)(now - entity->last_update) / entity->interpolation_time;
            alpha = fminf(fmaxf(alpha, 0.0f), 1.0f);
            
            // Interpolate position
            // Would need previous and next states
        }
    }
    
    pthread_mutex_unlock(&manager->entity_mutex);
}

// State reconciliation (client-side prediction)
void network_reconcile_state(NetworkManager* manager) {
    if (manager->is_server) return;
    
    // Compare predicted state with server state
    // In case of mismatch, correct and replay inputs
    
    pthread_mutex_lock(&manager->entity_mutex);
    
    for (uint32_t i = 0; i < manager->entity_count; i++) {
        NetworkEntity* entity = &manager->entities[i];
        
        // Only reconcile entities we own
        if (entity->owner_id != manager->local_player_id) continue;
        
        // Get latest server state for this entity
        NetworkEntity* server_entity = find_server_entity(manager, entity->entity_id);
        
        if (server_entity && 
            (vector_distance(entity->position, server_entity->position) > 0.1f ||
             vector_distance(entity->rotation, server_entity->rotation) > 0.01f)) {
            
            // State mismatch detected, need to reconcile
            printf("State mismatch for entity %lu\n", entity->entity_id);
            
            // Correct position
            entity->position = server_entity->position;
            entity->rotation = server_entity->rotation;
            
            // Replay inputs since the diverged tick
            // This would involve rewinding and reapplying player inputs
        }
    }
    
    pthread_mutex_unlock(&manager->entity_mutex);
}

// Voice chat processing
void network_send_voice(NetworkManager* manager, uint8_t* audio_data, 
                       uint16_t data_size, uint8_t channels, uint16_t sample_rate) {
    if (!manager->is_connected) return;
    
    VoicePacket voice;
    voice.player_id = manager->local_player_id;
    voice.sequence = 0;  // Would increment
    voice.timestamp = (uint32_t)time(NULL);
    voice.codec = 0;  // Opus
    voice.channels = channels;
    voice.sample_rate = sample_rate;
    voice.data_size = data_size;
    
    memcpy(voice.audio_data, audio_data, data_size);
    
    // Send voice packet
    uint8_t packet[MAX_PACKET_SIZE];
    
    packet[0] = METAVERSE_PROTOCOL_VERSION;
    packet[1] = PACKET_VOICE_DATA;
    
    pthread_mutex_lock(&manager->network_mutex);
    *(uint16_t*)(packet + 2) = manager->next_send_sequence++;
    pthread_mutex_unlock(&manager->network_mutex);
    
    // Serialize voice packet
    uint8_t* ptr = packet + 4;
    memcpy(ptr, &voice, sizeof(VoicePacket) - sizeof(voice.audio_data) + data_size);
    ptr += sizeof(VoicePacket) - sizeof(voice.audio_data) + data_size;
    
    size_t packet_size = ptr - packet;
    
    // Send
    if (manager->is_server) {
        // Broadcast to all other clients
        for (uint32_t i = 0; i < manager->player_count; i++) {
            if (manager->players[i].connected && 
                manager->players[i].player_id != manager->local_player_id) {
                
                sendto(manager->udp_socket, packet, packet_size, 0,
                      (struct sockaddr*)&manager->players[i].address,
                      sizeof(struct sockaddr_in));
            }
        }
    } else {
        // Send to server
        sendto(manager->udp_socket, packet, packet_size, 0,
              (struct sockaddr*)&manager->server_address,
              sizeof(struct sockaddr_in));
    }
    
    manager->packets_sent++;
    manager->bytes_sent += packet_size;
}

// Start network system
bool network_manager_start(NetworkManager* manager) {
    manager->network_active = true;
    
    // Start threads
    pthread_create(&manager->receive_thread, NULL, network_receive_thread, manager);
    pthread_create(&manager->send_thread, NULL, network_send_thread, manager);
    pthread_create(&manager->update_thread, NULL, network_update_thread, manager);
    
    if (manager->is_server) {
        printf("Metaverse server started on port %d\n", 
               ntohs(manager->server_address.sin_port));
    } else {
        printf("Connecting to metaverse server...\n");
        
        // Send connection request
        network_send_connect(manager);
    }
    
    return true;
}

// Stop network system
void network_manager_stop(NetworkManager* manager) {
    manager->network_active = false;
    
    // Send disconnect notification
    network_send_disconnect(manager);
    
    // Wait for threads
    pthread_join(manager->receive_thread, NULL);
    pthread_join(manager->send_thread, NULL);
    pthread_join(manager->update_thread, NULL);
    
    // Close socket
    close(manager->udp_socket);
    
    printf("Network system stopped\n");
}

// Utility functions
float vector_distance(Vector4 a, Vector4 b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    float dz = a.z - b.z;
    return sqrtf(dx*dx + dy*dy + dz*dz);
}

NetworkEntity* find_server_entity(NetworkManager* manager, uint64_t entity_id) {
    // In real implementation, would have separate server entity buffer
    for (uint32_t i = 0; i < manager->entity_count; i++) {
        if (manager->entities[i].entity_id == entity_id) {
            return &manager->entities[i];
        }
    }
    return NULL;
}

int main_network_test() {
    printf("Metaverse Networking System Test\n");
    
    // Test as server
    printf("Starting as server...\n");
    NetworkManager* server = network_manager_create(true, NULL, 7777);
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        return 1;
    }
    
    network_manager_start(server);
    
    // Run for 30 seconds
    printf("Server running for 30 seconds...\n");
    sleep(30);
    
    // Print statistics
    printf("Server Statistics:\n");
    printf("  Packets sent: %u\n", server->packets_sent);
    printf("  Packets received: %u\n", server->packets_received);
    printf("  Packets lost: %u\n", server->packets_lost);
    printf("  Packet loss rate: %.2f%%\n", server->packet_loss_rate * 100);
    printf("  Bytes sent: %u\n", server->bytes_sent);
    printf("  Bytes received: %u\n", server->bytes_received);
    printf("  Average ping: %.2fms\n", server->average_ping);
    
    network_manager_stop(server);
    free(server->entities);
    free(server);
    
    printf("Network test completed\n");
    return 0;
}