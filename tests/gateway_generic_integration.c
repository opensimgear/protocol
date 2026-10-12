/* Portable integration of the real generic acquisition/client/delivery code
 * with the real gateway core, registry, Cyphal stack and USB-completion boundary.
 * Both endpoints exchange real libcanard frames with generated DSDL payloads. */
#include "opensimgear/gateway.h"
#include "can_client.h"
#include "can_delivery.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { IDLE=0, WAIT_PRESS=1, HOLD=2, WAIT_RELEASE=3, GAP=4 };
typedef struct {
    osg_gateway_t gateway;
    osg_can_client_t client;
    osg_cyphal_t gateway_transport;
    osg_can_delivery_t delivery;
    osg_profile_state_t acquisition;
    osg_profile_sample_t sample;
    osg_profile_t definition;
    osg_profile_button_t buttons[126];
    osg_profile_axis_t axes[8];
    osg_profile_hat_t hats[4];
    osg_profile_encoder_t encoder;
    uint8_t report[OSG_REPORT_CAPACITY];
    osg_gateway_frame_t queued[128],last_input,last_ack;
    osg_gateway_message_t last_input_message;
    bool from_gateway[128];
    unsigned queue_count,announces,assignments,inputs,completion_acks;
} fixture_t;

static bool enqueue(void *context,const osg_gateway_frame_t *frame) {
    fixture_t *f=context;
    assert(f->queue_count<sizeof f->queued/sizeof f->queued[0]);
    f->from_gateway[f->queue_count]=false;
    f->queued[f->queue_count++]=*frame;
    return true;
}
static bool gateway_enqueue(void *context,const osg_gateway_frame_t *frame) {
    fixture_t *f=context;
    assert(f->queue_count<sizeof f->queued/sizeof f->queued[0]);
    f->from_gateway[f->queue_count]=true;
    f->queued[f->queue_count++]=*frame;
    return true;
}
static void rearm(fixture_t *f,uint32_t now) {
    osg_profile_rearm(&f->acquisition,&f->sample,now);
    osg_profile_neutral(&f->acquisition,f->report);
    osg_can_delivery_rearm(&f->delivery);
}
static void pump(fixture_t *f,uint32_t now) {
    osg_cyphal_tick(&f->client.cyphal,now);
    osg_cyphal_tick(&f->gateway_transport,now);
    for(unsigned i=0;i<f->queue_count;i++) {
        if(f->from_gateway[i]) {
            uint32_t generation=f->client.generation;
            uint32_t acknowledged=f->client.ack_sequence;
            bool completed=osg_can_client_receive(&f->client,&f->queued[i],now);
            if(completed) {
                assert(f->client.ack_sequence>acknowledged);
                f->last_ack=f->queued[i];
            }
            if(generation!=f->client.generation) rearm(f,now);
            continue;
        }
        osg_gateway_message_t decoded;
        if(!osg_cyphal_receive(&f->gateway_transport,&f->queued[i],now,&decoded)) continue;
        osg_gateway_message_t reply;
        osg_gateway_receive_result_t result=osg_gateway_receive(&f->gateway,&decoded,now,&reply);
        if(decoded.type==OSG_GW_ANNOUNCE) {
            f->announces++;
            assert(result==OSG_GW_RX_ASSIGNMENT);
            assert(osg_cyphal_send(&f->gateway_transport,&reply,now));
            osg_cyphal_tick(&f->gateway_transport,now);
            f->assignments++;
        } else {
            assert(decoded.type==OSG_GW_INPUT);
            assert(result==OSG_GW_RX_ACCEPTED);
            f->last_input=f->queued[i];
            f->last_input_message=decoded;
            f->inputs++;
        }
    }
    f->queue_count=0;
}
static void acquire(fixture_t *f,uint32_t now) {
    osg_profile_update(&f->acquisition,&f->sample,now,osg_can_client_active(&f->client),f->report);
    osg_can_delivery_observe(&f->delivery,&f->acquisition,f->client.next_sequence);
}
static void transmit(fixture_t *f,uint32_t now) {
    acquire(f,now);
    uint8_t routed[OSG_GW_MAX_REPORT_SIZE]={0};
    memcpy(routed,f->report,f->acquisition.layout.length);
    osg_can_client_tick(&f->client,now,routed);
    pump(f,now);
}
static void closed(fixture_t *f,unsigned input,bool value) {
    assert(input<OSG_INPUTS);
    if(input<24) {
        if(value) f->sample.digital_closed|=UINT32_C(1)<<input;
        else f->sample.digital_closed&=~(UINT32_C(1)<<input);
    } else {
        unsigned bank=(input-24)/16,pin=(input-24)%16;
        if(value) f->sample.expansion_closed[bank]|=(uint16_t)(1U<<pin);
        else f->sample.expansion_closed[bank]&=(uint16_t)~(1U<<pin);
    }
}
static void detent(fixture_t *f,uint32_t now) {
    /* Actual active-low AB contact sequence: 3 -> 2 -> 0 -> 1 -> 3. */
    closed(f,0,true);acquire(f,now);
    closed(f,1,true);acquire(f,now+1);
    closed(f,0,false);acquire(f,now+2);
    closed(f,1,false);acquire(f,now+3);
}
static void init(fixture_t *f) {
    memset(f,0,sizeof *f);
    for(unsigned i=0;i<126;i++) f->buttons[i]=(osg_profile_button_t){.input=(uint8_t)(i+2)};
    for(unsigned i=0;i<8;i++) f->axes[i]=(osg_profile_axis_t){
        .channel=(uint8_t)i,.usage=(uint8_t)(0x30+i),.minimum=0,.maximum=4095,.neutral=(uint16_t)(8192U*i)};
    for(unsigned i=0;i<4;i++) for(unsigned direction=0;direction<4;direction++)
        f->hats[i].inputs[direction]=(uint8_t)(128U+4U*i+direction);
    f->encoder=(osg_profile_encoder_t){.a=0,.b=1};
    f->definition=(osg_profile_t){.name="maximum generic integration",.buttons=f->buttons,.button_count=126,
        .axes=f->axes,.axis_count=8,.hats=f->hats,.hat_count=4,.encoders=&f->encoder,.encoder_count=1,.expander_mask=0xff};
    f->sample.axes_fresh=0xff;f->sample.expansion_available=0xff;
    assert(osg_profile_init(&f->acquisition,&f->definition,&f->sample,0));
    assert(f->acquisition.layout.buttons==128 && f->acquisition.layout.axes==8 && f->acquisition.layout.hats==4);
    assert(f->acquisition.layout.length==OSG_GW_MAX_REPORT_SIZE);
    osg_profile_neutral(&f->acquisition,f->report);
    osg_gateway_profile_t profile;
    assert(osg_can_profile(&f->definition,&f->acquisition.layout,&profile));
    osg_gateway_identity_t id;
    assert(osg_gateway_identity_parse("00112233445566778899aabbccddeeff",&id));
    assert(osg_can_client_init(&f->client,&id,&profile,42,enqueue,f));
    assert(osg_gateway_init(&f->gateway,65536,NULL));
    osg_gateway_identity_t gateway_id;
    assert(osg_gateway_identity_parse("8899aabbccddeeff0011223344556677",&gateway_id));
    assert(osg_cyphal_init(&f->gateway_transport,&gateway_id,65536,
        "org.opensimgear.gateway",gateway_enqueue,f));
    /* Deterministic fixture addresses; automatic collision repair is exercised
     * separately by the canonical transport tests and device client tests. */
    assert(canard_set_node_id(&f->client.cyphal.canard,42));
    assert(canard_set_node_id(&f->gateway_transport.canard,101));
    osg_can_delivery_rearm(&f->delivery);
}
static osg_gateway_delivery_t submit(fixture_t *f,uint8_t packet[OSG_GW_MAX_REPORT_SIZE+1]) {
    osg_gateway_delivery_t delivery;
    packet[0]=1; /* Permanent slot 0 is report ID 1. */
    assert(osg_gateway_delivery_prepare(&f->gateway,0,packet+1,OSG_GW_MAX_REPORT_SIZE,&delivery)==OSG_GW_MAX_REPORT_SIZE);
    return delivery;
}
static osg_gateway_frame_t complete(fixture_t *f,const osg_gateway_delivery_t *delivery,
                                   const uint8_t packet[OSG_GW_MAX_REPORT_SIZE+1],uint32_t now) {
    osg_gateway_message_t message;
    assert(osg_gateway_delivery_complete(&f->gateway,delivery,packet,OSG_GW_MAX_REPORT_SIZE+1,&message));
    assert(message.type==OSG_GW_INPUT_ACK);
    assert(message.sequence==delivery->sequence && !memcmp(message.report,packet+1,OSG_GW_MAX_REPORT_SIZE));
    assert(osg_cyphal_send(&f->gateway_transport,&message,now));
    osg_cyphal_tick(&f->gateway_transport,now);
    pump(f,now);
    assert(f->client.ack_sequence==delivery->sequence);
    osg_can_delivery_completed(&f->delivery,&f->acquisition,f->client.ack_sequence,f->client.acknowledged,now);
    f->completion_acks++;
    return f->last_ack;
}
static void neutral_gateway(const fixture_t *f) {
    uint8_t actual[OSG_GW_MAX_REPORT_SIZE],expected[OSG_GW_MAX_REPORT_SIZE];
    osg_gateway_profile_neutral(&f->client.profile,expected);
    assert(osg_gateway_report(&f->gateway,0,actual,sizeof actual)==sizeof actual);
    assert(!memcmp(actual,expected,sizeof actual));
}

static void register_maximum_profile(fixture_t *f) {
    osg_can_client_tick(&f->client,0,f->report);pump(f,0);
    assert(f->announces==1 && f->assignments==1 && f->inputs==0);
    assert(osg_can_client_active(&f->client));
    assert(osg_gateway_find_module(&f->gateway,&f->client.identity)==0);
    assert(osg_gateway_find_slot(&f->gateway.active,&f->client.identity)==-1);
    assert(osg_gateway_register(&f->gateway,&f->client.identity));
    assert(osg_gateway_needs_apply(&f->gateway));
    uint8_t record[OSG_GW_REGISTRY_RECORD_SIZE];
    size_t length=osg_gateway_registry_encode(&f->gateway.pending,record,sizeof record);
    osg_gateway_registry_t persisted;
    assert(length==sizeof record && osg_gateway_registry_decode(record,length,&persisted));
    assert(osg_gateway_profile_equal(&persisted.slots[0].profile,&f->client.profile));
    f->gateway.pending=persisted;
    /* The board calls this only after its journal commits and USB detaches.
     * Here the portable core boundary is exercised, without claiming hardware. */
    osg_gateway_apply_layout(&f->gateway);
    assert(!osg_gateway_needs_apply(&f->gateway));
    assert(osg_gateway_find_slot(&f->gateway.active,&f->client.identity)==0);
    uint8_t descriptor[OSG_GW_DESCRIPTOR_CAPACITY];
    assert(osg_gateway_descriptor(&f->gateway,descriptor,sizeof descriptor)>0);
    osg_gateway_set_running(&f->gateway,true);
    acquire(f,0);acquire(f,11); /* Released contacts arm through real debounce. */
}
static void encoder_completion_test(fixture_t *f) {
    closed(f,2,true);closed(f,128,true);closed(f,133,true);closed(f,136,true);closed(f,137,true);
    const uint16_t raw[8]={0,4095,2048,1024,3072,100,4000,2020};
    memcpy(f->sample.axes,raw,sizeof raw);
    acquire(f,12);detent(f,13);acquire(f,23);
    assert(f->report[0]==1 && (f->report[15]&0xc0)==0x40);
    assert(f->report[16]==0x20 && f->report[17]==0x81);
    assert(f->report[18]==0 && f->report[19]==0 && f->report[20]==0xff && f->report[21]==0xff);
    assert(f->report[22]==0x08 && f->report[23]==0x80); /* ADC 2048 -> 32776. */
    assert(f->acquisition.encoders[0].phase==WAIT_PRESS);
    transmit(f,23);
    assert(f->inputs==1 && f->client.ack_sequence==0 && f->completion_acks==0);
    uint8_t press_packet[OSG_GW_MAX_REPORT_SIZE+1];
    osg_gateway_delivery_t press=submit(f,press_packet);
    assert(press.valid && !memcmp(press_packet+1,f->report,OSG_GW_MAX_REPORT_SIZE));
    osg_gateway_message_t ignored;
    uint8_t bad[sizeof press_packet];memcpy(bad,press_packet,sizeof bad);bad[0]=2;
    assert(!osg_gateway_delivery_complete(&f->gateway,&press,bad,sizeof bad,&ignored));
    assert(!osg_gateway_delivery_complete(&f->gateway,&press,press_packet,sizeof press_packet-1,&ignored));
    memcpy(bad,press_packet,sizeof bad);bad[16]^=0x40;
    assert(!osg_gateway_delivery_complete(&f->gateway,&press,bad,sizeof bad,&ignored));
    transmit(f,100);
    assert(f->acquisition.encoders[0].phase==WAIT_PRESS && f->client.ack_sequence==0);
    osg_gateway_frame_t press_ack=complete(f,&press,press_packet,110);
    assert(f->acquisition.encoders[0].phase==HOLD && f->acquisition.encoders[0].phase_at==110);
    assert(!osg_can_client_receive(&f->client,&press_ack,111)); /* Duplicate completion is inert. */
    acquire(f,134);assert(f->acquisition.encoders[0].phase==HOLD && (f->report[15]&0xc0)==0x40);
    transmit(f,135);assert(f->acquisition.encoders[0].phase==WAIT_RELEASE && !(f->report[15]&0xc0));
    uint8_t release_packet[OSG_GW_MAX_REPORT_SIZE+1];
    osg_gateway_delivery_t release=submit(f,release_packet);
    assert(release.valid && release.sequence>press.sequence);
    detent(f,136);assert(f->acquisition.encoders[0].count==1);
    acquire(f,200);assert(f->acquisition.encoders[0].phase==WAIT_RELEASE);
    assert(!osg_can_client_receive(&f->client,&press_ack,200));
    assert(f->acquisition.encoders[0].phase==WAIT_RELEASE);
    complete(f,&release,release_packet,201);
    assert(f->acquisition.encoders[0].phase==GAP && f->acquisition.encoders[0].phase_at==201);
    acquire(f,225);assert(f->acquisition.encoders[0].phase==GAP);
    acquire(f,226);assert(f->acquisition.encoders[0].phase==WAIT_PRESS && f->acquisition.encoders[0].count==0);
    /* A historical same-direction press cannot credit the later queued pulse. */
    osg_can_delivery_completed(&f->delivery,&f->acquisition,press.sequence,press.report,226);
    assert(f->acquisition.encoders[0].phase==WAIT_PRESS);
}
static void ownership_and_stale_lease_test(fixture_t *f) {
    transmit(f,226);
    uint8_t pending_packet[OSG_GW_MAX_REPORT_SIZE+1];
    osg_gateway_delivery_t pending=submit(f,pending_packet);
    osg_gateway_message_t old_input=f->last_input_message,old_ack_message;
    assert(osg_gateway_delivery_complete(&f->gateway,&pending,pending_packet,sizeof pending_packet,&old_ack_message));
    /* Delay this exact eligible ACK on the bus until after ownership changes. */
    assert(osg_cyphal_send(&f->gateway_transport,&old_ack_message,226));
    osg_cyphal_tick(&f->gateway_transport,226);
    assert(f->queue_count>0 && f->from_gateway[f->queue_count-1]);
    osg_gateway_frame_t old_ack=f->queued[f->queue_count-1];
    f->queue_count=0;
    assert(osg_can_client_usb(&f->client,true));rearm(f,227);
    osg_can_client_tick(&f->client,228,f->report);pump(f,228);
    assert(!osg_can_client_active(&f->client) && f->gateway.modules[0].direct_usb);
    neutral_gateway(f);
    osg_gateway_message_t ignored;
    assert(!osg_gateway_delivery_complete(&f->gateway,&pending,pending_packet,sizeof pending_packet,&ignored));
    assert(!osg_can_client_receive(&f->client,&old_ack,228));
    assert(osg_can_client_usb(&f->client,false));rearm(f,229);
    osg_can_client_tick(&f->client,230,f->report);pump(f,230);
    assert(osg_can_client_active(&f->client) && !f->gateway.modules[0].direct_usb);
    uint8_t fresh_packet[OSG_GW_MAX_REPORT_SIZE+1];
    osg_gateway_delivery_t fresh=submit(f,fresh_packet);
    assert(fresh.valid && fresh.sequence>pending.sequence);
    assert(!osg_can_client_receive(&f->client,&old_ack,230));
    complete(f,&fresh,fresh_packet,231);
    assert(f->acquisition.encoders[0].phase==IDLE); /* Ownership rearmed acquisition. */
    acquire(f,241);assert(!(f->report[0]&1)); /* Held contact cannot resume a press. */
    closed(f,2,false);acquire(f,242);acquire(f,253);
    closed(f,2,true);acquire(f,254);transmit(f,265);assert(f->report[0]&1);
    uint32_t old_lease=f->client.lease;
    osg_gateway_bus_reset(&f->gateway);osg_gateway_set_running(&f->gateway,true);
    assert(osg_gateway_find_slot(&f->gateway.active,&f->client.identity)==0);
    neutral_gateway(f);
    osg_can_client_reset(&f->client);rearm(f,300);
    osg_can_client_tick(&f->client,301,f->report);pump(f,301);
    assert(f->client.lease>old_lease);
    assert(osg_gateway_receive(&f->gateway,&old_input,302,&ignored)==OSG_GW_RX_REJECTED);
    assert(!osg_can_client_receive(&f->client,&old_ack,302));
    transmit(f,303);
    fresh=submit(f,fresh_packet);assert(fresh.valid);complete(f,&fresh,fresh_packet,304);
}
static void offline_registry_test(fixture_t *f) {
    uint32_t expired=f->gateway.modules[0].last_seen+OSG_GW_OFFLINE_TIMEOUT_MS;
    osg_gateway_tick(&f->gateway,expired);
    assert(!f->gateway.modules[0].online && f->gateway.modules[0].lease==0);
    assert(osg_gateway_find_slot(&f->gateway.pending,&f->client.identity)==0);
    assert(osg_gateway_find_slot(&f->gateway.active,&f->client.identity)==0);
    assert(!osg_gateway_needs_apply(&f->gateway));neutral_gateway(f);
    uint8_t packet[OSG_GW_MAX_REPORT_SIZE+1];osg_gateway_delivery_t neutral=submit(f,packet);
    assert(!neutral.valid);
    osg_gateway_message_t ignored;
    assert(!osg_gateway_delivery_complete(&f->gateway,&neutral,packet,sizeof packet,&ignored));
    uint8_t record[OSG_GW_REGISTRY_RECORD_SIZE];osg_gateway_registry_t restored;
    size_t length=osg_gateway_registry_encode(&f->gateway.active,record,sizeof record);
    assert(length==sizeof record && osg_gateway_registry_decode(record,length,&restored));
    osg_gateway_t rebooted;assert(osg_gateway_init(&rebooted,131072,&restored));
    assert(rebooted.active.slots[0].occupied && !rebooted.modules[0].online);
    assert(osg_gateway_identity_equal(&rebooted.active.slots[0].identity,&f->client.identity));
}
void multi_device_integration_test(void);
int main(void) {
    fixture_t f;init(&f);register_maximum_profile(&f);
    encoder_completion_test(&f);ownership_and_stale_lease_test(&f);offline_registry_test(&f);
    assert(f.announces>=4 && f.assignments==f.announces && f.inputs>=6 && f.completion_acks==4);
    puts("PASS: real generic acquisition -> CAN discovery/assignment -> persistent registry -> gateway HID completion ACK -> encoder delivery; ownership, lease and offline guards");
    osg_cyphal_destroy(&f.client.cyphal);
    osg_cyphal_destroy(&f.gateway_transport);
    multi_device_integration_test();
    return 0;
}
