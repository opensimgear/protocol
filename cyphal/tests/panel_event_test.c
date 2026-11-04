#include "opensimgear/gateway_protocol.h"
#include <assert.h>
#include <string.h>

static osg_gateway_message_t sample(osg_panel_kind_t kind) {
    return (osg_gateway_message_t){
        .type=OSG_GW_PANEL_EVENT,.node=32,.boot_session=UINT32_MAX,.lease=2,
        .gateway_epoch=3,.configuration_generation=4,.sequence=5,
        .panel_cursor=6,.panel_sampled_at_ms=0,.panel_kind=kind
    };
}

static osg_gateway_message_t round_trip(const osg_gateway_message_t *sent,size_t expected) {
    uint8_t bytes[64]={0};
    size_t size=osg_gateway_payload_encode(sent,bytes,63);
    assert(size==expected);
    osg_gateway_message_t received=sample(OSG_PANEL_GAP);
    assert(!osg_gateway_payload_decode(OSG_CYPHAL_SUBJECT_PANEL_EVENT,bytes,size-1,&received));
    assert(received.panel_kind==OSG_PANEL_GAP);
    assert(osg_gateway_payload_decode(OSG_CYPHAL_SUBJECT_PANEL_EVENT,bytes,63,&received));
    assert(received.type==sent->type && received.node==sent->node);
    assert(received.boot_session==sent->boot_session && received.lease==sent->lease);
    assert(received.gateway_epoch==sent->gateway_epoch && received.configuration_generation==sent->configuration_generation);
    assert(received.sequence==sent->sequence && received.panel_cursor==sent->panel_cursor);
    assert(received.panel_sampled_at_ms==sent->panel_sampled_at_ms && received.panel_kind==sent->panel_kind);
    if(size<63) {
        bytes[size]=1;
        osg_gateway_message_t unchanged=received;
        assert(!osg_gateway_payload_decode(OSG_CYPHAL_SUBJECT_PANEL_EVENT,bytes,63,&received));
        assert(!memcmp(&received,&unchanged,sizeof received));
    }
    assert(!osg_gateway_payload_decode(OSG_CYPHAL_SUBJECT_PANEL_EVENT,bytes,64,&received));
    bytes[29]=4;
    assert(!osg_gateway_payload_decode(OSG_CYPHAL_SUBJECT_PANEL_EVENT,bytes,63,&received));
    return received;
}

int main(void) {
    uint8_t bytes[63];
    osg_gateway_message_t m=sample(OSG_PANEL_LAYOUT);
    m.panel_layout=(osg_panel_layout_t){.contact_count=88,.encoder_count=8,.selector_count=8};
    m.panel_layout.maintained_contacts[10]=128;
    osg_gateway_message_t out=round_trip(&m,49);
    assert(out.panel_layout.contact_count==88 && out.panel_layout.encoder_count==8);
    assert(out.panel_layout.selector_count==8 && out.panel_layout.maintained_contacts[10]==128);
    m.panel_layout.maintained_contacts[11]=1;
    assert(!osg_gateway_payload_encode(&m,bytes,sizeof bytes));
    m.panel_layout.contact_count=129;
    assert(!osg_gateway_payload_encode(&m,bytes,sizeof bytes));

    m=sample(OSG_PANEL_STATE);
    m.panel_state.held[15]=128;
    m.panel_state.axes[7]=UINT16_MAX;
    m.panel_state.axis_available=128;
    out=round_trip(&m,63);
    assert(out.panel_state.held[15]==128 && out.panel_state.axes[7]==UINT16_MAX && out.panel_state.axis_available==128);
    assert(!osg_gateway_payload_encode(&m,bytes,62));

    m=sample(OSG_PANEL_EDGES);
    m.panel_edges.count=6;
    for(unsigned i=0;i<6;i++) {
        m.panel_edges.entries[i]=(osg_panel_edge_t){
            .control=127,.pressed=i%2==0,.captured_at_ms=UINT32_MAX-2U+i
        };
    }
    out=round_trip(&m,61);
    assert(out.panel_edges.count==6);
    for(unsigned i=0;i<6;i++) {
        assert(out.panel_edges.entries[i].control==127);
        assert(out.panel_edges.entries[i].pressed==(i%2==0));
        assert(out.panel_edges.entries[i].captured_at_ms==UINT32_MAX-2U+i);
    }
    m.panel_edges.entries[0].control=128;
    assert(!osg_gateway_payload_encode(&m,bytes,sizeof bytes));
    m.panel_edges.entries[0].control=127;
    m.panel_cursor=5;
    assert(!osg_gateway_payload_encode(&m,bytes,sizeof bytes));
    m.panel_edges.count=0;
    assert(!osg_gateway_payload_encode(&m,bytes,sizeof bytes));
    m.panel_edges.count=7;
    assert(!osg_gateway_payload_encode(&m,bytes,sizeof bytes));

    m=sample(OSG_PANEL_GAP);
    m.panel_gap=OSG_PANEL_GAP_RING;
    out=round_trip(&m,31);
    assert(out.panel_gap==OSG_PANEL_GAP_RING);
    m.panel_gap=OSG_PANEL_GAP_EXHAUSTED;
    out=round_trip(&m,31);
    assert(out.panel_gap==OSG_PANEL_GAP_EXHAUSTED);
    m.gateway_epoch=0;
    assert(!osg_gateway_payload_encode(&m,bytes,sizeof bytes));
    m=sample(OSG_PANEL_GAP);
    assert(!osg_gateway_payload_encode(&m,bytes,sizeof bytes));
    m.panel_kind=(osg_panel_kind_t)4;
    assert(!osg_gateway_payload_encode(&m,bytes,sizeof bytes));
    return 0;
}
