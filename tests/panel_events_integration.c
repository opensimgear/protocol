#include "config_can.h"
#include "opensimgear/events.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    osg_can_client_t client;
    osg_cyphal_t gateway;
    osg_module_config_can_t handler;
    osg_runtime_config_t runtime;
    osg_events_t events;
    osg_profile_sample_t sample;
    char response[OSG_GW_CONFIG_RESPONSE_CAPACITY];
    uint16_t received,total;
    uint32_t now,request;
} fixture_t;
static const osg_profile_button_t button={.input=0};
static const osg_profile_t panel={.name="Master Caution",.buttons=&button,.button_count=1};
static bool gateway_send(void *context,const osg_gateway_frame_t *frame) {
    fixture_t *f=context;
    (void)osg_can_client_receive(&f->client,frame,f->now);return true;
}
static bool module_send(void *context,const osg_gateway_frame_t *frame) {
    fixture_t *f=context;osg_gateway_message_t message;
    if(!osg_cyphal_receive(&f->gateway,frame,f->now,&message)) return true;
    assert(message.type==OSG_GW_CONFIG_RESPONSE && message.sequence==f->request && message.source_node==21);
    assert(message.config_offset==f->received && message.config_total<sizeof f->response);
    if(f->received) assert(message.config_total==f->total);
    f->total=message.config_total;
    memcpy(f->response+f->received,message.config_data,message.config_length);
    f->received+=message.config_length;f->response[f->received]=0;return true;
}
static void request(fixture_t *f,uint32_t id,const char *command) {
    f->request=id;f->received=f->total=0;
    osg_gateway_message_t message={.type=OSG_GW_CONFIG_REQUEST,.identity=f->client.identity,
        .node=1,.lease=2,.gateway_epoch=9,.boot_session=7,.sequence=id,
        .config_total=(uint16_t)strlen(command),.config_length=(uint8_t)strlen(command)};
    assert(strlen(command)<=OSG_GW_CONFIG_CHUNK_SIZE);memcpy(message.config_data,command,message.config_length);
    assert(osg_cyphal_send(&f->gateway,&message,++f->now));
    for(unsigned i=0;i<256 && (f->handler.responding || !f->received);i++) osg_module_config_can_tick(&f->handler,++f->now);
    assert(f->received && f->received==f->total && !strncmp(f->response,"OK\n",3));
}
static void press(fixture_t *f,bool held) {
    f->sample.digital_closed=held?1:0;
    osg_events_observe(&f->events,&f->sample,f->now+=11);
    osg_events_observe(&f->events,&f->sample,f->now+=11);
}
void panel_events_integration_test(void) {
    fixture_t f={0};osg_gateway_identity_t identity={{'E','V','E','N','T',1}},gateway={{'G','A','T','E',1}};
    osg_gateway_profile_t profile={.button_count=1,.revision=1};
    assert(osg_can_client_init(&f.client,&identity,&profile,7,module_send,&f));
    assert(osg_cyphal_init(&f.gateway,&gateway,9,"org.opensimgear.gateway",gateway_send,&f));
    assert(canard_set_node_id(&f.client.cyphal.canard,21));assert(canard_set_node_id(&f.gateway.canard,100));
    f.client.node=1;f.client.lease=2;f.client.epoch=9;f.client.gateway_source=100;f.client.direct=true;
    osg_module_config_can_init(&f.handler,&f.client,&f.runtime);
    assert(osg_events_init(&f.events,&panel,&f.sample,0,1));osg_events_observe(&f.events,&f.sample,10);f.now=10;
    osg_module_config_can_events(&f.handler,&f.events);
    request(&f,1,"events state");assert(strstr(f.response,"\"head\":0"));
    char baseline[sizeof f.response];strcpy(baseline,f.response);
    press(&f,true);press(&f,false);
    request(&f,1,"events state");assert(!strcmp(baseline,f.response));
    request(&f,2,"events read 1 0");
    assert(strstr(f.response,"\"sequence\":1,\"control\":0,\"pressed\":true"));
    assert(strstr(f.response,"\"sequence\":2,\"control\":0,\"pressed\":false"));
    assert(!osg_can_client_active(&f.client) && !f.runtime.owner && !f.runtime.pending_apply);
    for(unsigned i=0;i<40;i++) {press(&f,true);press(&f,false);}
    request(&f,3,"events read 1 0");assert(strstr(f.response,"eventGap"));
    request(&f,4,"events read 1 18");assert(strstr(f.response,"\"next\":34,\"more\":true"));
    assert(osg_events_init(&f.events,&panel,&f.sample,f.now,2));
    request(&f,5,"events read 1 82");assert(strstr(f.response,"staleStream"));
    request(&f,6,"events state");assert(strstr(f.response,"\"stream\":2,\"head\":0"));
    osg_cyphal_destroy(&f.client.cyphal);osg_cyphal_destroy(&f.gateway);
    puts("PASS: panel press/release through sealed CAN event envelopes; immutable retry, direct USB, bounded pages/gaps and stream activation");
}
