#ifndef OSG_CYPHAL_H
#define OSG_CYPHAL_H
#include "opensimgear/gateway_protocol.h"
#include "canard.h"

/* One physical CAN controller, including daisy-chain sockets. The instance must never be moved while live. */
enum { OSG_CYPHAL_POOL_BLOCK_SIZE=256, OSG_CYPHAL_POOL_BLOCK_COUNT=48,
       OSG_CYPHAL_SESSION_COUNT=192,
       OSG_CYPHAL_SESSION_SIZE=sizeof(void*)>4 ? 160 : 96,
       OSG_CYPHAL_TX_CAPACITY=16, OSG_CYPHAL_HEARTBEAT_SUBJECT=7509,
       OSG_CYPHAL_GET_INFO_SERVICE=430,
       OSG_CYPHAL_APPLICATION_SUBJECT_COUNT=OSG_CYPHAL_SUBJECT_PANEL_EVENT-OSG_CYPHAL_SUBJECT_ANNOUNCE+1,
       OSG_CYPHAL_GET_INFO_SUBSCRIPTION_INDEX=OSG_CYPHAL_APPLICATION_SUBJECT_COUNT,
       OSG_CYPHAL_SUBSCRIPTION_COUNT=OSG_CYPHAL_APPLICATION_SUBJECT_COUNT+1,
       OSG_CYPHAL_HEARTBEAT_TRANSFER_INDEX=OSG_CYPHAL_APPLICATION_SUBJECT_COUNT,
       OSG_CYPHAL_TRANSFER_ID_COUNT=OSG_CYPHAL_APPLICATION_SUBJECT_COUNT+1 };
typedef bool (*osg_cyphal_send_fn)(void *,const osg_gateway_frame_t *);
typedef union { max_align_t alignment; uint8_t bytes[OSG_CYPHAL_POOL_BLOCK_SIZE]; } osg_cyphal_block_t;
typedef union { max_align_t alignment; uint8_t bytes[OSG_CYPHAL_SESSION_SIZE]; } osg_cyphal_session_block_t;
typedef struct {
    canard_t canard;
    canard_subscription_t subscriptions[OSG_CYPHAL_SUBSCRIPTION_COUNT];
    osg_cyphal_block_t pool[OSG_CYPHAL_POOL_BLOCK_COUNT];
    uint16_t free_blocks[OSG_CYPHAL_POOL_BLOCK_COUNT],free_count;
    osg_cyphal_session_block_t session_pool[OSG_CYPHAL_SESSION_COUNT];
    uint16_t free_sessions[OSG_CYPHAL_SESSION_COUNT],session_free_count;
    osg_gateway_identity_t identity;
    uint32_t boot_session,last_ms;
    uint64_t monotonic_ms,last_heartbeat_ms;
    uint8_t transfer_ids[OSG_CYPHAL_TRANSFER_ID_COUNT];
    char name[51];
    osg_cyphal_send_fn send;
    void *send_context;
    osg_gateway_message_t received;
    bool received_ready,initialized,heartbeat_sent,clock_started,panel_events_enabled;
    struct { uint8_t source,transfer_id; canard_prio_t priority; bool pending; } get_info;
} osg_cyphal_t;

bool osg_cyphal_init(osg_cyphal_t *,const osg_gateway_identity_t *,uint32_t boot_session,
                     const char *node_name,osg_cyphal_send_fn,void *);
/* Only the panel stream consumer subscribes. Publishing nodes do not allocate
 * receive sessions for other panels. The subscription survives reset. */
bool osg_cyphal_enable_panel_events(osg_cyphal_t *);
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
