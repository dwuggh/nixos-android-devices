/* SPDX-License-Identifier: MIT */
#include "dsp.h"
#include "protocol.h"
#include "rpc-abi.h"
/* Native replacement for the OEM AP-side callback skeleton, not an Android
 * library. Scalars and primitive input layouts verified in its signed image's
 * companion libarm_proxy_skel.so; never treat DSP logs as format strings. */
__attribute__((visibility("default"))) int arm_proxy_skel_invoke(uint32_t scalar, remote_arg *args)
{
    uint32_t command, result, length;
    switch ((scalar >> 24) & 31) {
    case 0:
        if ((scalar & 0xffffff) != 0x10000 || !args || !args[0].buf.pv || args[0].buf.nLen < 8)
            return 14;
        uf_wire_get32(args[0].buf.pv, 8, 0, &command);
        uf_wire_get32(args[0].buf.pv, 8, 4, &result);
        return uf_dsp_notify(command, (int32_t)result);
    case 1:
        if ((scalar & 0xffffff) != 0x20000 || !args || !args[0].buf.pv || args[0].buf.nLen < 4)
            return 14;
        uf_wire_get32(args[0].buf.pv, 4, 0, &length);
        return length <= args[1].buf.nLen && length <= 65536 &&
                       (!length || args[1].buf.pv) ? 0 : 14;
    default:
        return 20;
    }
}
