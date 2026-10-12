#include "opensimgear/gateway_protocol.h"
#include <string.h>
#include "opensimgear/input/Announce_1_0.h"
#include "opensimgear/input/Assignment_1_0.h"
#include "opensimgear/input/Input_1_0.h"
#include "opensimgear/input/Status_1_0.h"
#include "opensimgear/input/InputAck_1_0.h"
#include "opensimgear/input/ConfigRequest_1_0.h"
#include "opensimgear/input/ConfigResponse_1_0.h"


static void write16(uint8_t *p,uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static bool zero(const uint8_t *p,size_t n) {
    for(size_t i=0;i<n;i++) if(p[i]) return false;
    return true;
}
bool osg_gateway_identity_valid(const osg_gateway_identity_t *id) {
    if(!id || zero(id->bytes,sizeof id->bytes)) return false;
    for(size_t i=0;i<sizeof id->bytes;i++) if(id->bytes[i]!=0xff) return true;
    return false;
}
bool osg_gateway_identity_equal(const osg_gateway_identity_t *a,const osg_gateway_identity_t *b) {
    return a && b && !memcmp(a->bytes,b->bytes,OSG_GW_IDENTITY_SIZE);
}
static int hex(char c) {
    if(c>='0' && c<='9') return c-'0';
    if(c>='a' && c<='f') return c-'a'+10;
    if(c>='A' && c<='F') return c-'A'+10;
    return -1;
}
bool osg_gateway_identity_parse(const char *s,osg_gateway_identity_t *out) {
    if(!s || !out || strlen(s)!=32) return false;
    osg_gateway_identity_t id;
    for(unsigned i=0;i<16;i++) {
        int high=hex(s[2*i]),low=hex(s[2*i+1]);
        if(high<0 || low<0) return false;
        id.bytes[i]=(uint8_t)((high<<4)|low);
    }
    if(!osg_gateway_identity_valid(&id)) return false;
    *out=id; return true;
}
void osg_gateway_identity_format(const osg_gateway_identity_t *id,char out[33]) {
    static const char digits[]="0123456789abcdef";
    if(!out) return;
    if(!id) { out[0]=0; return; }
    for(unsigned i=0;i<16;i++) { out[2*i]=digits[id->bytes[i]>>4]; out[2*i+1]=digits[id->bytes[i]&15]; }
    out[32]=0;
}
bool osg_gateway_profile_valid(const osg_gateway_profile_t *p) {
    if(!p || p->button_count>OSG_GW_MAX_BUTTONS || p->axis_count>OSG_GW_MAX_AXES ||
       p->hat_count>OSG_GW_MAX_HATS || (!p->button_count && !p->axis_count && !p->hat_count) || !p->revision) return false;
    uint16_t seen=0;
    for(unsigned i=0;i<8;i++) {
        if(i>=p->axis_count) {
            if(p->axis_usage[i] || p->axis_neutral[i]) return false;
        } else {
            uint8_t usage=p->axis_usage[i];
            if(usage<0x30 || usage>0x38 || (seen&(1U<<(usage-0x30)))) return false;
            seen|=(uint16_t)(1U<<(usage-0x30));
        }
    }
    return true;
}
bool osg_gateway_profile_equal(const osg_gateway_profile_t *a,const osg_gateway_profile_t *b) {
    return a && b && a->button_count==b->button_count && a->axis_count==b->axis_count && a->hat_count==b->hat_count &&
        a->revision==b->revision && !memcmp(a->axis_usage,b->axis_usage,sizeof a->axis_usage) &&
        !memcmp(a->axis_neutral,b->axis_neutral,sizeof a->axis_neutral);
}
size_t osg_gateway_profile_report_size(const osg_gateway_profile_t *p) {
    return osg_gateway_profile_valid(p) ? (size_t)((p->button_count+7)/8+(p->hat_count+1)/2+2*p->axis_count) : 0;
}
bool osg_gateway_profile_report_valid(const osg_gateway_profile_t *p,const uint8_t report[OSG_GW_MAX_REPORT_SIZE]) {
    size_t length=osg_gateway_profile_report_size(p);
    if(!length || !report) return false;
    if(p->button_count%8 && (report[p->button_count/8]&(uint8_t)(0xffU<<(p->button_count%8)))) return false;
    unsigned offset=(p->button_count+7)/8;
    for(unsigned hat=0;hat<p->hat_count;hat++)
        if(((report[offset+hat/2]>>(4*(hat%2)))&15)>8) return false;
    if(p->hat_count%2 && (report[offset+p->hat_count/2]&0xf0)) return false;
    return zero(report+length,OSG_GW_MAX_REPORT_SIZE-length);
}
void osg_gateway_profile_neutral(const osg_gateway_profile_t *p,uint8_t report[OSG_GW_MAX_REPORT_SIZE]) {
    if(!report) return;
    memset(report,0,OSG_GW_MAX_REPORT_SIZE);
    if(!osg_gateway_profile_valid(p)) return;
    unsigned offset=(p->button_count+7)/8;
    for(unsigned hat=0;hat<p->hat_count;hat++) report[offset+hat/2]|=(uint8_t)(8U<<(4*(hat%2)));
    offset+=(p->hat_count+1)/2;
    for(unsigned axis=0;axis<p->axis_count;axis++) write16(report+offset+2*axis,p->axis_neutral[axis]);
}
uint32_t osg_gateway_crc32(const void *data,size_t length) {
    const uint8_t *p=data; uint32_t crc=UINT32_MAX;
    for(size_t i=0;i<length;i++) {
        crc^=p[i];
        for(unsigned bit=0;bit<8;bit++) crc=(crc>>1)^(UINT32_C(0xedb88320)&(uint32_t)-(int32_t)(crc&1));
    }
    return ~crc;
}
static bool valid_message(const osg_gateway_message_t *m) {
    if(!m || !m->boot_session) return false;
    bool compact=m->type==OSG_GW_INPUT || m->type==OSG_GW_INPUT_ACK;
    if(!compact && !osg_gateway_identity_valid(&m->identity)) return false;
    if(m->type==OSG_GW_ANNOUNCE) return !m->node && osg_gateway_profile_valid(&m->profile);
    if(m->node<1 || m->node>10 || !m->lease) return false;
    if(m->type==OSG_GW_ASSIGN) return !m->direct_usb && m->gateway_epoch!=0;
    if(m->type==OSG_GW_CONFIG_REQUEST || m->type==OSG_GW_CONFIG_RESPONSE) {
        unsigned capacity=m->type==OSG_GW_CONFIG_REQUEST ? OSG_GW_CONFIG_COMMAND_CAPACITY : OSG_GW_CONFIG_RESPONSE_CAPACITY;
        return m->gateway_epoch && m->sequence && m->config_total && m->config_total<capacity &&
            m->config_length && m->config_length<=OSG_GW_CONFIG_CHUNK_SIZE &&
            m->config_offset<m->config_total && (unsigned)m->config_offset+m->config_length<=m->config_total &&
            (m->config_offset%OSG_GW_CONFIG_CHUNK_SIZE)==0 &&
            m->config_length==((unsigned)m->config_total-m->config_offset>OSG_GW_CONFIG_CHUNK_SIZE ?
                OSG_GW_CONFIG_CHUNK_SIZE : m->config_total-m->config_offset);
    }
    if(compact) return !m->direct_usb;
    return m->type==OSG_GW_HEARTBEAT;
}
uint16_t osg_gateway_subject(osg_gateway_message_type_t type) {
    switch(type) {
    case OSG_GW_ANNOUNCE: return OSG_CYPHAL_SUBJECT_ANNOUNCE;
    case OSG_GW_ASSIGN: return OSG_CYPHAL_SUBJECT_ASSIGN;
    case OSG_GW_INPUT: return OSG_CYPHAL_SUBJECT_INPUT;
    case OSG_GW_HEARTBEAT: return OSG_CYPHAL_SUBJECT_STATUS;
    case OSG_GW_INPUT_ACK: return OSG_CYPHAL_SUBJECT_INPUT_ACK;
    case OSG_GW_CONFIG_REQUEST: return OSG_CYPHAL_SUBJECT_CONFIG_REQUEST;
    case OSG_GW_CONFIG_RESPONSE: return OSG_CYPHAL_SUBJECT_CONFIG_RESPONSE;
    default: return UINT16_MAX;
    }
}
size_t osg_gateway_payload_encode(const osg_gateway_message_t *m,void *buffer,size_t capacity) {
    if(!buffer || !valid_message(m)) return 0;
    size_t length=capacity;
    if(m->type==OSG_GW_CONFIG_REQUEST || m->type==OSG_GW_CONFIG_RESPONSE) {
#define SERIALIZE_CONFIG(TYPE) do { TYPE x={0}; x.logical_node=m->node; \
        memcpy(x.unique_id,m->identity.bytes,sizeof x.unique_id); x.boot_session=m->boot_session; \
        x.lease=m->lease; x.gateway_epoch=m->gateway_epoch; x.request_id=m->sequence; \
        x.offset=m->config_offset; x._total=m->config_total; x.length=m->config_length; \
        memcpy(x.data,m->config_data,sizeof x.data); \
        return TYPE##_serialize_(&x,buffer,&length)>=0 ? length : 0; } while(0)
        if(m->type==OSG_GW_CONFIG_REQUEST) SERIALIZE_CONFIG(opensimgear_input_ConfigRequest_1_0);
        SERIALIZE_CONFIG(opensimgear_input_ConfigResponse_1_0);
#undef SERIALIZE_CONFIG
    }
    switch(m->type) {
    case OSG_GW_ANNOUNCE: {
        opensimgear_input_Announce_1_0 x={0};
        memcpy(x.unique_id,m->identity.bytes,sizeof x.unique_id);
        x.boot_session=m->boot_session;
        x.direct_usb=m->direct_usb;
        x.configuration_generation=m->configuration_generation;
        x.profile.button_count=m->profile.button_count;
        x.profile.axis_count=m->profile.axis_count;
        x.profile.hat_count=m->profile.hat_count;
        x.profile.revision=m->profile.revision;
        memcpy(x.profile.axis_usage,m->profile.axis_usage,sizeof x.profile.axis_usage);
        memcpy(x.profile.axis_neutral,m->profile.axis_neutral,sizeof x.profile.axis_neutral);
        return opensimgear_input_Announce_1_0_serialize_(&x,buffer,&length)>=0 ? length : 0;
    }
    case OSG_GW_ASSIGN: {
        opensimgear_input_Assignment_1_0 x={0};
        x.logical_node=m->node;
        memcpy(x.unique_id,m->identity.bytes,sizeof x.unique_id);
        x.boot_session=m->boot_session;
        x.lease=m->lease;
        x.gateway_epoch=m->gateway_epoch;
        return opensimgear_input_Assignment_1_0_serialize_(&x,buffer,&length)>=0 ? length : 0;
    }
    case OSG_GW_INPUT: {
        opensimgear_input_Input_1_0 x={0};
        x.logical_node=m->node;
        x.boot_session=m->boot_session;
        x.lease=m->lease;
        x.sequence=m->sequence;
        memcpy(x.report,m->report,sizeof x.report);
        return opensimgear_input_Input_1_0_serialize_(&x,buffer,&length)>=0 ? length : 0;
    }
    case OSG_GW_HEARTBEAT: {
        opensimgear_input_Status_1_0 x={0};
        x.logical_node=m->node;
        x.direct_usb=m->direct_usb;
        memcpy(x.unique_id,m->identity.bytes,sizeof x.unique_id);
        x.boot_session=m->boot_session;
        x.lease=m->lease;
        x.sequence=m->sequence;
        return opensimgear_input_Status_1_0_serialize_(&x,buffer,&length)>=0 ? length : 0;
    }
    case OSG_GW_INPUT_ACK: {
        opensimgear_input_InputAck_1_0 x={0};
        x.logical_node=m->node;
        x.boot_session=m->boot_session;
        x.lease=m->lease;
        x.sequence=m->sequence;
        memcpy(x.report,m->report,sizeof x.report);
        return opensimgear_input_InputAck_1_0_serialize_(&x,buffer,&length)>=0 ? length : 0;
    }
    default: return 0;
    }
}
bool osg_gateway_payload_decode(uint16_t subject,const void *buffer,size_t length,osg_gateway_message_t *out) {
    if(!buffer || !out) return false;
    osg_gateway_message_t m={0};
    if(subject==OSG_CYPHAL_SUBJECT_CONFIG_REQUEST || subject==OSG_CYPHAL_SUBJECT_CONFIG_RESPONSE) {
#define DESERIALIZE_CONFIG(TYPE) do { TYPE x; \
        if(length<TYPE##_SERIALIZATION_BUFFER_SIZE_BYTES_ || TYPE##_deserialize_(&x,buffer,&length)<0) return false; \
        m.node=x.logical_node; memcpy(m.identity.bytes,x.unique_id,sizeof x.unique_id); \
        m.boot_session=x.boot_session; m.lease=x.lease; m.gateway_epoch=x.gateway_epoch; \
        m.sequence=x.request_id; m.config_offset=x.offset; m.config_total=x._total; m.config_length=x.length; \
        memcpy(m.config_data,x.data,sizeof x.data); } while(0)
        if(subject==OSG_CYPHAL_SUBJECT_CONFIG_REQUEST) {
            m.type=OSG_GW_CONFIG_REQUEST; DESERIALIZE_CONFIG(opensimgear_input_ConfigRequest_1_0);
        } else { m.type=OSG_GW_CONFIG_RESPONSE; DESERIALIZE_CONFIG(opensimgear_input_ConfigResponse_1_0); }
#undef DESERIALIZE_CONFIG
        if(!valid_message(&m)) return false;
        *out=m; return true;
    }
    switch(subject) {
    case OSG_CYPHAL_SUBJECT_ANNOUNCE: {
        if(length<opensimgear_input_Announce_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_) return false;
        opensimgear_input_Announce_1_0 x;
        if(opensimgear_input_Announce_1_0_deserialize_(&x,buffer,&length)<0) return false;
        m.type=OSG_GW_ANNOUNCE;
        memcpy(m.identity.bytes,x.unique_id,sizeof x.unique_id);
        m.boot_session=x.boot_session;
        if(x.direct_usb>1) return false;
        m.direct_usb=x.direct_usb!=0;
        m.configuration_generation=x.configuration_generation;
        m.profile.button_count=x.profile.button_count;
        m.profile.axis_count=x.profile.axis_count;
        m.profile.hat_count=x.profile.hat_count;
        m.profile.revision=x.profile.revision;
        memcpy(m.profile.axis_usage,x.profile.axis_usage,sizeof x.profile.axis_usage);
        memcpy(m.profile.axis_neutral,x.profile.axis_neutral,sizeof x.profile.axis_neutral);
        break;
    }
    case OSG_CYPHAL_SUBJECT_ASSIGN: {
        if(length<opensimgear_input_Assignment_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_) return false;
        opensimgear_input_Assignment_1_0 x;
        if(opensimgear_input_Assignment_1_0_deserialize_(&x,buffer,&length)<0) return false;
        m.type=OSG_GW_ASSIGN;
        m.node=x.logical_node;
        memcpy(m.identity.bytes,x.unique_id,sizeof x.unique_id);
        m.boot_session=x.boot_session;
        m.lease=x.lease;
        m.gateway_epoch=x.gateway_epoch;
        break;
    }
    case OSG_CYPHAL_SUBJECT_INPUT: {
        if(length<opensimgear_input_Input_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_) return false;
        opensimgear_input_Input_1_0 x;
        if(opensimgear_input_Input_1_0_deserialize_(&x,buffer,&length)<0) return false;
        m.type=OSG_GW_INPUT;
        m.node=x.logical_node;
        m.boot_session=x.boot_session;
        m.lease=x.lease;
        m.sequence=x.sequence;
        memcpy(m.report,x.report,sizeof x.report);
        break;
    }
    case OSG_CYPHAL_SUBJECT_STATUS: {
        if(length<opensimgear_input_Status_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_) return false;
        opensimgear_input_Status_1_0 x;
        if(opensimgear_input_Status_1_0_deserialize_(&x,buffer,&length)<0) return false;
        m.type=OSG_GW_HEARTBEAT;
        m.node=x.logical_node;
        if(x.direct_usb>1) return false;
        m.direct_usb=x.direct_usb!=0;
        memcpy(m.identity.bytes,x.unique_id,sizeof x.unique_id);
        m.boot_session=x.boot_session;
        m.lease=x.lease;
        m.sequence=x.sequence;
        break;
    }
    case OSG_CYPHAL_SUBJECT_INPUT_ACK: {
        if(length<opensimgear_input_InputAck_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_) return false;
        opensimgear_input_InputAck_1_0 x;
        if(opensimgear_input_InputAck_1_0_deserialize_(&x,buffer,&length)<0) return false;
        m.type=OSG_GW_INPUT_ACK;
        m.node=x.logical_node;
        m.boot_session=x.boot_session;
        m.lease=x.lease;
        m.sequence=x.sequence;
        memcpy(m.report,x.report,sizeof x.report);
        break;
    }
    default: return false;
    }
    if(!valid_message(&m)) return false;
    *out=m; return true;
}
