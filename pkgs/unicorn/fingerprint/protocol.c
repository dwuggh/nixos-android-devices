/* SPDX-License-Identifier: MIT */
#include "protocol.h"
#include <errno.h>
#include <string.h>

static void put32(unsigned char *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; i++)
        p[i] = v >> (8 * i);
}
static uint32_t get32(const unsigned char *p)
{
    uint32_t v = 0;
    for (unsigned i = 0; i < 4; i++)
        v |= (uint32_t)p[i] << (8 * i);
    return v;
}
static int32_t signed32(const unsigned char *p)
{
    uint32_t v = get32(p);
    return v <= INT32_MAX ? (int32_t)v : -1 - (int32_t)(UINT32_MAX - v);
}

/* libgf_hal6.so call-site constants, not the unrelated gfenu command ABI. */
static const struct {
    uint32_t module, command;
    size_t size;
} commands[] = {
    [UF_AUTHKEY] = {1000, 10, 284},     [UF_WAKE] = {1002, 9, 32},
    [UF_SLEEP] = {1002, 8, 28},         [UF_DETECTOR_INIT] = {1002, 0, 28},
    [UF_DETECTOR_INFO] = {1002, 1, 44}, [UF_SET_GROUP] = {1000, 0, 288},
    [UF_PRE_ENROLL] = {1000, 2, 40},    [UF_ENROLL] = {1000, 3, 108},
    [UF_POST_ENROLL] = {1000, 4, 28},   [UF_AUTHENTICATE] = {1000, 1, 48},
    [UF_ENUMERATE] = {1000, 7, 112},    [UF_REMOVE] = {1000, 6, 120},
    [UF_SAVE] = {1000, 9, 36},          [UF_CANCEL] = {1000, 8, 28},
    [UF_SCREEN] = {1000, 11, 36},       [UF_SENSOR_INIT] = {1002, 2, 640},
    [UF_ALGO_INIT] = {1001, 0, 560},    [UF_CAPTURE] = {1002, 3, 88},
    [UF_READ_IMAGE] = {1002, 101, 88},  [UF_IRQ] = {1002, 5, 32},
    [UF_SENSOR_IDS] = {1001, 22, 48},   [UF_ENROLL_IMAGE] = {1001, 1, 184},
    [UF_AUTH_IMAGE] = {1001, 2, 240},   [UF_ENROLL_END] = {1001, 29, 40},
    [UF_POST_AUTH] = {1000, 12, 52},    [UF_ENROLL_FINISH] = {1001, 24, 40},
    [UF_CALI_ERROR] = {1002, 103, 28},
};
_Static_assert(sizeof(commands) / sizeof(commands[0]) == UF_COMMAND_COUNT, "command table");

size_t uf_command_size(enum uf_command command)
{
    return (unsigned)command < UF_COMMAND_COUNT ? commands[command].size : 0;
}

int uf_prepare(enum uf_command command, void *header, size_t hsize, void *body, size_t bsize,
               uint32_t sequence, uint64_t milliseconds, uint32_t carveout_size)
{
    size_t size = uf_command_size(command);
    if (!header || hsize != 64 || !body || !size || bsize != size || !sequence ||
        milliseconds >= 86400000) {
        errno = EINVAL;
        return -1;
    }
    unsigned char *h = header, *b = body;
    memset(h, 0, hsize);
    memset(b, 0, bsize);
    put32(h + 8, size);
    put32(h + 12, sequence);
    for (unsigned i = 0; i < 8; i++)
        h[16 + i] = milliseconds >> (8 * i);
    put32(h + 40, carveout_size);
    put32(b + 4, commands[command].module);
    put32(b + 8, commands[command].command);
    return 0;
}

int uf_status_decode(const void *header, size_t hsize, const void *body, size_t bsize,
                     struct uf_status *status)
{
    if (!header || hsize != 64 || !body || bsize < 28 || !status) {
        errno = EPROTO;
        return -1;
    }
    const unsigned char *h = header, *b = body;
    *status = (struct uf_status){signed32(h + 28), signed32(b + 12), b[0] != 0};
    return 0;
}

void uf_keymaster_request(unsigned char request[64])
{
    memset(request, 0, 64);
    put32(request, 517);
    put32(request + 4, 2);
}

int uf_keymaster_status(const void *reply, size_t size, int32_t *status)
{
    if (!reply || size != 256 || !status) {
        errno = EPROTO;
        return -1;
    }
    *status = signed32(reply);
    return 0;
}

int uf_wire_put32(void *p, size_t size, size_t offset, uint32_t value)
{
    if (!p || size < 4 || offset > size - 4) {
        errno = EINVAL;
        return -1;
    }
    put32((unsigned char *)p + offset, value);
    return 0;
}

int uf_wire_put64(void *p, size_t size, size_t offset, uint64_t value)
{
    if (!p || size < 8 || offset > size - 8) {
        errno = EINVAL;
        return -1;
    }
    put32((unsigned char *)p + offset, value);
    put32((unsigned char *)p + offset + 4, value >> 32);
    return 0;
}

int uf_wire_get32(const void *p, size_t size, size_t offset, uint32_t *value)
{
    if (!p || !value || size < 4 || offset > size - 4) {
        errno = EPROTO;
        return -1;
    }
    *value = get32((const unsigned char *)p + offset);
    return 0;
}

int uf_wire_get64(const void *p, size_t size, size_t offset, uint64_t *value)
{
    if (!p || !value || size < 8 || offset > size - 8) {
        errno = EPROTO;
        return -1;
    }
    const unsigned char *b = (const unsigned char *)p + offset;
    *value = get32(b) | (uint64_t)get32(b + 4) << 32;
    return 0;
}

int uf_group_encode(void *body, size_t size, uint32_t group, const char *path)
{
    if (!body || size != 288 || !path || !*path || strlen(path) > 255) {
        errno = EINVAL;
        return -1;
    }
    put32((unsigned char *)body + 28, group);
    memset((unsigned char *)body + 32, 0, 256);
    memcpy((unsigned char *)body + 32, path, strlen(path));
    return 0;
}

int uf_enroll_encode(void *body, size_t size, uint32_t group, const uint8_t *hat, size_t hat_size)
{
    if (!body || size != 108 || (hat ? hat_size != UF_HAT_SIZE : hat_size != 0)) {
        errno = EINVAL;
        return -1;
    }
    unsigned char *b = body;
    put32(b + 28, group);
    /* FingerprintCore::enroll 0x3c5c0 through 0x3c5e4. The packed HAT
     * begins at +33, not +32 or the gfenu profile's +110. */
    memset(b + 32, 0, 76);
    if (hat)
        memcpy(b + 33, hat, UF_HAT_SIZE);
    return 0;
}

int uf_enumerate_decode(const void *body, size_t size, struct uf_template *out, size_t capacity,
                        size_t *count)
{
    if (count)
        *count = 0;
    if (!body || size != 112 || !count || (!out && capacity)) {
        errno = EPROTO;
        return -1;
    }
    const unsigned char *b = body;
    uint32_t n = get32(b + 28);
    if (n > UF_TEMPLATE_MAX) {
        errno = EPROTO;
        return -1;
    }
    if (n > capacity) {
        errno = ENOSPC;
        return -1;
    }
    /* notifyEnumerate 0x3f148 uses count at 28 and parallel arrays +32/+72.
     * Reject malformed/duplicate locators before exposing any result. */
    struct uf_template parsed[UF_TEMPLATE_MAX];
    for (uint32_t i = 0; i < n; i++) {
        parsed[i] = (struct uf_template){get32(b + 32 + 4 * i), get32(b + 72 + 4 * i)};
        if (!parsed[i].finger || uf_template_in_set(parsed[i], parsed, i)) {
            errno = EPROTO;
            return -1;
        }
    }
    if (n)
        memcpy(out, parsed, n * sizeof(*out));
    *count = n;
    return 0;
}

int uf_template_in_set(struct uf_template needle, const struct uf_template *set, size_t count)
{
    if (!needle.finger || (!set && count))
        return 0;
    for (size_t i = 0; i < count; i++)
        if (set[i].group == needle.group && set[i].finger == needle.finger)
            return 1;
    return 0;
}

size_t uf_profile_size(enum uf_profile profile, enum uf_command command)
{
    if (profile != UF_PROFILE_GFUNI && profile != UF_PROFILE_GFUNI6)
        return 0;
    if (profile == UF_PROFILE_GFUNI) {
        if (command == UF_SENSOR_INIT)
            return 616;
        if (command == UF_CAPTURE)
            return 80;
        if (command == UF_ENROLL_IMAGE)
            return 176;
    }
    return uf_command_size(command);
}
int uf_profile_prepare(enum uf_profile profile, enum uf_command command, void *header, void *body,
                       size_t size, uint32_t sequence, uint64_t milliseconds,
                       uint32_t carveout_size)
{
    if (!header || !body || !size || size != uf_profile_size(profile, command) || !sequence ||
        milliseconds >= 86400000) {
        errno = EINVAL;
        return -1;
    }
    memset(header, 0, 64);
    memset(body, 0, size);
    unsigned char *h = header, *b = body;
    put32(h + (profile == UF_PROFILE_GFUNI ? 4 : 8), size);
    put32(h + (profile == UF_PROFILE_GFUNI ? 8 : 12), sequence);
    uf_wire_put64(h, 64, 16, milliseconds);
    put32(h + (profile == UF_PROFILE_GFUNI ? 36 : 40), carveout_size);
    put32(b + 4, commands[command].module);
    put32(b + 8, commands[command].command);
    return 0;
}

const char *uf_error_name(int32_t code)
{
    switch (code) {
    case 0:
        return "GF_SUCCESS";
    case 1000:
        return "GF_ERROR_BASE";
    case 1001:
        return "GF_ERROR_OUT_OF_MEMORY";
    case 1002:
        return "GF_ERROR_OPEN_TA_FAILED";
    case 1003:
        return "GF_ERROR_MODULE_NOT_INITED";
    case 1004:
        return "GF_ERROR_BAD_PARAMS";
    case 1005:
        return "GF_ERROR_NO_SPACE";
    case 1006:
        return "GF_ERROR_MEMORY_COPY_FAILED";
    case 1007:
        return "GF_ERROR_REACH_FINGERS_UPLIMIT";
    case 1008:
        return "GF_ERROR_NOT_MATCH";
    case 1009:
        return "GF_ERROR_CANCELED";
    case 1010:
        return "GF_ERROR_TIMEOUT";
    case 1011:
        return "GF_ERROR_PREPROCESS_FAILED";
    case 1012:
        return "GF_ERROR_GENERIC";
    case 1013:
        return "GF_ERROR_ACQUIRED_PARTIAL";
    case 1014:
        return "GF_ERROR_ACQUIRED_IMAGER_DIRTY";
    case 1015:
        return "GF_ERROR_DUPLICATE_FINGER";
    case 1016:
        return "GF_ERROR_OPEN_DEVICE_FAILED";
    case 1017:
        return "GF_ERROR_HAL_GENERAL_ERROR";
    case 1018:
        return "GF_ERROR_HAL_FILE_DESCRIPTION_NULL";
    case 1019:
        return "GF_ERROR_HAL_IOCTL_FAILED";
    case 1020:
        return "GF_ERROR_HAL_TIMER_FUNC";
    case 1021:
        return "GF_ERROR_CORRUPT_CONTENT";
    case 1022:
        return "GF_ERROR_INCORRECT_VERSION";
    case 1023:
        return "GF_ERROR_CORRUPT_OBJECT";
    case 1024:
        return "GF_ERROR_INVALID_DATA";
    case 1025:
        return "GF_ERROR_SPI_TRANSFER_ERROR";
    case 1026:
        return "GF_ERROR_SPI_GENERAL_ERROR";
    case 1027:
        return "GF_ERROR_SPI_IRQ_HANDLE";
    case 1028:
        return "GF_ERROR_SPI_RAW_DATA_CRC_FAILED";
    case 1029:
        return "GF_ERROR_SPI_RAW_DATA_BUF_BUSY";
    case 1030:
        return "GF_ERROR_SPI_FW_CFG_DATA_ERROR";
    case 1031:
        return "GF_ERROR_SPI_FW_DOWNLOAD_FAILED";
    case 1032:
        return "GF_ERROR_SPI_CFG_DOWNLOAD_FAILED";
    case 1033:
        return "GF_ERROR_SAVE_FP_TEMPLATE";
    case 1034:
        return "GF_ERROR_FP_BUSY";
    case 1035:
        return "GF_ERROR_OPEN_SECURE_OBJECT_FAILED";
    case 1036:
        return "GF_ERROR_READ_SECURE_OBJECT_FAILED";
    case 1037:
        return "GF_ERROR_WRITE_SECURE_OBJECT_FAILED";
    case 1038:
        return "GF_ERROR_DELETE_SECURE_OBJECT_FAILED";
    case 1039:
        return "GF_ERROR_SECURE_OBJECT_NOT_EXIST";
    case 1040:
        return "GF_ERROR_WRITE_CONFIG_FAILED";
    case 1041:
        return "GF_ERROR_TEST_SENSOR_FAILED";
    case 1042:
        return "GF_ERROR_SET_MODE_FAILED";
    case 1043:
        return "GF_ERROR_CHIP_ID_NOT_CORRECT";
    case 1044:
        return "GF_ERROR_MAX_NUM";
    case 1045:
        return "GF_ERROR_TEST_BAD_POINT_FAILED";
    case 1046:
        return "GF_ERROR_TEST_FRR_FAR_ENROLL_DIFFERENT_FINGER";
    case 1047:
        return "GF_ERROR_DUPLICATE_AREA";
    case 1048:
        return "GF_ERROR_SPI_COMMUNICATION";
    case 1049:
        return "GF_ERROR_FINGER_NOT_EXIST";
    case 1050:
        return "GF_ERROR_INVALID_PREPROCESS_VERSION";
    case 1051:
        return "GF_ERROR_TA_DEAD";
    case 1052:
        return "GF_ERROR_NAV_TOO_FAST";
    case 1053:
        return "GF_ERROR_UNSUPPORT_CHIP";
    case 1054:
        return "GF_ERROR_INVALID_FINGER_PRESS";
    case 1055:
        return "GF_ERROR_TA_GENERATE_RANDOM";
    case 1056:
        return "GF_ERROR_BIO_ASSAY_FAIL";
    case 1057:
        return "GF_ERROR_INVALID_HAT_VERSION";
    case 1058:
        return "GF_ERROR_INVALID_CHALLENGE";
    case 1059:
        return "GF_ERROR_UNTRUSTED_ENROLL";
    case 1060:
        return "GF_ERROR_INVALID_BASE";
    case 1061:
        return "GF_ERROR_SENSOR_BROKEN_CHECK_NEXT_FRAME";
    case 1062:
        return "GF_ERROR_SENSOR_BROKEN_CHECK_ALGO_ERROR";
    case 1063:
        return "GF_ERROR_SENSOR_IS_BROKEN";
    case 1064:
        return "GF_ERROR_SENSOR_NOT_AVAILABLE";
    case 1065:
        return "GF_ERROR_SENSOR_TEST_FAILED";
    case 1066:
        return "GF_ERROR_MATCH_FAIL_AND_RETRY";
    case 1067:
        return "GF_ERROR_FINGER_NOT_LEAVE";
    case 1068:
        return "GF_ERROR_GET_TEMPERATURE_FAILED";
    case 1069:
        return "GF_ERROR_GET_OTP_INFO_FAILED";
    case 1070:
        return "GF_ERROR_TEST_RESET_PIN";
    case 1071:
        return "GF_ERROR_TEST_INTERRUPT_PIN";
    case 1072:
        return "GF_ERROR_UNKNOWN_CMD";
    case 1073:
        return "GF_ERROR_TEST_SPI_TRANSFER_TIMEOUT";
    case 1074:
        return "GF_ERROR_ENCODE_TLV";
    case 1075:
        return "GF_ERROR_INVALID_FP_ID";
    case 1076:
        return "GF_ERROR_NATIVE_SERVICE_BASE";
    case 1077:
        return "GF_ERROR_NATIVE_SERVICE_GETSERVICE";
    case 1078:
        return "GF_ERROR_NATIVE_SERVICE_INIT_HAL";
    case 1079:
        return "GF_ERROR_MKDIR_FAILED";
    case 1080:
        return "GF_ERROR_FILE_OPEN_FAILED";
    case 1081:
        return "GF_ERROR_FILE_NOT_EXIST";
    case 1082:
        return "GF_ERROR_FILE_READ_FAILED";
    case 1083:
        return "GF_ERROR_FILE_WRITE_FAILED";
    case 1084:
        return "GF_ERROR_FILE_DELETE_FAILED";
    case 1085:
        return "GF_ERROR_TOO_MUCH_UNDER_SATURATED_PIXELS";
    case 1086:
        return "GF_ERROR_TOO_MUCH_OVER_SATURATED_PIXELS";
    case 1087:
        return "GF_ERROR_SENSOR_HAS_CHANGED";
    case 1088:
        return "GF_ERROR_OPEN_SESSION_FAILED";
    case 1089:
        return "GF_ERROR_FACTORY_TEST_FAILED";
    case 1090:
        return "GF_ERROR_NO_GROUP_ID";
    case 1091:
        return "GF_ERROR_UNKNOWN_TEST_TOKEN";
    case 1092:
        return "GF_FINGER_FEATURE_STUDY_FAILED";
    case 1093:
        return "GF_ERROR_INVALID_OTP_VERSION";
    case 1094:
        return "GF_ERROR_CALIBRATION_NOT_READY";
    case 1095:
        return "GF_ERROR_ALGO_INVALID_DATA";
    case 1096:
        return "GF_ERROR_TOO_FAST";
    case 1097:
        return "GF_ERROR_TEST_NOT_SUPPORTED";
    case 1098:
        return "GF_ERROR_NO_NEED_UPDATE_BASE";
    case 1099:
        return "GF_ERROR_OTP_CHECK_FAIL";
    case 1100:
        return "GF_ERROR_INIT_ALGO_REGISTER_INFO";
    case 1101:
        return "GF_ERROR_DYNAMIC_ENROLL_INVALID_PRESS_TOO_MUCH";
    case 1102:
        return "GF_ERROR_DYNAMIC_ERNOLL_INCOMPLETE_TEMPLATE";
    case 1103:
        return "GF_ERROR_ALGO_DIRTY_FINGER";
    case 1104:
        return "GF_ERROR_PREPROCESS_FAIL_AND_RETRY";
    case 1105:
        return "GF_ERROR_CRC_CHECK_FAILED_AND_RETRY";
    case 1106:
        return "GF_ERROR_ALGO_COVER_BROKEN";
    case 1108:
        return "GF_ERROR_FARGO_CHIP_ID_NOT_CORRECT";
    case 1109:
        return "GF_ERROR_CREATE_FINGER_TEMPLATE";
    case 1110:
        return "GF_ERROR_DUMP_CONFIG_NOT_CORRECT";
    case 1111:
        return "GF_ERROR_INVALID_STATE";
    case 1112:
        return "GF_ERROR_FAILED_AND_RETRY";
    case 1113:
        return "GF_ERROR_MT_INFO_CHECK_FAIL";
    case 1114:
        return "GF_ERROR_TEST_RESET_INT_PIN_MCU";
    case 1115:
        return "GF_ERROR_TEST_RESET_INT_PIN_SENSOR";
    case 1116:
        return "GF_ERROR_TEST_RESET_INT_PIN_PMIC";
    case 1117:
        return "GF_ERROR_TEST_MT";
    case 1118:
        return "GF_ERROR_TEST_SPMT";
    case 1119:
        return "GF_ERROR_TEST_OTP";
    case 1120:
        return "GF_ERROR_CHIP_IS_NOT_READY";
    case 1121:
        return "GF_ERROR_FACTORY_FOV_INVALID";
    case 1122:
        return "GF_ERROR_FACTORY_BADPOINT_INVALID";
    case 1123:
        return "GF_ERROR_FACTORY_SCALE_INVALID";
    case 1124:
        return "GF_ERROR_FACTORY_TNOISE_INVALID";
    case 1125:
        return "GF_ERROR_FACTORY_SNOISE_INVALID";
    case 1126:
        return "GF_ERROR_FACTORY_FLESH_TOUCHDIFF_INVALID";
    case 1127:
        return "GF_ERROR_FACTORY_LIGHT_LEAK_RATIO_INVALID";
    case 1128:
        return "GF_ERROR_FACTORY_RELATIVEILLUMINANCE_INVALID";
    case 1129:
        return "GF_ERROR_FACTORY_CROP_INVALID";
    case 1130:
        return "GF_ERROR_FACTORY_SSNR_INVALID";
    case 1131:
        return "GF_ERROR_FACTORY_SHAPENESS_INVALID";
    case 1132:
        return "GF_ERROR_FACTORY_CONSTRAST_INVALID";
    case 1133:
        return "GF_ERROR_FACTORY_RAWDATA_INVALID";
    case 1134:
        return "GF_ERROR_HOLD_MCU_FAILED";
    case 1135:
        return "GF_ERROR_CAPTURE_IMAGE_FAILED";
    case 1136:
        return "GF_ERROR_NO_NEED_FORCE_STUDY";
    case 1137:
        return "GF_ERROR_NOT_LIVE_FINGER";
    case 1138:
        return "GF_ERROR_RESIDUAL_FINGER";
    case 1139:
        return "GF_ERROR_FACTORY_SCREEN_STRUCT_INVALID";
    case 1140:
        return "GF_ERROR_ALGO_INIT_FAILED";
    case 1141:
        return "GF_ERROR_NOT_MATCH_NOT_LIVE_FINGER";
    case 1142:
        return "GF_ERROR_ENROLL_AUTH_TOKEN_TIME_OUT";
    case 1143:
        return "GF_ERROR_UI_READY_TIMEOUT";
    case 1144:
        return "GF_ERROR_FACTORY_TILT_LEVEL_INVALID";
    case 1145:
        return "GF_ERROR_ALG_ANTIPEEPING_FINGER";
    case 1146:
        return "GF_ERROR_ALG_SCREEN_STRUCT_IMG";
    case 1147:
        return "GF_ERROR_ACQUIRED_IMAGER_DRY";
    case 1148:
        return "GF_ERROR_ACQUIRED_IMAGER_WET";
    case 1149:
        return "GF_ERROR_FACTORY_CAHRT_DIRECTION_INVALID";
    case 1150:
        return "GF_ERROR_GPIO_FAILED";
    case 1151:
        return "GF_ERROR_DSP_NOT_AVAILABLE";
    case 1152:
        return "GF_ERROR_DSP_WAIT_TIMEOUT";
    case 1153:
        return "GF_ERROR_DSP_GET_FEATURE_FAIL";
    case 1155:
        return "GF_ERROR_UI_DISAPPEAR";
    case 1156:
        return "GF_ERROR_FARR_MOCK_DISABLE";
    case 1157:
        return "GF_ERROR_ACQUIRED_NOT_LIVE_FINGER";
    case 1158:
        return "GF_ERROR_RPMB_GENERAL_ERROR";
    case 1159:
        return "GF_ERROR_RPMB_WRITE_ERROR";
    case 1160:
        return "GF_ERROR_RPMB_READ_ERROR";
    case 1161:
        return "GF_ERROR_CHECK_PERFORMANCE_TESTING_RESULT_NOT_PASS";
    case 1162:
        return "GF_ERROR_ALG_LOGO_DETECTION_FAILED";
    case 1163:
        return "GF_ERROR_BRIGHTNESS_CHANGE_FAIL";
    case 1164:
        return "GF_ERROR_BRIGHTNESS_NOT_STABILITY";
    case 1165:
        return "GF_ERROR_CHART_DIRECTION_INCORRECT";
    case 1166:
        return "GF_ERROR_TEST_SMT";
    case 1167:
        return "GF_ERROR_ANOMALY_FINGER";
    case 1168:
        return "GF_ERROR_NEED_CANCLE_ENROLL";
    case 1170:
        return "GF_ERROR_TA_TRANSFER_SIZE";
    case 1171:
        return "GF_ERROR_TEMPORARY_LOCKOUT";
    case 1172:
        return "GF_ERROR_PERMANENT_LOCKOUT";
    case 1500:
        return "GF_ERROR_SPI_TEST";
    case 1501:
        return "GF_ERROR_RST_INT_TEST";
    case 1502:
        return "GF_ERROR_OTP_FLASH_TEST";
    case 1503:
        return "GF_ERROR_GET_CALIBRATION";
    case 1504:
        return "GF_ERROR_LOC_CIRCLE_TEST";
    case 1505:
        return "GF_ERROR_BAD_POINT_CLUSTER";
    case 1506:
        return "GF_ERROR_SNOISE";
    case 1507:
        return "GF_ERROR_TNOISE";
    case 1508:
        return "GF_ERROR_SIGNAL";
    case 1509:
        return "GF_ERROR_FLATSNOISE";
    case 1510:
        return "GF_ERROR_SSNR";
    case 1511:
        return "GF_ERROR_TSNR";
    case 1512:
        return "GF_ERROR_SHARPNESS";
    case 1513:
        return "GF_ERROR_HIGH_LIGHT";
    case 1514:
        return "GF_ERROR_SWITCH_BRIGHTNESS";
    case 1515:
        return "GF_ERROR_BRIGHTNESS_INSTABILITY";
    case 1516:
        return "GF_ERROR_PRESS_INCOMPLETE";
    case 1517:
        return "GF_ERROR_CHART_DIRECTION";
    case 1518:
        return "GF_ERROR_BLACK_RUBBER_LEAKAGE";
    case 1519:
        return "GF_ERROR_CALI_TEST_STAGE";
    case 1520:
        return "GF_ERROR_CALI_INFO_HEADER_CRC";
    case 1521:
        return "GF_ERROR_CALI_INFO_CRC";
    case 1522:
        return "GF_ERROR_HAF_BAD_POINT_CLUSTER";
    case 1523:
        return "GF_ERROR_DARK_PIXEL_PARAM";
    case 1524:
        return "GF_ERROR_SCREEN_PARAM";
    case 1525:
        return "GF_ERROR_CALCULATE_PGA_GAIN";
    case 1526:
        return "GF_ERROR_MORPHOTYPE_TEST";
    case 1527:
        return "GF_ERROR_PRODUCT_ALGO_INTERFACE";
    case 1528:
        return "GF_ERROR_CHIP_TYPE_INCORRECT";
    case 1529:
        return "GF_ERROR_TEMPERATURE_ADC_BASE";
    case 1530:
        return "GF_ERROR_DIAMOND_K";
    case 1531:
        return "GF_ERROR_POLAR_DEGREE";
    case 1532:
        return "GF_ERROR_CHART_GHOST_SHADOW";
    case 1533:
        return "GF_ERROR_CHART_DIRTY";
    case 1534:
        return "GF_ERROR_ANTIFAKE_OFFSET";
    case 1535:
        return "GF_ERROR_SENSOR_ID_DISMATCH";
    case 1536:
        return "GF_ERROR_HRD_NOISE";
    case 1537:
        return "GF_ERROR_FLESH_3D_EXCHANGE";
    case 1538:
        return "GF_ERROR_HBLACK_RANGE";
    case 1539:
        return "GF_ERROR_TEST_HEAD_DEPTH";
    case 2000:
        return "GF_SZ_FT_PASS";
    case 2019:
        return "GF_SZ_FT_ERROR_INIT_PARAM";
    case 2020:
        return "GF_SZ_FT_ERROR_BAD_PIXEL_OVERPROOF";
    case 2021:
        return "GF_SZ_FT_ERROR_ANC_PREPROCESS";
    case 2022:
        return "GF_SZ_FT_ERROR_RAWDATA_INVALID";
    case 2023:
        return "GF_SZ_FT_ERROR_GET_IMAGE_MASK_FAIL";
    case 2024:
        return "GF_SZ_FT_ERROR_COMM_FAIL";
    case 2025:
        return "GF_SZ_FT_ERROR_READ_RAWDATA_TIMEOUT";
    case 2026:
        return "GF_SZ_FT_ERROR_NOISE_TEST_FAIL";
    case 2027:
        return "GF_SZ_FT_ERROR_Q_VALUR_ABNORMAL";
    case 2028:
        return "GF_SZ_FT_ERROR_CROP_PEREMETER_INVALID";
    case 2029:
        return "GF_SZ_FT_ERROR_SCREEN_STRUCT_INVALID";
    case 2030:
        return "GF_SZ_FT_ERROR_CHART_DIRECTION_INVALID";
    case 2033:
        return "GF_SZ_FT_ERROR_CALCULATE_KB_FAIL";
    case 2034:
        return "GF_SZ_FT_ERROR_GET_TNOISE_FAIL";
    case 2035:
        return "GF_SZ_FT_ERROR_GET_SNOISE_FAIL";
    case 2036:
        return "GF_SZ_FT_ERROR_GET_PREPRESS_PARAM_FAIL";
    case 2038:
        return "GF_SZ_FT_ERROR_GET_RAWDATA_FAIL";
    case 2039:
        return "GF_SZ_FT_ERROR_SHARPNESS_ABNORMAL";
    case 2040:
        return "GF_SZ_FT_ERROR_SINGNAL_TEST_FAIL";
    case 2041:
        return "GF_SZ_FT_ERROR_SSNR_TEST_FAIL";
    case 2045:
        return "GF_SZ_FT_ERROR_KB_CALIBRATION_FAIL";
    case 2046:
        return "GF_SZ_FT_ERROR_READ_FLASH_FAIL";
    case 2047:
        return "GF_SZ_FT_ERROR_WRITE_FLASH_FAIL";
    case 2048:
        return "GF_SZ_FT_ERROR_CHECK_MT_FAIL";
    case 2049:
        return "GF_SZ_FT_ERROR_WRITE_RESUTL_TO_FLASH_FAIL";
    case 2051:
        return "GF_SZ_FT_ERROR_CHIP_ID_CHECK_FAIL";
    case 2052:
        return "GF_SZ_FT_ERROR_FOV_TEST_FAIL";
    case 2053:
        return "GF_SZ_FT_ERROR_SCALE_ABNORMAL";
    case 2054:
        return "GF_SZ_FT_ERROR_ILLUMINANCE_ABNORMAL";
    case 2055:
        return "GF_SZ_FT_ERROR_TOUCH_DIFF_ABNORMAL";
    case 2056:
        return "GF_SZ_FT_ERROR_CONTRAST_ABNORMAL";
    case 2057:
        return "GF_SZ_FT_ERROR_LDC_TEST_ABNORMAL";
    case 2058:
        return "GF_SZ_FT_ERROR_SELF_EXPOSURE_ABNORMAL";
    case 2059:
        return "GF_SZ_FT_ERROR_VENDOR_ID_ERROR";
    case 2060:
        return "GF_SZ_FT_ERROR_LEAK_LIGHT_ABNOEMAL";
    case 2093:
        return "GF_SZ_FT_ERROR_LOG_IS_OPENED";
    case 2094:
        return "GF_SZ_FT_ERROR_NOT_SUPPORT_TEST";
    case 2112:
        return "GF_SZ_FT_ERROR_BAD_PIXEL_PARAM_INIT_FAIL";
    case 2113:
        return "GF_SZ_FT_ERROR_CHECK_BAD_PIXEL_FAIL";
    case 2114:
        return "GF_SZ_FT_ERROR_ANC_PARAM_INIT_FAIL";
    case 2115:
        return "GF_SZ_FT_ERROR_GET_FOV_FAIL";
    case 2116:
        return "GF_SZ_FT_ERROR_SCREEN_STRUCT_PARAM_INIT_FAIL";
    case 2117:
        return "GF_SZ_FT_ERROR_GET_SCREEN_STRUCT_RATIO_FAIL";
    case 2118:
        return "GF_SZ_FT_ERROR_CALCULATE_KB_PARAM_INIT_FAIL";
    case 2119:
        return "GF_SZ_FT_ERROR_GET_SCALE_RATIO_FAIL";
    case 2120:
        return "GF_SZ_FT_ERROR_QUALITY_CONTROL_PARAM_INIT_FAIL";
    case 2121:
        return "GF_SZ_FT_ERROR_SCALE_RATIO_PARAM_INIT_FAIL";
    case 2122:
        return "GF_SZ_FT_ERROR_TNOISE_PARAM_INIT_FAIL";
    case 2123:
        return "GF_SZ_FT_ERROR_TNOISE_OVERPROOF";
    case 2124:
        return "GF_SZ_FT_ERROR_SNOISE_PARAM_INIT_FAIL";
    case 2125:
        return "GF_SZ_FT_ERROR_SNOISE_OVERPROOF";
    case 2126:
        return "GF_SZ_FT_ERROR_TOUCH_DIFF_PARAM_INIT_FAIL";
    case 2127:
        return "GF_SZ_FT_ERROR_GET_TOUCH_DIFF_FAIL";
    case 2128:
        return "GF_SZ_FT_ERROR_LEAK_LIGHT_PARAM_INIT_FAIL";
    case 2129:
        return "GF_SZ_FT_ERROR_GET_LEAK_LIGHT_FAIL";
    case 2130:
        return "GF_SZ_FT_ERROR_ILLUMINANCE_PARAM_INIT_FAIL";
    case 2131:
        return "GF_SZ_FT_ERROR_GET_ILLUMINANCE_FAIL";
    case 2132:
        return "GF_SZ_FT_ERROR_SSNR_PARAM_INIT_FAIL";
    case 2133:
        return "GF_SZ_FT_ERROR_GET_SSNR_PTOP_FAIL";
    case 2134:
        return "GF_SZ_FT_ERROR_CHART_DIRECTION_PARAM_INIT_FAIL";
    case 2135:
        return "GF_SZ_FT_ERROR_SHARPNESS_PARAM_INIT_FAIL";
    case 2136:
        return "GF_SZ_FT_ERROR_GET_SHARPNESS_FAIL";
    case 2137:
        return "GF_SZ_FT_ERROR_TC_PARAM_INIT_FAIL";
    case 2138:
        return "GF_SZ_FT_ERROR_GET_TC_FAIL";
    case 2139:
        return "GF_SZ_FT_ERROR_SET_HBM_MODE_FAIL";
    case 2140:
        return "GF_SZ_FT_ERROR_CLOSE_HBM_MODE_FAIL";
    case 2141:
        return "GF_SZ_FT_ERROR_SET_HIGH_BRIGHTNESS_FAIL";
    case 2142:
        return "GF_SZ_FT_ERROR_SET_LOW_BRIGHTNESS_FAIL";
    case 2143:
        return "GF_SZ_FT_ERROR_LOCAL_AREA_SAMPLE_FAIL";
    case 2144:
        return "GF_SZ_FT_ERROR_CENTER_OFFSET_INIT_FAIL";
    case 2145:
        return "GF_SZ_FT_ERROR_CHECK_CENTER_OFFSET_FAIL";
    case 2146:
        return "GF_SZ_FT_ERROR_CHECK_LENS_MODULE_TILT";
    case 2147:
        return "GF_SZ_FT_ERROR_LENS_MODULE_TILT_OVERPROOF";
    case 2148:
        return "GF_SZ_FT_ERROR_CF_MASK_CHECK_FAIL";
    case 2149:
        return "GF_SZ_FT_ERROR_RAW_DATA";
    case 2255:
        return "GF_SZ_FT_ERROR_APPLICATION_EXCEPTION";
    case 2150:
        return "GF_SZ_FT_ERROR_MOIRE_PARAM_INIT_FAIL";
    case 2151:
        return "GF_SZ_FT_ERROR_CHECK_MOIRE_FAIL";
    case 2152:
        return "GF_SZ_FT_ERROR_AUTO_EXPOSURE";
    case 2153:
        return "GF_SZ_FT_ERROR_CHECK_FLESH_INIT_FAIL";
    case 2154:
        return "GF_SZ_FT_ERROR_CHECK_FLESH_CHECK_FAIL";
    case 2155:
        return "GF_SZ_FT_ERROR_CHECK_FLESH_FAIL";
    case 2156:
        return "GF_SZ_FT_ERROR_GET_PATTERN_INIT_FAIL";
    case 2157:
        return "GF_SZ_FT_ERROR_GET_PATTERN_CHECK_FAIL";
    case 2160:
        return "GF_SZ_FT_ERROR_IRREGULAR_SPOT_INIT_FAIL";
    case 2161:
        return "GF_SZ_FT_ERROR_IRREGULAR_SPOT_CHECK_FAIL";
    case 2158:
        return "GF_SZ_FT_ERROR_CALC_SHELTER_FAIL";
    case 2159:
        return "GF_SZ_FT_ERROR_CHECK_SHELTER_FAIL";
    case 2164:
        return "GF_SZ_FT_ERROR_SCALE_RATIO_DIVIDE_BY_0";
    case 2165:
        return "GF_SZ_FT_ERROR_SCALE_RATIO_ERR_LINE_NUM";
    case 2166:
        return "GF_SZ_FT_ERROR_SCALE_RATIO_ERR_PARALLEL_LINE";
    case 2167:
        return "GF_SZ_FT_ERROR_SCALE_RATIO_ERR_CROSS_LINE";
    case 2168:
        return "GF_SZ_FT_ERROR_LB_BASE_INIT_FAIL";
    case 2169:
        return "GF_SZ_FT_ERROR_LB_BASE";
    case 2170:
        return "GF_SZ_FT_ERROR_LB_BASE_CHECK_FAILED";
    case 2256:
        return "INVALID_ERROR";
    default:
        return "GF_UNKNOWN_ERROR";
    }
}
