/* Real device clients and gateway on a deterministic, fault-injected CAN bus.
 * Driver refusal retains TX frames; wire delay never extends TX deadlines.
 * USB completion and persisted registry restoration are portable boundaries. */
#include "opensimgear/gateway.h"
#include "can_client.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

enum { DEVICES=OSG_GW_MAX_MODULES, GATEWAY=DEVICES, QUEUE=4096 };
typedef struct bus bus_t;
typedef struct { bus_t *bus; unsigned sender; } endpoint_t;
typedef struct {
    osg_gateway_frame_t frame;
    unsigned sender;
    uint32_t due;
    uint8_t input_report[OSG_GW_MAX_REPORT_SIZE];
    bool has_ack;
    bool corrupted;
    osg_gateway_delivery_t ack;
} packet_t;
static uint32_t random_next(uint32_t *state) {
    *state^=*state<<13;*state^=*state>>17;*state^=*state<<5;return *state;
}
struct bus {
    osg_gateway_t gateway;
    osg_cyphal_t transport;
    osg_can_client_t clients[DEVICES];
    endpoint_t endpoints[DEVICES+1];
    uint8_t reports[DEVICES][OSG_GW_MAX_REPORT_SIZE];
    packet_t queue[QUEUE];
    unsigned count, drops, delays, reorders, gateway_migrations, completed[DEVICES];
    unsigned fault_sender, fault_subject, drop_remaining, delay_remaining;
    uint32_t delay_ms, now;
    bool reverse, muted[DEVICES];
    bool stress, noise, emitting_ack;
    uint32_t seed, noise_random, trace_hash, event;
    unsigned sender_drops[DEVICES+1],sender_delays[DEVICES+1],stale_usb;
    osg_gateway_delivery_t ack_origin;
    osg_gateway_delivery_t ack_ledger[64];
    unsigned ack_head;
    unsigned sender_duplicates[DEVICES+1],sender_corruptions[DEVICES+1],corruption_types[4];
    unsigned corrupted_subjects[5];
    unsigned refused[DEVICES+1],retried[DEVICES+1],expired[DEVICES+1],last_expired[DEVICES+1];
    unsigned admission_failed[DEVICES+1],max_tx[DEVICES+1],corrupted_delivered;
    unsigned driver_expired[DEVICES+1];
    uint32_t blocked_until[DEVICES+1];
    uint32_t credits_at;
    unsigned credits[DEVICES+1];
    bool retry_pending[DEVICES+1];
    osg_gateway_frame_t retry_frame[DEVICES+1];
};
/* Keep seed, simulated time and event number in every stress failure. */
static void require_stress(const bus_t *b,bool condition,const char *check) {
    if(!condition) {
        fprintf(stderr,"STRESS FAIL seed=0x%08x time=%u event=%u: %s\n",b->seed,b->now,b->event,check);
        abort();
    }
}
#define STRESS_CHECK(b, condition) require_stress((b),(condition),#condition)
static void trace_word(bus_t *b,uint32_t word) {
    /* Fixed byte order for reproducibility across host architectures. */
    for(unsigned i=0;i<4;i++) { b->trace_hash^=(word>>(i*8))&255U;b->trace_hash*=16777619U; }
}
static void deterministic_transport(bus_t *b,osg_cyphal_t *t) {
    if(!b->stress) return;
    /* Pinned libcanard 5.0 mixes its address into the constructor seed and
     * consumes one splitmix64 step for the initial node ID. Remove only that
     * constructor state (also after resets), so ASLR cannot change collision
     * repair. Initial IDs are explicitly set; firmware/vendor code is untouched. */
    uint64_t seed=UINT64_C(14695981039346656037);
    for(unsigned i=0;i<sizeof t->identity.bytes;i++) seed=(seed^t->identity.bytes[i])*UINT64_C(1099511628211);
    seed^=t->boot_session;
    const uint64_t step=UINT64_C(0x9e3779b97f4a7c15);
    if(t->canard.prng_state==(seed^(uintptr_t)&t->canard)+step)
        t->canard.prng_state=(seed^b->seed)+step;
}
static unsigned subject(const osg_gateway_frame_t *f) { return (f->id>>8)&8191U; }
static bool same_frame(const osg_gateway_frame_t *a,const osg_gateway_frame_t *b) {
    return a->id==b->id && a->length==b->length && a->fd==b->fd && a->brs==b->brs &&
           a->extended==b->extended && a->deadline_valid==b->deadline_valid &&
           a->deadline_ms==b->deadline_ms && !memcmp(a->data,b->data,a->length);
}
static bool ack_matches(const osg_gateway_delivery_t *d,const osg_gateway_message_t *m) {
    return d->valid && d->node==m->node && d->lease==m->lease && d->boot_session==m->boot_session &&
           d->sequence==m->sequence && !memcmp(d->report,m->report,sizeof d->report);
}
static bool enqueue_multi(void *context,const osg_gateway_frame_t *frame) {
    endpoint_t *e=context; bus_t *b=e->bus;
    /* Refusal is before wire admission: libcanard must retain its immutable
     * bytes/deadline. Wire loss below happens only after the driver accepts. */
    if(b->stress) {
        STRESS_CHECK(b,frame->deadline_valid);
        const osg_gateway_frame_t *prior=&b->retry_frame[e->sender];
        /* Resets, collision repair and expiration may replace the head. For
         * the same transfer (ID/deadline/tail), require immutable retries. */
        bool retry=b->retry_pending[e->sender] && prior->id==frame->id &&
                   prior->deadline_ms==frame->deadline_ms && prior->length==frame->length &&
                   prior->data[prior->length-1]==frame->data[frame->length-1];
        if(retry) STRESS_CHECK(b,same_frame(frame,prior));
        /* The board accepts frames into its driver queue then drops expired
         * entries. Consume an expired entry here too: refusing it forever at
         * libcanard's inclusive deadline would block newer valid traffic. */
        if((int32_t)(frame->deadline_ms-b->now)<=0) {
            b->driver_expired[e->sender]++;b->retry_pending[e->sender]=false;
            trace_word(b,UINT32_MAX-5U);trace_word(b,e->sender);trace_word(b,b->now);return true;
        }
        if(b->noise && (b->blocked_until[e->sender]>b->now || !b->credits[e->sender])) {
            b->refused[e->sender]++;b->retry_frame[e->sender]=*frame;b->retry_pending[e->sender]=true;
            trace_word(b,UINT32_MAX-2U);trace_word(b,e->sender);trace_word(b,b->now);return false;
        }
        if(b->noise) b->credits[e->sender]--;
        if(retry) b->retried[e->sender]++;
        b->retry_pending[e->sender]=false;
    }
    if(b->stress) {
        trace_word(b,b->now);trace_word(b,e->sender);trace_word(b,frame->id);
        trace_word(b,frame->length);
        for(unsigned i=0;i<frame->length;i++) trace_word(b,frame->data[i]);
    }
    uint32_t noise=b->noise ? random_next(&b->noise_random) : 0;
    if(b->noise && noise%100U<12U) {
        b->drops++;b->sender_drops[e->sender]++;trace_word(b,UINT32_MAX);return true;
    }
    bool match=e->sender==b->fault_sender && subject(frame)==b->fault_subject;
    if(match && b->drop_remaining) { b->drop_remaining--; b->drops++; return true; }
    assert(b->count<QUEUE);
    packet_t p={.frame=*frame,.sender=e->sender,.due=b->now};
    if(e->sender<DEVICES) memcpy(p.input_report,b->reports[e->sender],sizeof p.input_report);
    /* Backpressure may emit an older queued input/ACK while a newer transfer
     * is being admitted. Capture provenance from original bytes, never from
     * today's report or whichever USB callback happens to be running. */
    osg_gateway_message_t original;
    if(b->stress && subject(frame)==OSG_CYPHAL_SUBJECT_INPUT) {
        STRESS_CHECK(b,osg_gateway_payload_decode(subject(frame),frame->data,frame->length-1,&original));
        memcpy(p.input_report,original.report,sizeof p.input_report);
    }
    if(b->stress && e->sender==GATEWAY && subject(frame)==OSG_CYPHAL_SUBJECT_INPUT_ACK) {
        STRESS_CHECK(b,osg_gateway_payload_decode(subject(frame),frame->data,frame->length-1,&original));
        if(b->emitting_ack && ack_matches(&b->ack_origin,&original)) { p.has_ack=true;p.ack=b->ack_origin; }
        for(unsigned i=0;!p.has_ack && i<sizeof b->ack_ledger/sizeof b->ack_ledger[0];i++)
            if(ack_matches(&b->ack_ledger[i],&original)) { p.has_ack=true;p.ack=b->ack_ledger[i]; }
        STRESS_CHECK(b,p.has_ack);
    }
    if(b->noise && noise%100U<40U) {
        p.due+=10U+random_next(&b->noise_random)%391U;
        b->delays++;b->sender_delays[e->sender]++;
    }
    if(match && b->delay_remaining) {
        b->delay_remaining--; p.due+=b->delay_ms; b->delays++;
    }
    if(b->noise && random_next(&b->noise_random)%100U<10U) {
        unsigned kind=random_next(&b->noise_random)%4U;p.corrupted=true;
        b->sender_corruptions[e->sender]++;b->corruption_types[kind]++;
        if(subject(frame)>=OSG_CYPHAL_SUBJECT_ANNOUNCE && subject(frame)<=OSG_CYPHAL_SUBJECT_INPUT_ACK)
            b->corrupted_subjects[subject(frame)-OSG_CYPHAL_SUBJECT_ANNOUNCE]++;
        switch(kind) {
        case 0: p.frame.extended=false;break;
        case 1: p.frame.length=9;break; /* Illegal CAN FD DLC length. */
        case 2: p.frame.data[p.frame.length-1]^=0x20U;break; /* Single-frame toggle must be set. */
        default:
            if(subject(frame)==OSG_CYPHAL_SUBJECT_ANNOUNCE) memset(p.frame.data,0,16); /* Invalid UID. */
            else if(subject(frame)>=OSG_CYPHAL_SUBJECT_ASSIGN && subject(frame)<=OSG_CYPHAL_SUBJECT_INPUT_ACK)
                p.frame.data[0]=0; /* Invalid logical route. */
            else p.frame.data[p.frame.length-1]^=0x20U;
            break;
        }
        trace_word(b,UINT32_MAX-3U);trace_word(b,kind);
    }
    if(b->stress) trace_word(b,p.due);
    b->queue[b->count++]=p;
    if(b->noise && random_next(&b->noise_random)%100U<15U) {
        STRESS_CHECK(b,b->count<QUEUE);
        p.due+=random_next(&b->noise_random)%81U;
        b->sender_duplicates[e->sender]++;trace_word(b,UINT32_MAX-4U);trace_word(b,p.due);
        b->queue[b->count++]=p;
    }
    return true;
}
static void fault(bus_t *b,unsigned sender,unsigned port,unsigned drop,unsigned delay,uint32_t ms) {
    b->fault_sender=sender;b->fault_subject=port;b->drop_remaining=drop;
    b->delay_remaining=delay;b->delay_ms=ms;
}
static void receive_multi(bus_t *b,const packet_t *p) {
    /* CAN broadcast: every other endpoint sees every frame, including traffic
     * unrelated to its application subscriptions and occupancy tracking. */
    if(p->sender!=GATEWAY) {
        osg_gateway_message_t m,reply;
        deterministic_transport(b,&b->transport);
        uint8_t source=osg_cyphal_node_id(&b->transport);
        bool received=osg_cyphal_receive(&b->transport,&p->frame,b->now,&m);
        if(b->stress && p->corrupted) STRESS_CHECK(b,!received);
        if(source!=osg_cyphal_node_id(&b->transport)) {
            b->gateway_migrations++;
            /* Mirror the board loop's gateway source migration boundary. */
            osg_cyphal_reset(&b->transport);
            deterministic_transport(b,&b->transport);
            osg_gateway_bus_reset(&b->gateway);
            osg_gateway_set_running(&b->gateway,true);
        } else if(received) {
            osg_gateway_module_t before={0};
            if(b->stress && m.type==OSG_GW_INPUT) {
                int index=osg_gateway_find_module(&b->gateway,&b->clients[p->sender].identity);
                STRESS_CHECK(b,index>=0);before=b->gateway.modules[index];
                STRESS_CHECK(b,!memcmp(m.report,p->input_report,sizeof m.report));
            }
            osg_gateway_receive_result_t r=osg_gateway_receive(&b->gateway,&m,b->now,&reply);
            if(b->stress && m.type==OSG_GW_INPUT && r==OSG_GW_RX_ACCEPTED) {
                STRESS_CHECK(b,before.online && before.source_node==m.source_node && before.node==m.node);
                STRESS_CHECK(b,before.boot_session==m.boot_session && before.lease==m.lease);
                STRESS_CHECK(b,!before.has_sequence || m.sequence>before.sequence);
            }
            if(r==OSG_GW_RX_ASSIGNMENT && !osg_cyphal_send(&b->transport,&reply,b->now)) {
                if(b->stress) b->admission_failed[GATEWAY]++;else assert(false);
            }
        }
    }
    for(unsigned i=0;i<DEVICES;i++) if(i!=p->sender && !b->muted[i]) {
        deterministic_transport(b,&b->clients[i].cyphal);
        uint32_t prior=b->clients[i].ack_sequence;
        bool acknowledged=osg_can_client_receive(&b->clients[i],&p->frame,b->now);
        /* Assignment application also returns false: inspect the transport's
         * decode flag so corrupt discovery cannot silently alter a route. */
        if(b->stress && p->corrupted) STRESS_CHECK(b,!b->clients[i].cyphal.received_ready);
        if(acknowledged) {
            if(b->stress) {
                STRESS_CHECK(b,!p->corrupted);
                const osg_can_client_t *c=&b->clients[i];
                STRESS_CHECK(b,p->has_ack && osg_gateway_identity_equal(&p->ack.identity,&c->identity));
                STRESS_CHECK(b,c->ack_sequence>prior && c->ack_sequence==p->ack.sequence);
                STRESS_CHECK(b,c->boot==p->ack.boot_session && c->lease==p->ack.lease && c->node==p->ack.node);
                STRESS_CHECK(b,c->gateway_source==(p->frame.id&127U));
                STRESS_CHECK(b,!c->direct && !memcmp(c->acknowledged,p->ack.report,sizeof c->acknowledged));
            }
            b->completed[i]++;
        }
        deterministic_transport(b,&b->clients[i].cyphal);
    }
}
static void pump_multi(bus_t *b) {
    unsigned delivered=0;
    for(;;) {
        int selected=-1;
        for(unsigned i=0;i<b->count;i++) if(b->queue[i].due<=b->now) {
            selected=(int)i; if(!b->reverse) break;
        }
        if(selected<0) break;
        packet_t p=b->queue[selected];
        memmove(&b->queue[selected],&b->queue[selected+1],
                (b->count-(unsigned)selected-1)*sizeof b->queue[0]);
        b->count--; if(b->reverse) b->reorders++;
        assert(++delivered<QUEUE); receive_multi(b,&p);
        if(b->stress && p.corrupted) b->corrupted_delivered++;
    }
}
static void tick_multi(bus_t *b,uint32_t now) {
    assert(now>=b->now); b->now=now;
    if(b->stress && b->credits_at!=now) {
        for(unsigned i=0;i<=DEVICES;i++) b->credits[i]=2;
        b->credits_at=now;
    }
    deterministic_transport(b,&b->transport);
    osg_gateway_tick(&b->gateway,now);osg_cyphal_tick(&b->transport,now);
    for(unsigned i=0;i<DEVICES;i++) if(!b->muted[i]) {
        deterministic_transport(b,&b->clients[i].cyphal);
        osg_can_client_tick(&b->clients[i],now,b->reports[i]);
        deterministic_transport(b,&b->clients[i].cyphal);
    }
    pump_multi(b);
}
static osg_gateway_module_t *module(bus_t *b,unsigned i) {
    int index=osg_gateway_find_module(&b->gateway,&b->clients[i].identity);
    assert(index>=0); return &b->gateway.modules[index];
}
static void check_report(bus_t *b,unsigned i,bool neutral) {
    uint8_t actual[OSG_GW_MAX_REPORT_SIZE],expected[OSG_GW_MAX_REPORT_SIZE];
    int slot=osg_gateway_find_slot(&b->gateway.active,&b->clients[i].identity); assert(slot==(int)i);
    size_t size=osg_gateway_profile_report_size(&b->clients[i].profile);
    assert(osg_gateway_report(&b->gateway,(unsigned)slot,actual,sizeof actual)==size);
    if(neutral) osg_gateway_profile_neutral(&b->clients[i].profile,expected);
    else memcpy(expected,b->reports[i],sizeof expected);
    if(memcmp(actual,expected,size)) {
        fprintf(stderr,"Report mismatch: time=%u device=%u neutral=%u actual=%02x/%02x/%02x expected=%02x/%02x/%02x\n",
                b->now,i,neutral,actual[0],actual[1],actual[2],expected[0],expected[1],expected[2]);
        assert(false);
    }
}
static void init_multi(bus_t *b) {
    memset(b,0,sizeof *b);
    osg_gateway_identity_t gateway_id={{0x80,1}};
    assert(osg_gateway_init(&b->gateway,65536,NULL));
    for(unsigned i=0;i<=DEVICES;i++) b->endpoints[i]=(endpoint_t){b,i};
    assert(osg_cyphal_init(&b->transport,&gateway_id,65536,"org.opensimgear.gateway",
                          enqueue_multi,&b->endpoints[GATEWAY]));
    assert(canard_set_node_id(&b->transport.canard,100));
    for(unsigned i=0;i<DEVICES;i++) {
        osg_gateway_identity_t id={{0x20,(uint8_t)(i+1)}};
        osg_gateway_profile_t profile={.button_count=8,.axis_count=1,.revision=1};
        profile.axis_usage[0]=0x30;profile.axis_neutral[0]=32768;
        assert(osg_can_client_init(&b->clients[i],&id,&profile,1,enqueue_multi,&b->endpoints[i]));
        assert(canard_set_node_id(&b->clients[i].cyphal.canard,(uint8_t)(i+1)));
        b->reports[i][0]=(uint8_t)(i+1);b->reports[i][1]=(uint8_t)(11*i);b->reports[i][2]=0x40;
    }
}
static bool send_ack_multi(bus_t *b,const osg_gateway_message_t *ack,const osg_gateway_delivery_t *origin) {
    b->emitting_ack=true;b->ack_origin=*origin;
    bool accepted=osg_cyphal_send(&b->transport,ack,b->now);
    b->emitting_ack=false;
    if(b->stress) {
        if(accepted) b->ack_ledger[b->ack_head++%(sizeof b->ack_ledger/sizeof b->ack_ledger[0])]=*origin;
        else b->admission_failed[GATEWAY]++;
    }
    return accepted;
}
static void complete_multi(bus_t *b,unsigned i) {
    int slot=osg_gateway_find_slot(&b->gateway.active,&b->clients[i].identity);assert(slot>=0);
    osg_gateway_delivery_t d;uint8_t packet[OSG_GW_MAX_REPORT_SIZE+1];
    size_t size=osg_gateway_delivery_prepare(&b->gateway,(unsigned)slot,packet+1,sizeof packet-1,&d);
    assert(d.valid);packet[0]=(uint8_t)(slot+1);
    osg_gateway_message_t ack;
    assert(osg_gateway_delivery_complete(&b->gateway,&d,packet,size+1,&ack));
    assert(send_ack_multi(b,&ack,&d));
}
static void ready_multi(bus_t *b) {
    init_multi(b);tick_multi(b,0);
    for(unsigned i=0;i<DEVICES;i++) {
        assert(osg_can_client_active(&b->clients[i]));
        assert(osg_gateway_register(&b->gateway,&b->clients[i].identity));
    }
    osg_gateway_apply_layout(&b->gateway);osg_gateway_set_running(&b->gateway,true);
    tick_multi(b,10);
    for(unsigned i=0;i<DEVICES;i++) check_report(b,i,false);
}
static void destroy_multi(bus_t *b) {
    assert(b->count==0);
    for(unsigned i=0;i<DEVICES;i++) {
        osg_cyphal_destroy(&b->clients[i].cyphal);
        if(b->stress) STRESS_CHECK(b,b->clients[i].cyphal.free_count==OSG_CYPHAL_POOL_BLOCK_COUNT);
    }
    osg_cyphal_destroy(&b->transport);
    if(b->stress) STRESS_CHECK(b,b->transport.free_count==OSG_CYPHAL_POOL_BLOCK_COUNT);
}
static osg_gateway_delivery_t pending_multi(bus_t *b,unsigned i,uint8_t packet[OSG_GW_MAX_REPORT_SIZE+1]) {
    osg_gateway_delivery_t d;packet[0]=(uint8_t)(i+1);
    assert(osg_gateway_delivery_prepare(&b->gateway,i,packet+1,OSG_GW_MAX_REPORT_SIZE,&d)==3);
    assert(d.valid);return d;
}
static void usb_ownership_multi(bus_t *b) {
    ready_multi(b);
    uint8_t packet[OSG_GW_MAX_REPORT_SIZE+1];
    osg_gateway_delivery_t pending=pending_multi(b,0,packet);
    fault(b,GATEWAY,OSG_CYPHAL_SUBJECT_INPUT_ACK,0,1,100);complete_multi(b,0);
    fault(b,0,OSG_CYPHAL_SUBJECT_INPUT,0,1,100);tick_multi(b,20);
    uint32_t lease=b->clients[0].lease;
    assert(osg_can_client_usb(&b->clients[0],true));tick_multi(b,21);
    assert(!osg_can_client_active(&b->clients[0]) && module(b,0)->direct_usb);
    check_report(b,0,true);
    osg_gateway_message_t ack;
    assert(!osg_gateway_delivery_complete(&b->gateway,&pending,packet,4,&ack));
    osg_gateway_delivery_t neutral;
    uint8_t neutral_report[OSG_GW_MAX_REPORT_SIZE];
    assert(osg_gateway_delivery_prepare(&b->gateway,0,neutral_report,sizeof neutral_report,&neutral)==3 && !neutral.valid);
    for(uint32_t t=30;t<=130;t+=10) tick_multi(b,t);
    assert(b->completed[0]==0 && b->clients[0].ack_sequence==0);
    check_report(b,0,true);
    for(unsigned i=1;i<DEVICES;i++) { check_report(b,i,false);complete_multi(b,i); }
    pump_multi(b);
    /* Release ownership, but lose the first fresh input: gateway stays neutral
     * until another input arrives, and cannot ACK a pre-ownership snapshot. */
    fault(b,0,OSG_CYPHAL_SUBJECT_INPUT,1,0,0);
    assert(osg_can_client_usb(&b->clients[0],false));tick_multi(b,131);
    assert(osg_can_client_active(&b->clients[0]) && !module(b,0)->direct_usb);
    assert(b->clients[0].lease==lease);check_report(b,0,true);
    assert(!osg_gateway_delivery_complete(&b->gateway,&pending,packet,4,&ack));
    tick_multi(b,141);check_report(b,0,false);complete_multi(b,0);pump_multi(b);
    assert(b->completed[0]==1 && b->clients[0].ack_sequence==module(b,0)->input_sequence);
    for(unsigned i=1;i<DEVICES;i++) assert(b->completed[i]==1);
    destroy_multi(b);
    puts("PASS: direct USB takeover/release on a ten-device bus; stale input/ACK and USB completion blocked, fresh input required, other devices unaffected");
}
static void collision_multi(bus_t *b,bool gateway_collision) {
    ready_multi(b);
    unsigned victim=gateway_collision ? GATEWAY : 0, newcomer=1;
    uint8_t source=gateway_collision ? osg_cyphal_node_id(&b->transport) : osg_cyphal_node_id(&b->clients[victim].cyphal);
    uint32_t leases[DEVICES];for(unsigned i=0;i<DEVICES;i++) leases[i]=b->clients[i].lease;
    uint32_t generation=b->clients[0].generation;
    uint8_t packet[OSG_GW_MAX_REPORT_SIZE+1];
    osg_gateway_delivery_t pending=pending_multi(b,0,packet);
    /* Gateway migration is learned by clients after their current assignment
     * expires. Retain old traffic until after that rediscovery boundary. */
    uint32_t delay=gateway_collision ? 2500U : 100U;
    fault(b,GATEWAY,OSG_CYPHAL_SUBJECT_INPUT_ACK,0,1,delay);complete_multi(b,0);
    fault(b,0,OSG_CYPHAL_SUBJECT_INPUT,0,1,delay);tick_multi(b,20);
    /* A rebooted endpoint appears at an already occupied transport address.
     * Emit a real standard Heartbeat; libcanard must detect and repair it. */
    osg_gateway_identity_t id=b->clients[newcomer].identity;
    osg_gateway_profile_t profile=b->clients[newcomer].profile;
    osg_cyphal_destroy(&b->clients[newcomer].cyphal);
    assert(osg_can_client_init(&b->clients[newcomer],&id,&profile,2,enqueue_multi,&b->endpoints[newcomer]));
    assert(canard_set_node_id(&b->clients[newcomer].cyphal.canard,source));
    b->now=21;osg_cyphal_tick(&b->clients[newcomer].cyphal,21);pump_multi(b);
    if(gateway_collision) {
        assert(b->gateway_migrations==1 && osg_cyphal_node_id(&b->transport)!=source);
        for(unsigned i=0;i<DEVICES;i++) { assert(!module(b,i)->lease);check_report(b,i,true); }
    } else {
        assert(b->clients[victim].generation>generation);
        assert(osg_cyphal_node_id(&b->clients[victim].cyphal)!=source);
        assert(!osg_can_client_active(&b->clients[victim]) && b->clients[victim].ack_sequence==0);
    }
    b->reports[0][0]=0x81; /* Retained input has different bytes from fresh input. */
    /* Discover under the repaired source before retained old traffic arrives. */
    for(uint32_t t=22;t<=132;t+=10) tick_multi(b,t);
    assert(b->completed[0]==0 && b->clients[0].ack_sequence==0);
    osg_gateway_message_t ack;
    assert(!osg_gateway_delivery_complete(&b->gateway,&pending,packet,4,&ack));
    for(uint32_t t=142;t<=3142;t+=10) {
        tick_multi(b,t);
        if(module(b,0)->has_input) check_report(b,0,false);
    }
    for(unsigned i=0;i<DEVICES;i++) {
        assert(osg_can_client_active(&b->clients[i]));check_report(b,i,false);
        assert(module(b,i)->source_node==osg_cyphal_node_id(&b->clients[i].cyphal));
        assert(b->clients[i].gateway_source==osg_cyphal_node_id(&b->transport));
        assert(osg_cyphal_node_id(&b->clients[i].cyphal)!=osg_cyphal_node_id(&b->transport));
        for(unsigned j=0;j<i;j++) assert(osg_cyphal_node_id(&b->clients[i].cyphal)!=osg_cyphal_node_id(&b->clients[j].cyphal));
        if(gateway_collision || i==victim || i==newcomer) assert(b->clients[i].lease>leases[i]);
        else assert(b->clients[i].lease==leases[i]);
        complete_multi(b,i);
    }
    pump_multi(b);
    for(unsigned i=0;i<DEVICES;i++) assert(b->completed[i]==1 && b->clients[i].ack_sequence==module(b,i)->input_sequence);
    destroy_multi(b);
    puts(gateway_collision ? "PASS: gateway CAN node-ID collision; all leases revoked, stable slots, source-bound rediscovery and messaging recovery" :
                            "PASS: device CAN node-ID collision; automatic address repair, renewed leases, stale delivery rejection and isolated recovery");
}
/* Each seed defines a complete fault schedule; a separate PRNG drives wire
 * impairment so event selection does not depend on how many frames are sent. */
typedef struct {
    osg_gateway_delivery_t delivery;
    uint8_t packet[OSG_GW_MAX_REPORT_SIZE+1];
    uint32_t due;
    bool pending;
} stress_usb_t;
static void reboot_stress_client(bus_t *b,unsigned i,uint8_t source) {
    osg_gateway_identity_t id=b->clients[i].identity;
    osg_gateway_profile_t profile=b->clients[i].profile;
    uint32_t boot=b->clients[i].boot+1;bool direct=b->clients[i].direct;
    osg_cyphal_destroy(&b->clients[i].cyphal);
    STRESS_CHECK(b,osg_can_client_init(&b->clients[i],&id,&profile,boot,enqueue_multi,&b->endpoints[i]));
    STRESS_CHECK(b,canard_set_node_id(&b->clients[i].cyphal.canard,source));
    if(direct) STRESS_CHECK(b,osg_can_client_usb(&b->clients[i],true));
    deterministic_transport(b,&b->clients[i].cyphal);
}
static void reboot_stress_gateway(bus_t *b) {
    uint8_t record[OSG_GW_REGISTRY_RECORD_SIZE];osg_gateway_registry_t restored;
    STRESS_CHECK(b,osg_gateway_registry_encode(&b->gateway.active,record,sizeof record)==sizeof record);
    STRESS_CHECK(b,osg_gateway_registry_decode(record,sizeof record,&restored));
    uint32_t epoch=b->gateway.epoch+65536U;
    osg_gateway_identity_t id=b->transport.identity;uint8_t source=osg_cyphal_node_id(&b->transport);
    osg_cyphal_destroy(&b->transport);
    STRESS_CHECK(b,osg_gateway_init(&b->gateway,epoch,&restored));
    osg_gateway_set_running(&b->gateway,true);
    STRESS_CHECK(b,osg_cyphal_init(&b->transport,&id,epoch,"org.opensimgear.gateway",enqueue_multi,&b->endpoints[GATEWAY]));
    STRESS_CHECK(b,canard_set_node_id(&b->transport.canard,source));
    deterministic_transport(b,&b->transport);
}
static void invariants_stress(bus_t *b) {
    STRESS_CHECK(b,b->count<QUEUE);
    for(unsigned i=0;i<DEVICES;i++) {
        osg_gateway_module_t *m=module(b,i);
        STRESS_CHECK(b,osg_gateway_find_slot(&b->gateway.active,&b->clients[i].identity)==(int)i);
        STRESS_CHECK(b,osg_gateway_find_slot(&b->gateway.pending,&b->clients[i].identity)==(int)i);
        STRESS_CHECK(b,b->clients[i].cyphal.free_count<=OSG_CYPHAL_POOL_BLOCK_COUNT);
        STRESS_CHECK(b,b->clients[i].cyphal.canard.tx.queue_size<=OSG_CYPHAL_TX_CAPACITY);
        if(m->online) {
            STRESS_CHECK(b,m->node && m->lease && m->lease>b->gateway.epoch);
            for(unsigned j=0;j<i;j++) if(module(b,j)->online) {
                STRESS_CHECK(b,m->node!=module(b,j)->node);
                STRESS_CHECK(b,m->source_node!=module(b,j)->source_node);
            }
        }
        if(!m->online || m->direct_usb || !m->has_input) check_report(b,i,true);
        else {
            uint8_t report[OSG_GW_MAX_REPORT_SIZE];
            STRESS_CHECK(b,osg_gateway_report(&b->gateway,i,report,sizeof report)==3);
            /* High axis byte identifies the emitter independently of routing;
             * delayed legitimate reports may contain older low-byte values. */
            STRESS_CHECK(b,report[2]==0x40U+i);
        }
    }
    STRESS_CHECK(b,b->transport.free_count<=OSG_CYPHAL_POOL_BLOCK_COUNT);
    STRESS_CHECK(b,b->transport.canard.tx.queue_size<=OSG_CYPHAL_TX_CAPACITY);
    for(unsigned i=0;i<=DEVICES;i++) {
        const osg_cyphal_t *t=i==GATEWAY ? &b->transport : &b->clients[i].cyphal;
        unsigned expired=(unsigned)t->canard.err.tx_expiration;
        b->expired[i]+=expired>=b->last_expired[i] ? expired-b->last_expired[i] : expired;
        b->last_expired[i]=expired;
        if(t->canard.tx.queue_size>b->max_tx[i]) b->max_tx[i]=(unsigned)t->canard.tx.queue_size;
    }
}
static void saturate_stress(bus_t *b) {
    /* A deterministic saturation prelude verifies every bounded transport,
     * before randomly interleaving pressure with the other faults. */
    for(unsigned i=0;i<=DEVICES;i++) b->blocked_until[i]=b->now+150U;
    for(unsigned i=0;i<DEVICES;i++) {
        osg_can_client_t *c=&b->clients[i];
        for(unsigned attempt=0;attempt<40;attempt++) {
            unsigned depth=(unsigned)c->cyphal.canard.tx.queue_size;
            uint32_t sequence=c->next_sequence,dropped=c->tx_dropped;
            c->input_due=true;osg_can_client_tick(c,b->now,b->reports[i]);
            if(depth==OSG_CYPHAL_TX_CAPACITY) STRESS_CHECK(b,c->next_sequence==sequence);
            b->admission_failed[i]+=c->tx_dropped-dropped;
        }
        STRESS_CHECK(b,c->cyphal.canard.tx.queue_size==OSG_CYPHAL_TX_CAPACITY && b->admission_failed[i]>0);
    }
    for(unsigned attempt=0;attempt<40;attempt++) {
        unsigned i=attempt%DEVICES;uint8_t packet[OSG_GW_MAX_REPORT_SIZE+1];
        osg_gateway_delivery_t d=pending_multi(b,i,packet);osg_gateway_message_t ack;
        STRESS_CHECK(b,osg_gateway_delivery_complete(&b->gateway,&d,packet,4,&ack));
        (void)send_ack_multi(b,&ack,&d);
    }
    STRESS_CHECK(b,b->transport.canard.tx.queue_size==OSG_CYPHAL_TX_CAPACITY && b->admission_failed[GATEWAY]>0);
    invariants_stress(b);
}
static void usb_stress(bus_t *b,stress_usb_t usb[DEVICES],uint32_t *random) {
    for(unsigned i=0;i<DEVICES;i++) {
        stress_usb_t *u=&usb[i];
        if(u->pending && u->due<=b->now) {
            osg_gateway_message_t ack;
            const osg_gateway_module_t *current=module(b,i);
            bool invalid=!u->delivery.valid || !current->online || current->direct_usb || !current->has_input ||
                u->delivery.run_generation!=b->gateway.run_generation ||
                u->delivery.delivery_generation!=current->delivery_generation ||
                u->delivery.boot_session!=current->boot_session || u->delivery.lease!=current->lease;
            bool completed=osg_gateway_delivery_complete(&b->gateway,&u->delivery,u->packet,4,&ack);
            STRESS_CHECK(b,!invalid || !completed);
            if(completed) {
                osg_gateway_module_t *m=module(b,i);
                STRESS_CHECK(b,m->online && !m->direct_usb && m->has_input);
                STRESS_CHECK(b,m->lease==u->delivery.lease && m->boot_session==u->delivery.boot_session);
                STRESS_CHECK(b,ack.sequence==u->delivery.sequence && !memcmp(ack.report,u->packet+1,3));
                (void)send_ack_multi(b,&ack,&u->delivery);
            } else if(u->delivery.valid) b->stale_usb++;
            u->pending=false;
        }
        if(!u->pending && random_next(random)%4U==0) {
            size_t length=osg_gateway_delivery_prepare(&b->gateway,i,u->packet+1,OSG_GW_MAX_REPORT_SIZE,&u->delivery);
            STRESS_CHECK(b,length==3);u->packet[0]=(uint8_t)(i+1);
            u->pending=true;u->due=b->now+10U+random_next(random)%191U;
        }
    }
    pump_multi(b);
}
static uint32_t randomized_stress(bus_t *b,uint32_t seed) {
    ready_multi(b);
    for(unsigned i=0;i<DEVICES;i++) b->reports[i][2]=(uint8_t)(0x40U+i);
    tick_multi(b,20);
    b->stress=true;b->noise=true;b->seed=seed;
    b->noise_random=seed^UINT32_C(0xa5a5a5a5);b->trace_hash=2166136261U;
    uint32_t random=seed;
    stress_usb_t usb[DEVICES]={0};
    unsigned reboots[DEVICES]={0},collisions[DEVICES]={0},ownership[DEVICES]={0},gateway_reboots=0;
    uint32_t usb_until[DEVICES]={0};
    unsigned order[DEVICES];for(unsigned i=0;i<DEVICES;i++) order[i]=i;
    for(unsigned i=DEVICES-1;i>0;i--) {
        unsigned j=random_next(&random)%(i+1),tmp=order[i];order[i]=order[j];order[j]=tmp;
    }
    printf("STRESS seed=0x%08x: 60 s mixed faults, 35 s bounded recovery\n",seed);fflush(stdout);
    saturate_stress(b);
    for(unsigned step=1;step<=6000;step++) {
        b->now=20U+step*10U;
        for(unsigned i=0;i<DEVICES;i++) if(usb_until[i] && usb_until[i]<=b->now) {
            if(b->clients[i].direct) {
                STRESS_CHECK(b,osg_can_client_usb(&b->clients[i],false));ownership[i]++;
                trace_word(b,b->now);trace_word(b,i);trace_word(b,UINT32_MAX-1U);
            }
            usb_until[i]=0;
        }
        /* Every device gets each requested state fault in a shuffled initial
         * sweep, followed by randomly interleaved events on arbitrary devices. */
        if(step%100U==0) {
            unsigned event=step/100U-1;b->event=event;
            unsigned i=event<30U ? order[event%DEVICES] : random_next(&random)%DEVICES;
            unsigned action=event<30U ? event/DEVICES : random_next(&random)%4U;
            trace_word(b,event);trace_word(b,i);trace_word(b,action);
            unsigned driver=event%(DEVICES+1U);
            b->blocked_until[driver]=b->now+50U+random_next(&random)%201U;
            trace_word(b,driver);trace_word(b,b->blocked_until[driver]);
            if(action==0) {
                STRESS_CHECK(b,osg_can_client_usb(&b->clients[i],!b->clients[i].direct));ownership[i]++;
                usb_until[i]=b->clients[i].direct ? b->now+250U+random_next(&random)%951U : 0;
            } else if(action==1) {
                reboot_stress_client(b,i,osg_cyphal_node_id(&b->clients[i].cyphal));reboots[i]++;
            } else if(action==2) {
                unsigned other=(i+1U+random_next(&random)%(DEVICES-1U))%DEVICES;
                uint8_t source=event%3U==0 ? osg_cyphal_node_id(&b->transport) : osg_cyphal_node_id(&b->clients[other].cyphal);
                reboot_stress_client(b,i,source);reboots[i]++;collisions[i]++;
                /* Immediate Heartbeat uses real libcanard frames, with the same
                 * loss/delay as other traffic; eventual detection is observed. */
                osg_cyphal_tick(&b->clients[i].cyphal,b->now);
            } else { reboot_stress_gateway(b);gateway_reboots++; }
        }
        for(unsigned i=0;i<DEVICES;i++) {
            b->reports[i][0]=(uint8_t)random_next(&random);
            b->reports[i][1]=(uint8_t)step;b->reports[i][2]=(uint8_t)(0x40U+i);
        }
        b->reverse=(random_next(&random)&1U)!=0;
        tick_multi(b,b->now);usb_stress(b,usb,&random);invariants_stress(b);
    }
    for(unsigned i=0;i<DEVICES;i++) {
        STRESS_CHECK(b,reboots[i]>0 && collisions[i]>0 && ownership[i]>=2);
        STRESS_CHECK(b,b->sender_drops[i]>0 && b->sender_delays[i]>0 && b->completed[i]>0);
    }
    STRESS_CHECK(b,b->sender_drops[GATEWAY]>0 && b->sender_delays[GATEWAY]>0 && b->stale_usb>0);
    STRESS_CHECK(b,gateway_reboots>0 && b->gateway_migrations>0);
    STRESS_CHECK(b,b->drops>0 && b->delays>0 && b->reorders>0);
    unsigned retries=0;
    for(unsigned i=0;i<=DEVICES;i++) {
        STRESS_CHECK(b,b->sender_duplicates[i]>0 && b->sender_corruptions[i]>0);
        STRESS_CHECK(b,b->refused[i]>0 && b->admission_failed[i]>0);
        STRESS_CHECK(b,b->max_tx[i]==OSG_CYPHAL_TX_CAPACITY && b->expired[i]+b->driver_expired[i]>0);
        retries+=b->retried[i];
    }
    for(unsigned i=0;i<4;i++) STRESS_CHECK(b,b->corruption_types[i]>0);
    STRESS_CHECK(b,b->corrupted_subjects[0]>0 && b->corrupted_subjects[1]>0 &&
                   b->corrupted_subjects[2]>0 && b->corrupted_subjects[4]>0);
    STRESS_CHECK(b,retries>0 && b->corrupted_delivered>0);
    /* Stop injecting faults. Keep already delayed wire frames and pending USB
     * completions, release ownership, then demand bounded natural recovery. */
    b->noise=false;b->reverse=false;
    for(unsigned i=0;i<DEVICES;i++) {
        if(b->clients[i].direct) STRESS_CHECK(b,osg_can_client_usb(&b->clients[i],false));
        b->reports[i][0]=(uint8_t)(i+1);b->reports[i][1]=(uint8_t)(11U*i);
    }
    uint32_t end=b->now+35000U;
    while(b->now<end) { tick_multi(b,b->now+10U);usb_stress(b,usb,&random);invariants_stress(b); }
    /* Finish with one known successful completion per slot. All emitted ACKs
     * now arrive immediately; the local snapshots are discarded at teardown. */
    for(unsigned i=0;i<DEVICES;i++) {
        STRESS_CHECK(b,osg_can_client_active(&b->clients[i]));check_report(b,i,false);
        STRESS_CHECK(b,b->clients[i].cyphal.canard.tx.queue_size==0);
        STRESS_CHECK(b,module(b,i)->source_node==osg_cyphal_node_id(&b->clients[i].cyphal));
        STRESS_CHECK(b,module(b,i)->boot_session==b->clients[i].boot);
        STRESS_CHECK(b,b->clients[i].gateway_source==osg_cyphal_node_id(&b->transport));
        for(unsigned j=0;j<i;j++) STRESS_CHECK(b,osg_cyphal_node_id(&b->clients[i].cyphal)!=osg_cyphal_node_id(&b->clients[j].cyphal));
        STRESS_CHECK(b,osg_cyphal_node_id(&b->clients[i].cyphal)!=osg_cyphal_node_id(&b->transport));
        unsigned before=b->completed[i];complete_multi(b,i);pump_multi(b);
        STRESS_CHECK(b,b->completed[i]==before+1 && b->clients[i].ack_sequence==module(b,i)->input_sequence);
        trace_word(b,b->completed[i]);trace_word(b,b->clients[i].lease);trace_word(b,b->clients[i].boot);
    }
    uint32_t digest=b->trace_hash;
    unsigned duplicated=0,corrupted=0,refused=0,expired=0,admissions=0;
    for(unsigned i=0;i<=DEVICES;i++) {
        duplicated+=b->sender_duplicates[i];corrupted+=b->sender_corruptions[i];
        refused+=b->refused[i];expired+=b->expired[i]+b->driver_expired[i];admissions+=b->admission_failed[i];
    }
    STRESS_CHECK(b,b->transport.canard.tx.queue_size==0);
    printf("PASS: stress seed=0x%08x trace=%08x drops=%u delays=%u reordered=%u gateway-reboots=%u migrations=%u stale-USB=%u; ten devices recovered\n",
           seed,digest,b->drops,b->delays,b->reorders,gateway_reboots,b->gateway_migrations,b->stale_usb);
    printf("PASS: wire/queue stress seed=0x%08x duplicates=%u corruptions=%u driver-refusals=%u immutable-retries=%u expired=%u admission-failures=%u\n",
           seed,duplicated,corrupted,refused,retries,expired,admissions);
    destroy_multi(b);return digest;
}
void multi_device_integration_test(void) {
    /* Static storage avoids putting eleven transport pools on the host stack. */
    static bus_t b;init_multi(&b);
    fault(&b,0,OSG_CYPHAL_SUBJECT_ANNOUNCE,1,0,0);tick_multi(&b,0);
    assert(!osg_can_client_active(&b.clients[0]));
    for(unsigned i=1;i<DEVICES;i++) assert(osg_can_client_active(&b.clients[i]));
    /* Drop one reply too: periodic discovery must recover both loss directions. */
    fault(&b,GATEWAY,OSG_CYPHAL_SUBJECT_ASSIGN,1,0,0);tick_multi(&b,600);
    assert(!osg_can_client_active(&b.clients[0]));
    fault(&b,0,OSG_CYPHAL_SUBJECT_ANNOUNCE,0,1,80);tick_multi(&b,1200);
    assert(!osg_can_client_active(&b.clients[0]));
    b.now=1280;pump_multi(&b);assert(osg_can_client_active(&b.clients[0]));
    for(unsigned i=0;i<DEVICES;i++) {
        assert(osg_gateway_register(&b.gateway,&b.clients[i].identity));
        for(unsigned j=0;j<i;j++) assert(b.clients[i].node!=b.clients[j].node);
    }
    osg_gateway_apply_layout(&b.gateway);osg_gateway_set_running(&b.gateway,true);
    tick_multi(&b,1290);
    for(unsigned i=0;i<DEVICES;i++) { check_report(&b,i,false);complete_multi(&b,i); }
    pump_multi(&b);
    for(unsigned i=0;i<DEVICES;i++) assert(b.completed[i]==1 && b.clients[i].ack_sequence==module(&b,i)->input_sequence);

    /* Older input arrives after a newer report from the same device. */
    fault(&b,0,OSG_CYPHAL_SUBJECT_INPUT,0,1,80);
    b.reports[0][0]=0x81;tick_multi(&b,1300);
    b.reports[0][0]=0x82;tick_multi(&b,1310);
    uint32_t sequence=module(&b,0)->input_sequence;
    tick_multi(&b,1380);assert(module(&b,0)->input_sequence>=sequence);check_report(&b,0,false);
    /* Reverse delivery order across ten active senders. */
    b.reverse=true;tick_multi(&b,1390);b.reverse=false;
    for(unsigned i=0;i<DEVICES;i++) check_report(&b,i,false);

    /* An old delayed ACK cannot roll back a newer acknowledged sequence. */
    fault(&b,GATEWAY,OSG_CYPHAL_SUBJECT_INPUT_ACK,0,1,80);complete_multi(&b,0);
    tick_multi(&b,1400);complete_multi(&b,0);pump_multi(&b);
    sequence=b.clients[0].ack_sequence;unsigned completions=b.completed[0];
    tick_multi(&b,1470);assert(b.clients[0].ack_sequence==sequence && b.completed[0]==completions);
    /* Drop an ACK and recover with a subsequent successful USB completion. */
    fault(&b,GATEWAY,OSG_CYPHAL_SUBJECT_INPUT_ACK,1,0,0);complete_multi(&b,1);pump_multi(&b);
    assert(b.completed[1]==1);tick_multi(&b,1480);complete_multi(&b,1);pump_multi(&b);assert(b.completed[1]==2);

    /* A single device loses input while its discovery remains operational. */
    fault(&b,2,OSG_CYPHAL_SUBJECT_INPUT,1000,0,0);
    for(uint32_t t=1490;t<=1790;t+=10) tick_multi(&b,t);
    check_report(&b,2,true);assert(module(&b,2)->online);
    for(unsigned i=0;i<DEVICES;i++) if(i!=2) check_report(&b,i,false);
    fault(&b,0,0,0,0,0);
    for(uint32_t t=1800;t<=2100;t+=10) tick_multi(&b,t);
    check_report(&b,2,false);

    /* Preserve an old input and ACK across a real client destroy/reinitialize. */
    fault(&b,3,OSG_CYPHAL_SUBJECT_INPUT,0,1,100);tick_multi(&b,2110);
    fault(&b,GATEWAY,OSG_CYPHAL_SUBJECT_INPUT_ACK,0,1,100);complete_multi(&b,3);
    uint32_t lease=b.clients[3].lease;
    osg_gateway_identity_t id=b.clients[3].identity;osg_gateway_profile_t profile=b.clients[3].profile;
    osg_cyphal_destroy(&b.clients[3].cyphal);
    assert(osg_can_client_init(&b.clients[3],&id,&profile,2,enqueue_multi,&b.endpoints[3]));
    /* Use a new transport source to avoid interpreting a rebooted transfer-ID
     * as a duplicate in the still-live receiver's transport session. */
    assert(canard_set_node_id(&b.clients[3].cyphal.canard,43));
    tick_multi(&b,2120);assert(b.clients[3].lease>lease && module(&b,3)->boot_session==2);
    tick_multi(&b,2130);sequence=module(&b,3)->input_sequence;
    b.now=2210;pump_multi(&b);
    assert(module(&b,3)->boot_session==2 && module(&b,3)->input_sequence==sequence);
    assert(b.clients[3].ack_sequence==0);check_report(&b,3,false);

    /* A gateway reboot restores serialized slots, increments the persisted
     * epoch, and initially exposes neutral reports until fresh discovery/input. */
    fault(&b,4,OSG_CYPHAL_SUBJECT_INPUT,0,1,100);tick_multi(&b,2220);
    uint32_t old_leases[DEVICES];for(unsigned i=0;i<DEVICES;i++) old_leases[i]=b.clients[i].lease;
    uint8_t record[OSG_GW_REGISTRY_RECORD_SIZE];osg_gateway_registry_t restored;
    assert(osg_gateway_registry_encode(&b.gateway.active,record,sizeof record)==sizeof record);
    assert(osg_gateway_registry_decode(record,sizeof record,&restored));
    osg_gateway_identity_t gateway_id=b.transport.identity;osg_cyphal_destroy(&b.transport);
    assert(osg_gateway_init(&b.gateway,131072,&restored));osg_gateway_set_running(&b.gateway,true);
    assert(osg_cyphal_init(&b.transport,&gateway_id,131072,"org.opensimgear.gateway",enqueue_multi,&b.endpoints[GATEWAY]));
    assert(canard_set_node_id(&b.transport.canard,100));
    for(unsigned i=0;i<DEVICES;i++) check_report(&b,i,true);
    tick_multi(&b,2320);for(unsigned i=0;i<DEVICES;i++) check_report(&b,i,true);
    for(uint32_t t=2330;t<=3100;t+=10) tick_multi(&b,t);
    for(unsigned i=0;i<DEVICES;i++) {
        assert(b.clients[i].epoch==131072 && b.clients[i].lease>old_leases[i]);
        check_report(&b,i,false);complete_multi(&b,i);
    }
    pump_multi(&b);for(unsigned i=0;i<DEVICES;i++) assert(b.clients[i].ack_sequence==module(&b,i)->input_sequence);
    /* Complete absence expires discovery without removing a permanent slot. */
    b.muted[5]=true;
    for(uint32_t t=3110;t<=4800;t+=10) tick_multi(&b,t);
    assert(!module(&b,5)->online);check_report(&b,5,true);
    b.muted[5]=false;
    for(uint32_t t=4810;t<=5400;t+=10) tick_multi(&b,t);
    assert(module(&b,5)->online);check_report(&b,5,false);
    /* Also reboot retaining the CAN source. The receiver may suppress reset
     * transfer IDs until normal periodic traffic/transport aging recovers. */
    lease=b.clients[6].lease;id=b.clients[6].identity;profile=b.clients[6].profile;
    osg_cyphal_destroy(&b.clients[6].cyphal);
    assert(osg_can_client_init(&b.clients[6],&id,&profile,2,enqueue_multi,&b.endpoints[6]));
    assert(canard_set_node_id(&b.clients[6].cyphal.canard,7));
    for(uint32_t t=5410;t<=10400;t+=10) tick_multi(&b,t);
    assert(module(&b,6)->boot_session==2 && b.clients[6].lease>lease);
    check_report(&b,6,false);complete_multi(&b,6);pump_multi(&b);
    assert(b.clients[6].ack_sequence==module(&b,6)->input_sequence);
    assert(b.count==0 && b.drops>=4 && b.delays==6 && b.reorders>=DEVICES);
    for(unsigned i=0;i<DEVICES;i++) osg_cyphal_destroy(&b.clients[i].cyphal);
    osg_cyphal_destroy(&b.transport);
    puts("PASS: ten real clients; dropped discovery/assignment/input/ACK, delayed and reordered frames, isolated freshness/offline recovery, device reboot and gateway reboot with stable restored slots");
    usb_ownership_multi(&b);
    collision_multi(&b,false);
    collision_multi(&b,true);
    const uint32_t seeds[]={UINT32_C(0x13579bdf),UINT32_C(0x2468ace1),UINT32_C(0xc0ffee01)};
    const char *override=getenv("OSG_CYPHAL_STRESS_SEED");
    uint32_t chosen=0;
    if(override) {
        char *end;unsigned long parsed=strtoul(override,&end,10);
        if(!*override || *end || !parsed || parsed>UINT32_MAX) {
            fputs("OSG_CYPHAL_STRESS_SEED must be a decimal integer in 1..4294967295\n",stderr);abort();
        }
        chosen=(uint32_t)parsed;
    }
    unsigned count=override ? 1U : (unsigned)(sizeof seeds/sizeof seeds[0]);
    for(unsigned i=0;i<count;i++) {
        uint32_t seed=override ? chosen : seeds[i];
        uint32_t first=randomized_stress(&b,seed);
        STRESS_CHECK(&b,randomized_stress(&b,seed)==first);
    }
}
