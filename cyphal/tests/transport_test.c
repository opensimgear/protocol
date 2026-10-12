#include "opensimgear/cyphal.h"
#include "uavcan/node/GetInfo_1_0.h"
#include "uavcan/node/Heartbeat_1_0.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { osg_gateway_frame_t frames[128];size_t count;bool blocked; } queue_t;
static bool emit(void *context,const osg_gateway_frame_t *frame) {
    queue_t *q=context;
    if(q->blocked) return false;
    assert(q->count<sizeof q->frames/sizeof q->frames[0]);
    q->frames[q->count++]=*frame;return true;
}
static osg_gateway_identity_t identity(unsigned value) {
    osg_gateway_identity_t result={{0}};result.bytes[0]=(uint8_t)value;return result;
}
static void init(osg_cyphal_t *t,queue_t *q,unsigned id,const char *name) {
    memset(q,0,sizeof *q);osg_gateway_identity_t uid=identity(id);
    assert(osg_cyphal_init(t,&uid,42,name,emit,q));
    assert(canard_set_node_id(&t->canard,(uint8_t)id));
}
static osg_gateway_message_t message(osg_gateway_message_type_t type,unsigned identity_byte) {
    osg_gateway_message_t m={.type=type,.node=1,.boot_session=42,.lease=9,.sequence=7,.gateway_epoch=65536};
    m.identity=identity(identity_byte);
    m.profile=(osg_gateway_profile_t){.button_count=128,.axis_count=8,.hat_count=4,.revision=1};
    for(unsigned i=0;i<8;i++) {m.profile.axis_usage[i]=(uint8_t)(0x30+i);m.profile.axis_neutral[i]=(uint16_t)(8192*i);}
    osg_gateway_profile_neutral(&m.profile,m.report);
    if(type==OSG_GW_ANNOUNCE) m.node=0;
    return m;
}
static void roundtrip_test(void) {
    static osg_cyphal_t a,b;queue_t qa,qb;init(&a,&qa,1,"org.opensimgear.device");init(&b,&qb,2,"org.opensimgear.gateway");
    for(unsigned type=1;type<=5;type++) {
        osg_gateway_message_t m=message((osg_gateway_message_type_t)type,1),out;
        qa.count=0;assert(osg_cyphal_send(&a,&m,type));assert(qa.count==1);
        osg_gateway_frame_t f=qa.frames[0];
        assert(f.extended && f.fd && f.brs && (f.data[f.length-1]&0xe0)==0xe0);
        assert(f.deadline_valid && f.deadline_ms==type+100);
        assert(osg_cyphal_receive(&b,&f,type,&out));
        assert(out.type==m.type && out.node==m.node && out.source_node==1 && out.boot_session==42);
        if(type==OSG_GW_ANNOUNCE) assert(osg_gateway_profile_equal(&m.profile,&out.profile));
        if(type==OSG_GW_INPUT || type==OSG_GW_INPUT_ACK) assert(!memcmp(m.report,out.report,sizeof m.report));
        assert(!osg_cyphal_receive(&b,&f,type,&out)); /* Transfer-ID duplicate suppression. */
        f.length=9;assert(!osg_cyphal_receive(&b,&f,type,&out));
        f=qa.frames[0];f.extended=false;assert(!osg_cyphal_receive(&b,&f,type,&out));
        f=qa.frames[0];f.data[f.length-1]^=0x80;assert(!osg_cyphal_receive(&b,&f,type,&out));
        uint8_t buffer[64];size_t length=osg_gateway_payload_encode(&m,buffer,sizeof buffer);assert(length>0);
        assert(!osg_gateway_payload_decode(osg_gateway_subject(m.type),buffer,length-1,&out));
    }
    /* Receiver accepts legitimate FD without BRS and Classic CAN multi-frame transfers. */
    osg_gateway_message_t m=message(OSG_GW_INPUT,1),out;qa.count=0;
    assert(osg_cyphal_send(&a,&m,10));qa.frames[0].brs=false;
    assert(osg_cyphal_receive(&b,&qa.frames[0],10,&out));
    a.canard.tx.fd=false;qa.count=0;assert(osg_cyphal_send(&a,&m,11));assert(qa.count==7);
    for(size_t i=0;i<qa.count;i++) assert(osg_cyphal_receive(&b,&qa.frames[i],11,&out)==(i==qa.count-1));
    assert(!memcmp(m.report,out.report,sizeof m.report));
    /* A damaged multi-frame transfer is rejected by the transport CRC. */
    qa.count=0;assert(osg_cyphal_send(&a,&m,12));qa.frames[2].data[1]^=1;
    for(size_t i=0;i<qa.count;i++) assert(!osg_cyphal_receive(&b,&qa.frames[i],12,&out));
    osg_cyphal_destroy(&a);osg_cyphal_destroy(&b);
    assert(a.free_count==OSG_CYPHAL_POOL_BLOCK_COUNT && b.free_count==OSG_CYPHAL_POOL_BLOCK_COUNT);
}
typedef struct { unsigned count;uint8_t source,tid;uavcan_node_GetInfo_Response_1_0 info;uavcan_node_Heartbeat_1_0 heartbeat; } capture_t;
static void capture(canard_subscription_t *sub,canard_us_t ts,canard_prio_t priority,uint_least8_t source,
                    uint_least8_t tid,canard_payload_t payload) {
    (void)ts;(void)priority;capture_t *c=sub->user_context;c->count++;c->source=(uint8_t)source;c->tid=(uint8_t)tid;
    size_t length=payload.view.size;
    if(sub->kind==canard_kind_response) assert(uavcan_node_GetInfo_Response_1_0_deserialize_(&c->info,payload.view.data,&length)>=0);
    else assert(uavcan_node_Heartbeat_1_0_deserialize_(&c->heartbeat,payload.view.data,&length)>=0);
    if(payload.origin.data) sub->owner->mem.rx_payload.vtable->free(sub->owner->mem.rx_payload,payload.origin.size,payload.origin.data);
}
static const canard_subscription_vtable_t capture_vtable={.on_message=capture};
static void standard_services_test(void) {
    static osg_cyphal_t client,server;queue_t qc,qs;
    const char *name="org.opensimgear.generic-device.example.long-name";
    init(&client,&qc,1,"org.opensimgear.client");init(&server,&qs,2,name);
    capture_t info={0},heartbeat={0};canard_subscription_t si,sh;
    assert(canard_subscribe_response(&client.canard,&si,430,100,&capture_vtable)==&si);si.user_context=&info;
    assert(canard_subscribe_13b(&client.canard,&sh,7509,12,2000000,&capture_vtable)==&sh);sh.user_context=&heartbeat;
    osg_cyphal_tick(&server,1234);assert(qs.count==1);osg_gateway_message_t ignored;
    assert(!osg_cyphal_receive(&client,&qs.frames[0],1234,&ignored));
    assert(heartbeat.count==1 && heartbeat.heartbeat.uptime==1 && heartbeat.heartbeat.mode.value==0);
    qs.count=0;
    canard_bytes_chain_t empty={.bytes={.data=NULL,.size=0},.next=NULL};
    assert(canard_request(&client.canard,2000000,canard_prio_nominal,430,2,17,empty,NULL));
    canard_poll(&client.canard,1);assert(qc.count==1);
    assert(!osg_cyphal_receive(&server,&qc.frames[0],1235,&ignored));assert(qs.count==2);
    for(size_t i=0;i<qs.count;i++) assert(!osg_cyphal_receive(&client,&qs.frames[i],1235,&ignored));
    assert(info.count==1 && info.source==2 && info.tid==17);
    assert(info.info.protocol_version.major==1 && info.info.protocol_version.minor==1);
    assert(!memcmp(info.info.unique_id,server.identity.bytes,16));
    assert(info.info.name.count==strlen(name) && !memcmp(info.info.name.elements,name,strlen(name)));
    canard_unsubscribe(&client.canard,&si);canard_unsubscribe(&client.canard,&sh);
    osg_cyphal_destroy(&client);osg_cyphal_destroy(&server);
    assert(client.free_count==OSG_CYPHAL_POOL_BLOCK_COUNT && server.free_count==OSG_CYPHAL_POOL_BLOCK_COUNT);
}
static void backpressure_reset_collision_test(void) {
    static osg_cyphal_t a,b;queue_t qa,qb;init(&a,&qa,1,"org.opensimgear.device");init(&b,&qb,2,"org.opensimgear.gateway");
    osg_gateway_message_t m=message(OSG_GW_INPUT,1),out;qa.blocked=true;
    for(unsigned i=0;i<OSG_CYPHAL_TX_CAPACITY;i++) assert(osg_cyphal_send(&a,&m,0));
    assert(!osg_cyphal_send(&a,&m,0) && a.canard.tx.queue_size==OSG_CYPHAL_TX_CAPACITY);
    assert(a.canard.err.tx_sacrifice==0 && qa.count==0);
    qa.blocked=false;osg_cyphal_tick(&a,1);assert(qa.count==OSG_CYPHAL_TX_CAPACITY+1);
    assert(a.canard.tx.queue_size==0 && a.free_count==OSG_CYPHAL_POOL_BLOCK_COUNT);
    qa.count=0;qa.blocked=true;assert(osg_cyphal_send(&a,&m,2));uint8_t tid=a.transfer_ids[2];
    osg_cyphal_reset(&a);assert(osg_cyphal_node_id(&a)==1 && a.transfer_ids[2]==tid && a.canard.tx.queue_size==0);
    assert(a.free_count==OSG_CYPHAL_POOL_BLOCK_COUNT);qa.blocked=false;osg_cyphal_tick(&a,3);assert(qa.count==0);
    uint16_t available=a.free_count;a.free_count=0;
    assert(!osg_cyphal_send(&a,&m,3) && a.canard.err.oom>0);a.free_count=available;
    /* Collision tracking is independent of the application subscription. */
    assert(canard_set_node_id(&b.canard,1));qb.count=0;osg_cyphal_tick(&b,3);assert(qb.count==1);
    assert(!osg_cyphal_receive(&a,&qb.frames[0],3,&out));
    assert(osg_cyphal_node_id(&a)!=1 && a.canard.err.collision==1);
    osg_cyphal_destroy(&a);osg_cyphal_destroy(&b);
}
static void ten_devices_test(void) {
    static osg_cyphal_t sender,gateway;queue_t qs,qg;init(&sender,&qs,1,"org.opensimgear.device");init(&gateway,&qg,100,"org.opensimgear.gateway");
    osg_gateway_message_t out;
    for(unsigned node=1;node<=10;node++) {
        assert(canard_set_node_id(&sender.canard,(uint8_t)node));
        for(unsigned type=1;type<=5;type++) {
            osg_gateway_message_t m=message((osg_gateway_message_type_t)type,node);qs.count=0;
            assert(osg_cyphal_send(&sender,&m,node));assert(qs.count==1);
            assert(osg_cyphal_receive(&gateway,&qs.frames[0],node,&out) && out.source_node==node);
        }
    }
    assert(gateway.canard.err.oom==0 && gateway.free_count==OSG_CYPHAL_POOL_BLOCK_COUNT-50);
    for(unsigned i=0;i<25;i++) osg_cyphal_tick(&gateway,31011+i);
    assert(gateway.free_count==OSG_CYPHAL_POOL_BLOCK_COUNT);
    osg_cyphal_destroy(&sender);osg_cyphal_destroy(&gateway);
}
static void clock_test(void) {
    static osg_cyphal_t node;queue_t q;init(&node,&q,1,"org.opensimgear.device");
    osg_gateway_message_t m=message(OSG_GW_INPUT,1),out;
    q.blocked=true;assert(osg_cyphal_send(&node,&m,100));
    osg_cyphal_tick(&node,99);assert(node.monotonic_ms==100 && node.canard.err.tx_expiration==0);
    osg_gateway_frame_t malformed={0};assert(!osg_cyphal_receive(&node,&malformed,98,&out));
    assert(node.monotonic_ms==100 && node.last_ms==100);
    osg_cyphal_tick(&node,101);assert(node.monotonic_ms==101);
    osg_cyphal_tick(&node,1201);assert(node.canard.err.tx_expiration>0 && q.count==0);
    osg_cyphal_destroy(&node);init(&node,&q,1,"org.opensimgear.device");
    osg_cyphal_tick(&node,UINT32_MAX-3);uint64_t old=node.monotonic_ms;
    assert(q.count==1 && q.frames[0].deadline_valid && q.frames[0].deadline_ms==996);
    q.count=0;assert(osg_cyphal_send(&node,&m,UINT32_MAX-2));
    assert(q.count==1 && q.frames[0].deadline_ms==97);
    osg_cyphal_tick(&node,5);assert(node.monotonic_ms==old+9 && node.last_ms==5);
    osg_cyphal_destroy(&node);
}
int main(void) {
    roundtrip_test();standard_services_test();backpressure_reset_collision_test();ten_devices_test();clock_test();
    puts("PASS: generated DSDL, real Cyphal FD/Classic transfers, TID/CRC guards, Heartbeat/GetInfo, bounded backpressure/OOM, reset/collision and ten-node aging");
    return 0;
}
