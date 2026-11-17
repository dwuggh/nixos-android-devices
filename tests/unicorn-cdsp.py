"""Check the actual compiled CDSP binding and every signed firmware segment."""
import importlib.util
import pathlib
import struct
import subprocess
import sys

dtb, firmware, assembler = map(pathlib.Path, sys.argv[1:])


def prop(node, name, kind="s"):
    return subprocess.check_output(
        ["fdtget", "-t", kind, str(dtb), node, name], text=True
    ).strip()


node = "/soc@0/remoteproc@32300000"
assert prop(node, "status") == "okay"
assert prop(node, "firmware-name") == "qcom/sm8475/xiaomi/unicorn/cdsp.mdt"
assert prop(node, "compatible") == "qcom,sm8450-cdsp-pas"
# qcom_q6v5_prepare votes this path before releasing DSP reset. A valid image
# and enabled node alone cannot establish the missing NSP/DDR boot vote.
icc = prop(node, "interconnects", "x").split()
assert len(icc) == 6
assert icc[0] == prop("/soc@0/interconnect@320c0000", "phandle", "x")
assert icc[3] == prop("/interconnect-1", "phandle", "x")
assert icc[1:3] == ["1", "7"] and icc[4:] == ["1", "7"]
channel = node + "/glink-edge/fastrpc"
assert prop(channel, "compatible") == "qcom,fastrpc"
assert prop(channel, "label") == "cdsp"
assert prop(channel, "qcom,glink-channels") == "fastrpcglink-apps-dsp"
memory = "/reserved-memory/memory@89900000"
assert prop(node, "memory-region", "x") == prop(memory, "phandle", "x")
assert prop(memory, "reg", "x").split() == ["0", "89900000", "0", "2000000"]
assert prop(memory, "no-map", "x") == ""

spec = importlib.util.spec_from_file_location("firmware_assembler", assembler)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
files = {p.name: p.read_bytes() for p in firmware.glob("cdsp.*")}
report = module.validate_mdt("cdsp.mdt", files)
assert report["memory_span_bytes"] <= 0x2000000
header = struct.unpack_from("<16sHHIIIIIHHHHHH", files["cdsp.mdt"])
for index in range(header[10]):
    ph = struct.unpack_from("<IIIIIIII", files["cdsp.mdt"], header[5] + index * header[9])
    if ph[0] == 1 and ph[5]:
        assert ph[6] & (1 << 27), "CDSP image must support relocation into its reservation"
print("CDSP: compiled enable/firmware/FastRPC bindings, reservation, signed split ELF and relocation bounds passed")
