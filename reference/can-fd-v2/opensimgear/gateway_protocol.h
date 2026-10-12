#ifndef OSG_GATEWAY_PROTOCOL_H
#define OSG_GATEWAY_PROTOCOL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Private bring-up protocol, NOT Cyphal/CAN or the canonical OSG protocol. */
enum { OSG_GW_PROTOCOL_VERSION=2, OSG_GW_MAX_MODULES=10,
       OSG_GW_IDENTITY_SIZE=16, OSG_GW_MAX_AXES=8, OSG_GW_MAX_BUTTONS=128,
       OSG_GW_MAX_HATS=4, OSG_GW_MAX_REPORT_SIZE=34, OSG_GW_FRAME_CAPACITY=64 };
#define OSG_GW_CAN_BASE UINT32_C(0x1a500000)
#define OSG_GW_CAN_MASK UINT32_C(0x1ffff800)
typedef struct { uint8_t bytes[OSG_GW_IDENTITY_SIZE]; } osg_gateway_identity_t;
typedef struct {
    uint8_t button_count, axis_count, hat_count;
    uint16_t revision;
    uint8_t axis_usage[OSG_GW_MAX_AXES];
    uint16_t axis_neutral[OSG_GW_MAX_AXES];
} osg_gateway_profile_t;
typedef enum { OSG_GW_ANNOUNCE=1, OSG_GW_ASSIGN=2, OSG_GW_INPUT=3,
               OSG_GW_HEARTBEAT=4, OSG_GW_INPUT_ACK=5 } osg_gateway_message_type_t;
typedef struct {
    uint32_t id;
    uint8_t length;
    bool extended, fd, brs;
    uint8_t data[OSG_GW_FRAME_CAPACITY];
} osg_gateway_frame_t;
typedef struct {
    osg_gateway_message_type_t type;
    uint8_t node;
    bool direct_usb;
    osg_gateway_identity_t identity;
    uint32_t boot_session, lease, gateway_epoch, sequence;
    osg_gateway_profile_t profile; /* ANNOUNCE only */
    uint8_t report[OSG_GW_MAX_REPORT_SIZE]; /* INPUT/INPUT_ACK; unused bytes zero */
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
bool osg_gateway_frame_encode(const osg_gateway_message_t *, osg_gateway_frame_t *);
bool osg_gateway_frame_decode(const osg_gateway_frame_t *, osg_gateway_message_t *);
#endif
