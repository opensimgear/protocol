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
    osg_actions_t actions;
    char display[64];unsigned display_writes;
    char response[OSG_GW_CONFIG_RESPONSE_CAPACITY];
    uint16_t received,total;
    uint32_t now,request;
} fixture_t;
static const osg_profile_button_t buttons[]={ {.input=0}, {.input=1,.maintained=true} };
static const osg_profile_encoder_t encoder={.a=2,.b=3};
static const osg_profile_selector_t selector={.low=4,.high=5};
static const osg_profile_axis_t axis={.channel=0,.usage=0x30,.minimum=100,.maximum=3900,.neutral=32768};
static const osg_profile_t panel={.name="Master Caution",.buttons=buttons,.button_count=1};
static const osg_profile_t radio={.name="Radio",.buttons=buttons,.button_count=2,.encoders=&encoder,.encoder_count=1,
    .selectors=&selector,.selector_count=1,.axes=&axis,.axis_count=1};
static void transform(void *context,const osg_profile_state_t *state,uint8_t *report) {
    osg_runtime_transform_report(context,state,report);
}
static bool display_write(void *context,const char *id,const osg_action_value_t *values,size_t count) {
    fixture_t *f=context;assert(!strcmp(id,"d.txt") && count==1);
    strcpy(f->display,values[0].value.text);f->display_writes++;return true;
}
static bool display_read(void *context,const char *id,osg_action_value_t *values,size_t count) {
    fixture_t *f=context;assert(!strcmp(id,"d.txt") && count==1);
    values[0].type=OSG_ACTION_STRING;strcpy(values[0].value.text,f->display);return true;
}
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
    /* The real gateway renews the same assigned lease periodically. Larger
     * metadata/pages advance this fixture beyond the assignment timeout. */
    osg_gateway_message_t assignment={.type=OSG_GW_ASSIGN,.identity=f->client.identity,
        .node=1,.lease=2,.gateway_epoch=9,.boot_session=7};
    assert(osg_cyphal_send(&f->gateway,&assignment,++f->now));
    osg_gateway_message_t message={.type=OSG_GW_CONFIG_REQUEST,.identity=f->client.identity,
        .node=1,.lease=2,.gateway_epoch=9,.boot_session=7,.sequence=id,
        .config_total=(uint16_t)strlen(command)};
    assert(strlen(command)<OSG_GW_CONFIG_COMMAND_CAPACITY);
    for(unsigned offset=0;offset<strlen(command);offset+=OSG_GW_CONFIG_CHUNK_SIZE) {
        unsigned remaining=(unsigned)strlen(command)-offset;
        message.config_offset=(uint16_t)offset;
        message.config_length=(uint8_t)(remaining>OSG_GW_CONFIG_CHUNK_SIZE?OSG_GW_CONFIG_CHUNK_SIZE:remaining);
        memcpy(message.config_data,command+offset,message.config_length);
        assert(osg_cyphal_send(&f->gateway,&message,++f->now));
    }
    for(unsigned i=0;i<256 && (f->handler.responding || !f->received);i++) osg_module_config_can_tick(&f->handler,++f->now);
    if(!f->received || f->received!=f->total || strncmp(f->response,"OK\n",3))
        fprintf(stderr,"request %u %s: received=%u total=%u now=%u node=%u response=%s\n",id,command,f->received,f->total,f->now,f->client.node,f->response);
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
    assert(osg_events_init(&f.events,&radio,&f.sample,f.now,3));
    f.client.profile=(osg_gateway_profile_t){.button_count=7,.axis_count=1,.axis_usage={0x30},.axis_neutral={32768},.revision=1};
    f.runtime.active.axis_count=1;f.runtime.active.axes[0]=axis;
    f.runtime.active.axis_options[0].inverted=true;f.runtime.active.axis_options[0].deadzone=1000;
    osg_events_set_transform(&f.events,transform,&f.runtime);
    f.sample.axes_fresh=1;f.sample.axes[0]=100;
    osg_events_observe(&f.events,&f.sample,++f.now);
    request(&f,7,"events state");
    assert(strstr(f.response,"\"contactButtonCount\":2,\"maintainedButtons\":[1],\"selectorButtons\":[[4,5,6]]"));
    assert(strstr(f.response,"\"axisCount\":1,\"axisUsage\":[48],\"axisNeutral\":[32768],\"axes\":[65535],\"axisAvailable\":[true]"));
    f.sample.axes_fresh=0;osg_events_observe(&f.events,&f.sample,f.now+=101);
    request(&f,8,"events state");assert(strstr(f.response,"\"axes\":[32768],\"axisAvailable\":[false]"));
    static const osg_action_parameter_t text={.id="text",.label="Text",.type=OSG_ACTION_STRING,.max_bytes=7};
    static const osg_action_display_t display={.columns=7,.rows=1,.encoding=OSG_ACTION_DISPLAY_ASCII};
    static const osg_action_descriptor_t descriptor={.id="d.txt",.label="Frequency",.endpoint="radio.frequency",
        .semantics=OSG_ACTION_SETTER,.readable=true,.parameters=&text,.parameter_count=1,.display=&display};
    assert(osg_actions_init(&f.actions,&descriptor,1,1,display_write,display_read,&f));
    assert(osg_actions_enable_ownership(&f.actions));
    osg_module_config_can_actions(&f.handler,&f.actions);
    request(&f,9,"action describe 0");assert(strstr(f.response,"\"display\":{\"columns\":7,\"rows\":1,\"encoding\":\"ascii\"}"));
    request(&f,10,"action invoke 1 d.txt 3132332E343530");assert(strstr(f.response,"\"values\":[\"123.450\"]"));
    assert(f.display_writes==1 && !strcmp(f.display,"123.450"));
    request(&f,10,"action invoke 1 d.txt 3132332E343530");assert(f.display_writes==1);
    request(&f,11,"action invoke 1 d.txt C2A3");assert(strstr(f.response,"invalidArgument") && f.display_writes==1);
    request(&f,12,"action state");assert(strstr(f.response,"123.450"));
    request(&f,13,"action claim 1 d.txt 00112233-4455-6677-8899-aabbccddeeff 5000 -");
    assert(strstr(f.response,"\"leaseId\":1") && f.display_writes==1);
    uint32_t expiry=f.actions.leases[0].expires_at;
    request(&f,13,"action claim 1 d.txt 00112233-4455-6677-8899-aabbccddeeff 5000 -");
    assert(f.actions.leases[0].expires_at==expiry && f.display_writes==1);
    char direct[OSG_ACTION_REPLY_CAPACITY];
    assert(osg_actions_command_at(&f.actions,"action invoke 1 d.txt 393939",++f.now,OSG_ACTION_SCOPE_USB,direct,sizeof direct));
    assert(strstr(direct,"outputBusy") && f.display_writes==1);
    request(&f,14,"action invoke_owned 00112233-4455-6677-8899-aabbccddeeff 1 1 d.txt 3235312e303030");
    assert(strstr(f.response,"251.000") && f.display_writes==2);
    request(&f,14,"action invoke_owned 00112233-4455-6677-8899-aabbccddeeff 1 1 d.txt 3235312e303030");assert(f.display_writes==2);
    request(&f,15,"action renew 00112233-4455-6677-8899-aabbccddeeff 1 5000");expiry=f.actions.leases[0].expires_at;
    request(&f,15,"action renew 00112233-4455-6677-8899-aabbccddeeff 1 5000");assert(f.actions.leases[0].expires_at==expiry);
    request(&f,16,"action ownership 0");assert(strstr(f.response,"\"owned\":true") && !strstr(f.response,"00112233-4455"));
    /* Expiry applies the callback even if neither HID nor action requests run. */
    f.now=expiry;osg_module_config_can_tick(&f.handler,f.now);
    assert(!f.actions.leases[0].owned && !f.display[0] && f.display_writes==3);
    request(&f,17,"action ownership 0");assert(strstr(f.response,"\"fallbackStatus\":\"applied\""));
    request(&f,18,"action claim 1 d.txt 00112233-4455-6677-8899-aabbccddeeff 5000 -");
    request(&f,19,"action invoke_owned 00112233-4455-6677-8899-aabbccddeeff 2 1 d.txt 3235312e303030");
    f.client.lease++;osg_module_config_can_tick(&f.handler,++f.now);
    assert(!f.actions.leases[0].owned && !f.display[0] && f.display_writes==5);
    osg_gateway_profile_t output_only={.revision=1};
    assert(osg_gateway_profile_valid(&output_only) && !osg_gateway_profile_report_valid(&output_only,(uint8_t[OSG_GW_MAX_REPORT_SIZE]){0}));
    assert(!f.runtime.owner && !f.runtime.pending_apply);
    osg_cyphal_destroy(&f.client.cyphal);osg_cyphal_destroy(&f.gateway);
    puts("PASS: panel events, calibrated axis snapshots and bounded ASCII display setters through sealed CAN envelopes; endpoint leases, immutable retries, direct USB and stream activation");
}
