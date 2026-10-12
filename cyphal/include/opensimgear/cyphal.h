#ifndef OSG_CYPHAL_H
#define OSG_CYPHAL_H
#include "opensimgear/gateway_protocol.h"
#include "canard.h"

/* One physical CAN controller, including daisy-chain sockets. The instance must never be moved while live. */
enum { OSG_CYPHAL_POOL_BLOCK_SIZE=256, OSG_CYPHAL_POOL_BLOCK_COUNT=128,
       OSG_CYPHAL_TX_CAPACITY=16, OSG_CYPHAL_HEARTBEAT_SUBJECT=7509,
       OSG_CYPHAL_GET_INFO_SERVICE=430, OSG_CYPHAL_SUBSCRIPTION_COUNT=8 };
typedef bool (*osg_cyphal_send_fn)(void *,const osg_gateway_frame_t *);
typedef union { max_align_t alignment; uint8_t bytes[OSG_CYPHAL_POOL_BLOCK_SIZE]; } osg_cyphal_block_t;
typedef struct {
    canard_t canard;
    canard_subscription_t subscriptions[OSG_CYPHAL_SUBSCRIPTION_COUNT];
    osg_cyphal_block_t pool[OSG_CYPHAL_POOL_BLOCK_COUNT];
    uint16_t free_blocks[OSG_CYPHAL_POOL_BLOCK_COUNT],free_count;
    osg_gateway_identity_t identity;
    uint32_t boot_session,last_ms;
    uint64_t monotonic_ms,last_heartbeat_ms;
    uint8_t transfer_ids[8]; /* Seven application subjects and the standard heartbeat. */
    char name[51];
    osg_cyphal_send_fn send;
    void *send_context;
    osg_gateway_message_t received;
    bool received_ready,initialized,heartbeat_sent,clock_started;
    struct { uint8_t source,transfer_id; canard_prio_t priority; bool pending; } get_info;
} osg_cyphal_t;

bool osg_cyphal_init(osg_cyphal_t *,const osg_gateway_identity_t *,uint32_t boot_session,
                     const char *node_name,osg_cyphal_send_fn,void *);
/* Advances the clock, handles complete transfers and GetInfo; returns true only for an application message. */
bool osg_cyphal_receive(osg_cyphal_t *,const osg_gateway_frame_t *,uint32_t now_ms,osg_gateway_message_t *);
/* True means queued in the bounded transport queue, not delivered to CAN/USB. Driver backpressure is retried by tick. */
bool osg_cyphal_send(osg_cyphal_t *,const osg_gateway_message_t *,uint32_t now_ms);
void osg_cyphal_tick(osg_cyphal_t *,uint32_t now_ms);
/* Purges pending transfers and receive sessions, retaining CAN node-ID, outgoing transfer counters and uptime. */
void osg_cyphal_reset(osg_cyphal_t *);
void osg_cyphal_destroy(osg_cyphal_t *);
uint8_t osg_cyphal_node_id(const osg_cyphal_t *);
#endif
