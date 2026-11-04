#ifndef OSG_GATEWAY_PROTOCOL_H
#define OSG_GATEWAY_PROTOCOL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Application contract 3 uses the canonical OpenSimGear Cyphal DSDL types. */
enum { OSG_GW_PROTOCOL_VERSION=3, OSG_GW_MAX_MODULES=32,
       OSG_GW_IDENTITY_SIZE=16, OSG_GW_MAX_AXES=8, OSG_GW_MAX_BUTTONS=128,
       OSG_GW_MAX_HATS=4, OSG_GW_MAX_REPORT_SIZE=34, OSG_GW_FRAME_CAPACITY=64,
       OSG_GW_CONFIG_CHUNK_SIZE=24, OSG_GW_CONFIG_COMMAND_CAPACITY=384,
       OSG_GW_CONFIG_RESPONSE_CAPACITY=4096 };
enum { OSG_CYPHAL_SUBJECT_ANNOUNCE=2000, OSG_CYPHAL_SUBJECT_ASSIGN=2001,
       OSG_CYPHAL_SUBJECT_INPUT=2002, OSG_CYPHAL_SUBJECT_STATUS=2003,
       OSG_CYPHAL_SUBJECT_INPUT_ACK=2004, OSG_CYPHAL_SUBJECT_CONFIG_REQUEST=2005,
       OSG_CYPHAL_SUBJECT_CONFIG_RESPONSE=2006, OSG_CYPHAL_SUBJECT_PANEL_EVENT=2007 };
typedef struct { uint8_t bytes[OSG_GW_IDENTITY_SIZE]; } osg_gateway_identity_t;
typedef struct {
    uint8_t button_count, axis_count, hat_count;
    uint16_t revision;
    uint8_t axis_usage[OSG_GW_MAX_AXES];
    uint16_t axis_neutral[OSG_GW_MAX_AXES];
} osg_gateway_profile_t;
typedef enum { OSG_GW_ANNOUNCE=1, OSG_GW_ASSIGN=2, OSG_GW_INPUT=3,
               OSG_GW_HEARTBEAT=4, OSG_GW_INPUT_ACK=5, OSG_GW_CONFIG_REQUEST=6,
               OSG_GW_CONFIG_RESPONSE=7, OSG_GW_PANEL_EVENT=8 } osg_gateway_message_type_t;
enum { OSG_PANEL_HELD_BYTES=16, OSG_PANEL_MAX_EDGES=6 };
typedef enum { OSG_PANEL_LAYOUT, OSG_PANEL_STATE, OSG_PANEL_EDGES, OSG_PANEL_GAP } osg_panel_kind_t;
typedef enum { OSG_PANEL_GAP_RING=1, OSG_PANEL_GAP_EXHAUSTED=2 } osg_panel_gap_t;
typedef struct {
    uint8_t contact_count, encoder_count, selector_count;
    uint8_t maintained_contacts[OSG_PANEL_HELD_BYTES];
} osg_panel_layout_t;
typedef struct {
    uint8_t held[OSG_PANEL_HELD_BYTES];
    uint16_t axes[OSG_GW_MAX_AXES];
    uint8_t axis_available;
} osg_panel_state_t;
typedef struct { uint8_t control; bool pressed; uint32_t captured_at_ms; } osg_panel_edge_t;
typedef struct { uint8_t count; osg_panel_edge_t entries[OSG_PANEL_MAX_EDGES]; } osg_panel_edges_t;
typedef struct {
    uint32_t id;
    uint8_t length;
    bool extended, fd, brs;
    uint8_t data[OSG_GW_FRAME_CAPACITY];
    uint32_t deadline_ms; /* Local TX driver metadata, never part of CAN bytes. */
    bool deadline_valid; /* RX frames may leave false; queued TX must expire at this time. */
} osg_gateway_frame_t;
typedef struct {
    osg_gateway_message_type_t type;
    uint8_t node;
    uint8_t source_node; /* Cyphal metadata, never serialized; independent of logical node/slot. */
    bool direct_usb;
    osg_gateway_identity_t identity;
    uint32_t boot_session, lease, gateway_epoch, sequence,configuration_generation;
    osg_gateway_profile_t profile; /* ANNOUNCE only */
    uint8_t report[OSG_GW_MAX_REPORT_SIZE]; /* INPUT/INPUT_ACK; unused bytes zero */
    uint16_t config_offset,config_total;
    uint8_t config_length,config_data[OSG_GW_CONFIG_CHUNK_SIZE];
    osg_panel_kind_t panel_kind;
    uint32_t panel_cursor, panel_sampled_at_ms;
    union {
        osg_panel_layout_t panel_layout;
        osg_panel_state_t panel_state;
        osg_panel_edges_t panel_edges;
        osg_panel_gap_t panel_gap;
    };
} osg_gateway_message_t;
bool osg_gateway_identity_valid(const osg_gateway_identity_t *);
bool osg_gateway_identity_equal(const osg_gateway_identity_t *, const osg_gateway_identity_t *);
bool osg_gateway_identity_parse(const char *, osg_gateway_identity_t *); /* exactly32 hex */
void osg_gateway_identity_format(const osg_gateway_identity_t *, char out[33]);
bool osg_gateway_profile_valid(const osg_gateway_profile_t *);
bool osg_gateway_profile_equal(const osg_gateway_profile_t *, const osg_gateway_profile_t *);
size_t osg_gateway_profile_report_size(const osg_gateway_profile_t *);
bool osg_gateway_profile_report_valid(const osg_gateway_profile_t *, const uint8_t report[OSG_GW_MAX_REPORT_SIZE]);
/* Buttons, packed 4-bit hats (8 = neutral), then little-endian uint16 axes. */
void osg_gateway_profile_neutral(const osg_gateway_profile_t *, uint8_t report[OSG_GW_MAX_REPORT_SIZE]);
uint32_t osg_gateway_crc32(const void *, size_t);
uint16_t osg_gateway_subject(osg_gateway_message_type_t);
/* DSDL payload helpers. CAN identifiers/tails/fragmentation are exclusively handled by libcanard. */
size_t osg_gateway_payload_encode(const osg_gateway_message_t *, void *, size_t);
bool osg_gateway_payload_decode(uint16_t subject_id, const void *, size_t, osg_gateway_message_t *);
#endif
