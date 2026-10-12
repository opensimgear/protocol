/* Real device clients and gateway on a deterministic, fault-injected CAN bus.
 * Frames here have already left the sender: delay does not extend TX deadlines.
 * USB completion and persisted registry restoration are portable boundaries. */
#include "opensimgear/gateway.h"
#include "can_client.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { DEVICES=OSG_GW_MAX_MODULES, GATEWAY=DEVICES, QUEUE=4096 };
typedef struct bus bus_t;
typedef struct { bus_t *bus; unsigned sender; } endpoint_t;
typedef struct { osg_gateway_frame_t frame; unsigned sender; uint32_t due; } packet_t;
struct bus {
    osg_gateway_t gateway;
    osg_cyphal_t transport;
    osg_can_client_t clients[DEVICES];
    endpoint_t endpoints[DEVICES+1];
    uint8_t reports[DEVICES][OSG_GW_MAX_REPORT_SIZE];
    packet_t queue[QUEUE];
    unsigned count, drops, delays, reorders, completed[DEVICES];
    unsigned fault_sender, fault_subject, drop_remaining, delay_remaining;
    uint32_t delay_ms, now;
    bool reverse, muted[DEVICES];
};
static unsigned subject(const osg_gateway_frame_t *f) { return (f->id>>8)&8191U; }
static bool enqueue_multi(void *context,const osg_gateway_frame_t *frame) {
    endpoint_t *e=context; bus_t *b=e->bus;
    bool match=e->sender==b->fault_sender && subject(frame)==b->fault_subject;
    if(match && b->drop_remaining) { b->drop_remaining--; b->drops++; return true; }
    assert(b->count<QUEUE);
    packet_t p={.frame=*frame,.sender=e->sender,.due=b->now};
    if(match && b->delay_remaining) {
        b->delay_remaining--; p.due+=b->delay_ms; b->delays++;
    }
    b->queue[b->count++]=p; return true;
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
        if(osg_cyphal_receive(&b->transport,&p->frame,b->now,&m)) {
            osg_gateway_receive_result_t r=osg_gateway_receive(&b->gateway,&m,b->now,&reply);
            if(r==OSG_GW_RX_ASSIGNMENT) assert(osg_cyphal_send(&b->transport,&reply,b->now));
        }
    }
    for(unsigned i=0;i<DEVICES;i++) if(i!=p->sender && !b->muted[i])
        if(osg_can_client_receive(&b->clients[i],&p->frame,b->now)) b->completed[i]++;
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
    }
}
static void tick_multi(bus_t *b,uint32_t now) {
    assert(now>=b->now); b->now=now;
    osg_gateway_tick(&b->gateway,now);osg_cyphal_tick(&b->transport,now);
    for(unsigned i=0;i<DEVICES;i++) if(!b->muted[i])
        osg_can_client_tick(&b->clients[i],now,b->reports[i]);
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
static void complete_multi(bus_t *b,unsigned i) {
    int slot=osg_gateway_find_slot(&b->gateway.active,&b->clients[i].identity);assert(slot>=0);
    osg_gateway_delivery_t d;uint8_t packet[OSG_GW_MAX_REPORT_SIZE+1];
    size_t size=osg_gateway_delivery_prepare(&b->gateway,(unsigned)slot,packet+1,sizeof packet-1,&d);
    assert(d.valid);packet[0]=(uint8_t)(slot+1);
    osg_gateway_message_t ack;
    assert(osg_gateway_delivery_complete(&b->gateway,&d,packet,size+1,&ack));
    assert(osg_cyphal_send(&b->transport,&ack,b->now));
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
}
