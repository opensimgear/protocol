#include "opensimgear/cyphal.h"
#include "uavcan/node/Heartbeat_1_0.h"
#include "uavcan/node/GetInfo_1_0.h"
#include <string.h>

static void *allocate(canard_mem_t memory,size_t size) {
    osg_cyphal_t *t=memory.context;
    if(size>OSG_CYPHAL_POOL_BLOCK_SIZE || !t->free_count) return NULL;
    return t->pool[t->free_blocks[--t->free_count]].bytes;
}
static void release(canard_mem_t memory,size_t size,void *pointer) {
    (void)size;
    osg_cyphal_t *t=memory.context;
    if(pointer) {
        /* Every resource uses the same aligned fixed-block pool, including RX payload ownership. */
        size_t index=((uint8_t *)pointer-(uint8_t *)t->pool)/sizeof t->pool[0];
        if(index<OSG_CYPHAL_POOL_BLOCK_COUNT && t->free_count<OSG_CYPHAL_POOL_BLOCK_COUNT)
            t->free_blocks[t->free_count++]=(uint16_t)index;
    }
}
static const canard_mem_vtable_t memory_vtable={.alloc=allocate,.free=release};
static canard_us_t current_time(const canard_t *canard) {
    const osg_cyphal_t *t=canard->user_context;
    return (canard_us_t)(t->monotonic_ms*1000U);
}
static bool transmit(canard_t *canard,void *context,canard_us_t deadline,uint_least8_t iface,
                     bool fd,uint32_t id,canard_bytes_t bytes) {
    (void)context;(void)iface;
    osg_cyphal_t *t=canard->user_context;
    if(bytes.size>OSG_GW_FRAME_CAPACITY) return false;
    canard_us_t remaining=deadline-current_time(canard);
    uint32_t remaining_ms=remaining>0 ? (uint32_t)((remaining+999)/1000) : 0;
    osg_gateway_frame_t f={.id=id,.length=(uint8_t)bytes.size,.extended=true,.fd=fd,.brs=fd,
                          .deadline_valid=true,.deadline_ms=t->last_ms+remaining_ms};
    if(bytes.size) memcpy(f.data,bytes.data,bytes.size);
    return t->send(t->send_context,&f);
}
static const canard_vtable_t transport_vtable={.now=current_time,.tx=transmit,.filter=NULL};
static void receive_transfer(canard_subscription_t *sub,canard_us_t timestamp,canard_prio_t priority,
                             uint_least8_t source,uint_least8_t transfer_id,canard_payload_t payload) {
    (void)timestamp;
    osg_cyphal_t *t=sub->user_context;
    if(source<=CANARD_NODE_ID_MAX) {
        if(sub->kind==canard_kind_request && sub->port_id==OSG_CYPHAL_GET_INFO_SERVICE) {
            t->get_info.source=(uint8_t)source;
            t->get_info.transfer_id=(uint8_t)transfer_id;
            t->get_info.priority=priority;
            t->get_info.pending=true;
        } else if(osg_gateway_payload_decode(sub->port_id,payload.view.data,payload.view.size,&t->received)) {
            t->received.source_node=(uint8_t)source;
            t->received_ready=true;
        }
    }
    if(payload.origin.data) release(t->canard.mem.rx_payload,payload.origin.size,payload.origin.data);
}
static const canard_subscription_vtable_t subscription_vtable={.on_message=receive_transfer};
static void advance(osg_cyphal_t *t,uint32_t now_ms) {
    if(!t->clock_started) {
        t->monotonic_ms=now_ms;t->last_ms=now_ms;t->clock_started=true;
    } else {
        /* Call at least once per half uint32 wrap. Main-loop snapshots may precede callback timestamps. */
        uint32_t elapsed=now_ms-t->last_ms;
        if(elapsed<UINT32_C(0x80000000)) {t->monotonic_ms+=elapsed;t->last_ms=now_ms;}
    }
}
static uint64_t seed(const osg_cyphal_t *t) {
    uint64_t value=UINT64_C(14695981039346656037);
    for(size_t i=0;i<sizeof t->identity.bytes;i++) value=(value^t->identity.bytes[i])*UINT64_C(1099511628211);
    return value^t->boot_session;
}
static bool start(osg_cyphal_t *t) {
    t->free_count=OSG_CYPHAL_POOL_BLOCK_COUNT;
    for(unsigned i=0;i<OSG_CYPHAL_POOL_BLOCK_COUNT;i++) t->free_blocks[i]=(uint16_t)i;
    canard_mem_t memory={.vtable=&memory_vtable,.context=t};
    canard_mem_set_t set={.tx_transfer=memory,.tx_frame=memory,.rx_session=memory,.rx_payload=memory,.rx_filters=memory};
    if(!canard_new(&t->canard,&transport_vtable,set,1,OSG_CYPHAL_TX_CAPACITY,seed(t),0)) return false;
    t->canard.user_context=t;
    for(unsigned i=0;i<7;i++) {
        /* Extent of 63 permits FD padding. Payload adapters require the complete known fixed-size fields. */
        if(canard_subscribe_13b(&t->canard,&t->subscriptions[i],(uint16_t)(OSG_CYPHAL_SUBJECT_ANNOUNCE+i),
                               63,CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_us,&subscription_vtable)!=&t->subscriptions[i])
            return false;
        t->subscriptions[i].user_context=t;
    }
    if(canard_subscribe_request(&t->canard,&t->subscriptions[7],OSG_CYPHAL_GET_INFO_SERVICE,0,
                               CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_us,&subscription_vtable)!=&t->subscriptions[7])
        return false;
    t->subscriptions[7].user_context=t;
    t->initialized=true;
    return true;
}
bool osg_cyphal_init(osg_cyphal_t *t,const osg_gateway_identity_t *identity,uint32_t boot_session,
                     const char *name,osg_cyphal_send_fn send,void *context) {
    if(!t || !osg_gateway_identity_valid(identity) || !boot_session || !name || !send) return false;
    size_t name_length=strlen(name);
    if(!name_length || name_length>50) return false;
    for(size_t i=0;i<name_length;i++) {
        char c=name[i];
        if(!((c>='a' && c<='z') || (c>='0' && c<='9') || c=='.' || c=='-' || c=='_')) return false;
    }
    memset(t,0,sizeof *t);
    t->identity=*identity;t->boot_session=boot_session;t->send=send;t->send_context=context;
    memcpy(t->name,name,name_length+1);
    return start(t);
}
static void respond_info(osg_cyphal_t *t) {
    if(!t->get_info.pending) return;
    uavcan_node_GetInfo_Response_1_0 response={0};
    response.protocol_version.major=CANARD_CYPHAL_VERSION_MAJOR;
    response.protocol_version.minor=CANARD_CYPHAL_VERSION_MINOR;
    response.software_version.minor=3;
    memcpy(response.unique_id,t->identity.bytes,sizeof response.unique_id);
    response.name.count=strlen(t->name);
    memcpy(response.name.elements,t->name,response.name.count);
    uint8_t buffer[uavcan_node_GetInfo_Response_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t length=sizeof buffer;
    if(uavcan_node_GetInfo_Response_1_0_serialize_(&response,buffer,&length)>=0) {
        size_t frames=length<=63 ? 1 : (length+2+62)/63;
        canard_bytes_chain_t payload={.bytes={.data=buffer,.size=length},.next=NULL};
        if(t->canard.tx.queue_size+frames<=OSG_CYPHAL_TX_CAPACITY &&
           canard_respond(&t->canard,current_time(&t->canard)+1000000,t->get_info.priority,
                          OSG_CYPHAL_GET_INFO_SERVICE,t->get_info.source,t->get_info.transfer_id,payload,NULL))
            t->get_info.pending=false;
    }
}
bool osg_cyphal_receive(osg_cyphal_t *t,const osg_gateway_frame_t *frame,uint32_t now_ms,
                        osg_gateway_message_t *out) {
    if(!t || !t->initialized || !frame || !out) return false;
    advance(t,now_ms);t->received_ready=false;
    if(!frame->extended || frame->id>UINT32_C(0x1fffffff) || !frame->length ||
       frame->length>(frame->fd ? CANARD_MTU_CAN_FD : CANARD_MTU_CAN_CLASSIC) ||
       canard_dlc_to_len[canard_len_to_dlc[frame->length]]!=frame->length || (!frame->fd && frame->brs)) return false;
    canard_bytes_t bytes={.data=frame->data,.size=frame->length};
    (void)canard_ingest_frame(&t->canard,current_time(&t->canard),0,frame->id,bytes);
    respond_info(t);
    canard_poll(&t->canard,1);
    if(t->received_ready) {*out=t->received;return true;}
    return false;
}
bool osg_cyphal_send(osg_cyphal_t *t,const osg_gateway_message_t *message,uint32_t now_ms) {
    if(!t || !t->initialized) return false;
    advance(t,now_ms);
    uint8_t buffer[63];
    size_t length=osg_gateway_payload_encode(message,buffer,sizeof buffer);
    uint16_t subject=message ? osg_gateway_subject(message->type) : UINT16_MAX;
    if(!length || subject==UINT16_MAX) return false;
    canard_poll(&t->canard,1);
    /* Application admission is lossless under backpressure; do not invoke libcanard's sacrifice policy. */
    size_t mtu=t->canard.tx.fd ? CANARD_MTU_CAN_FD : CANARD_MTU_CAN_CLASSIC;
    size_t frames=length<mtu ? 1 : (length+2+mtu-2)/(mtu-1);
    if(t->canard.tx.queue_size+frames>OSG_CYPHAL_TX_CAPACITY) return false;
    unsigned index=subject-OSG_CYPHAL_SUBJECT_ANNOUNCE;
    canard_bytes_chain_t payload={.bytes={.data=buffer,.size=length},.next=NULL};
    if(!canard_publish_13b(&t->canard,current_time(&t->canard)+100000,1,canard_prio_nominal,
                           subject,t->transfer_ids[index],payload,NULL)) return false;
    t->transfer_ids[index]=(uint8_t)((t->transfer_ids[index]+1)&CANARD_TRANSFER_ID_MAX);
    canard_poll(&t->canard,1);
    return true;
}
void osg_cyphal_tick(osg_cyphal_t *t,uint32_t now_ms) {
    if(!t || !t->initialized) return;
    advance(t,now_ms);
    canard_poll(&t->canard,1);
    respond_info(t);
    if(!t->heartbeat_sent || t->monotonic_ms-t->last_heartbeat_ms>=1000) {
        uavcan_node_Heartbeat_1_0 heartbeat={0};
        uint64_t uptime=t->monotonic_ms/1000;
        heartbeat.uptime=uptime>UINT32_MAX ? UINT32_MAX : (uint32_t)uptime;
        heartbeat.health.value=(t->canard.err.oom || t->canard.err.tx_expiration) ?
            uavcan_node_Health_1_0_ADVISORY : uavcan_node_Health_1_0_NOMINAL;
        heartbeat.mode.value=uavcan_node_Mode_1_0_OPERATIONAL;
        uint8_t buffer[uavcan_node_Heartbeat_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
        size_t length=sizeof buffer;
        if(uavcan_node_Heartbeat_1_0_serialize_(&heartbeat,buffer,&length)>=0 &&
           t->canard.tx.queue_size<OSG_CYPHAL_TX_CAPACITY) {
            canard_bytes_chain_t payload={.bytes={.data=buffer,.size=length},.next=NULL};
            if(canard_publish_13b(&t->canard,current_time(&t->canard)+1000000,1,canard_prio_nominal,
                                 OSG_CYPHAL_HEARTBEAT_SUBJECT,t->transfer_ids[7],payload,NULL)) {
                t->transfer_ids[7]=(uint8_t)((t->transfer_ids[7]+1)&CANARD_TRANSFER_ID_MAX);
                t->heartbeat_sent=true;t->last_heartbeat_ms=t->monotonic_ms;
            }
        }
    }
    canard_poll(&t->canard,1);
}
void osg_cyphal_destroy(osg_cyphal_t *t) {
    if(!t || !t->initialized) return;
    for(unsigned i=0;i<OSG_CYPHAL_SUBSCRIPTION_COUNT;i++) canard_unsubscribe(&t->canard,&t->subscriptions[i]);
    canard_destroy(&t->canard);
    t->initialized=false;t->received_ready=false;t->get_info.pending=false;
}
void osg_cyphal_reset(osg_cyphal_t *t) {
    if(!t || !t->initialized) return;
    uint8_t node_id=(uint8_t)t->canard.node_id;
    osg_cyphal_destroy(t);
    if(start(t)) (void)canard_set_node_id(&t->canard,node_id);
}
uint8_t osg_cyphal_node_id(const osg_cyphal_t *t) {
    return t && t->initialized ? (uint8_t)t->canard.node_id : CANARD_NODE_ID_ANONYMOUS;
}
