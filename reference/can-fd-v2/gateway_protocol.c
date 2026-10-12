#include "opensimgear/gateway_protocol.h"
#include <string.h>

static uint16_t read16(const uint8_t *p) { return (uint16_t)(p[0]|((uint16_t)p[1]<<8)); }
static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void write16(uint8_t *p,uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void write32(uint8_t *p,uint32_t v) {
    for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(8*i));
}
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
    if(compact) return !m->direct_usb;
    return m->type==OSG_GW_HEARTBEAT;
}
bool osg_gateway_frame_encode(const osg_gateway_message_t *m,osg_gateway_frame_t *out) {
    if(!out || !valid_message(m)) return false;
    osg_gateway_frame_t f={.id=OSG_GW_CAN_BASE|((uint32_t)m->type<<8)|m->node,
        .length=(uint8_t)((m->type==OSG_GW_ANNOUNCE || m->type==OSG_GW_INPUT || m->type==OSG_GW_INPUT_ACK)?64:48),
        .extended=true,.fd=true,.brs=true};
    uint8_t *p=f.data;
    p[0]=0x4f; p[1]=0x47; p[2]=OSG_GW_PROTOCOL_VERSION; p[3]=(uint8_t)m->type;
    p[4]=m->node; p[5]=(uint8_t)m->direct_usb;
    if(m->type==OSG_GW_INPUT || m->type==OSG_GW_INPUT_ACK) {
        write32(p+8,m->boot_session); write32(p+12,m->lease); write32(p+16,m->sequence);
        memcpy(p+20,m->report,OSG_GW_MAX_REPORT_SIZE);
    } else {
        memcpy(p+8,m->identity.bytes,16); write32(p+24,m->boot_session);
        if(m->type==OSG_GW_ANNOUNCE) {
        p[28]=m->profile.button_count; p[29]=m->profile.axis_count;
        write16(p+30,m->profile.revision); memcpy(p+32,m->profile.axis_usage,8);
        for(unsigned i=0;i<8;i++) write16(p+40+2*i,m->profile.axis_neutral[i]);
        p[56]=m->profile.hat_count;
        } else {
            write32(p+28,m->lease);
            if(m->type==OSG_GW_ASSIGN) write32(p+32,m->gateway_epoch);
            else write32(p+32,m->sequence);
        }
    }
    write32(p+f.length-4,osg_gateway_crc32(p,f.length-4));
    *out=f; return true;
}
bool osg_gateway_frame_decode(const osg_gateway_frame_t *f,osg_gateway_message_t *out) {
    if(!f || !out || !f->extended || !f->fd || !f->brs ||
       (f->id&OSG_GW_CAN_MASK)!=OSG_GW_CAN_BASE || (f->length!=48 && f->length!=64)) return false;
    const uint8_t *p=f->data;
    if(p[0]!=0x4f || p[1]!=0x47 || p[2]!=OSG_GW_PROTOCOL_VERSION || p[3]<1 || p[3]>OSG_GW_INPUT_ACK ||
       p[5]>1 || p[6] || p[7] || f->id!=(OSG_GW_CAN_BASE|((uint32_t)p[3]<<8)|p[4]) ||
       ((p[3]==OSG_GW_ANNOUNCE || p[3]==OSG_GW_INPUT || p[3]==OSG_GW_INPUT_ACK)?64:48)!=f->length ||
       read32(p+f->length-4)!=osg_gateway_crc32(p,f->length-4)) return false;
    osg_gateway_message_t m={.type=(osg_gateway_message_type_t)p[3],.node=p[4],.direct_usb=p[5]!=0,
                            .boot_session=read32(p+24)};
    if(m.type==OSG_GW_INPUT || m.type==OSG_GW_INPUT_ACK) {
        if(!zero(p+54,6)) return false;
        m.boot_session=read32(p+8); m.lease=read32(p+12); m.sequence=read32(p+16);
        memcpy(m.report,p+20,OSG_GW_MAX_REPORT_SIZE);
    } else {
        memcpy(m.identity.bytes,p+8,16);
        if(m.type==OSG_GW_ANNOUNCE) {
        if(!zero(p+57,3)) return false;
        m.profile.button_count=p[28]; m.profile.axis_count=p[29]; m.profile.revision=read16(p+30);
        m.profile.hat_count=p[56];
        memcpy(m.profile.axis_usage,p+32,8);
        for(unsigned i=0;i<8;i++) m.profile.axis_neutral[i]=read16(p+40+2*i);
        } else {
            m.lease=read32(p+28);
            if(m.type==OSG_GW_ASSIGN) m.gateway_epoch=read32(p+32);
            else m.sequence=read32(p+32);
            if(!zero(p+36,8)) return false;
        }
    }
    if(!valid_message(&m)) return false;
    *out=m; return true;
}
