#!/usr/bin/python3 -I
"""pve: /usr/local/sbin/gpu-regs.py – GC-Statusregister der Renoir-iGPU (gfx9) über debugfs amdgpu_regs lesen.
Ausgabe: globale Busy-Register und alle HQDs (MEC 1–2, Pipe 0–3, Queue 0–7) mit CP_HQD_ACTIVE != 0.
Offsets aus gc_9_0_offset.h, GC-Basis Renoir SEG0 0x2000, SEG1 0xA000 (renoir_ip_offset.h); Byte-Adresse = (Basis+Reg)*4.
debugfs-Kodierung (amdgpu_debugfs_process_reg_op): Bit 61 = SRBM-Auswahl, me Bits 24–33, pipe 34–43, queue 44–53."""
import glob, os, struct, sys

SEG0, SEG1 = 0x2000, 0xA000
GLOBAL = [("GRBM_STATUS", SEG0, 0x0004), ("GRBM_STATUS2", SEG0, 0x0002), ("CP_STAT", SEG0, 0x01a0),
          ("CP_CPC_STATUS", SEG0, 0x0084), ("CP_CPC_BUSY_STAT", SEG0, 0x0085), ("CP_CPC_STALLED_STAT1", SEG0, 0x0086),
          ("CP_MEC_CNTL", SEG0, 0x008d), ("RLC_STAT", SEG1, 0x4c04), ("RLC_SAFE_MODE", SEG1, 0x4c05),
          ("RLC_GPM_STAT", SEG1, 0x4c40), ("RLC_CNTL", SEG1, 0x4c00), ("RLC_CGCG_CGLS_CTRL", SEG1, 0x4c49),
          ("RLC_CGCG_CGLS_CTRL_3D", SEG1, 0x4cc5), ("RLC_CGTT_MGCG_OVERRIDE", SEG1, 0x4c48), ("RLC_PG_CNTL", SEG1, 0x4c43),
          ("CP_INT_CNTL_RING0", SEG0, 0x106a), ("CP_ME_CNTL", SEG0, 0x01b6),
          # Lauf 3: Fetcher/CP-Stall-Ursachen, Paketköpfe, UTCL1 (Adressübersetzung) und GC-VM-Fehler
          ("CP_CPF_STATUS", SEG0, 0x0087), ("CP_CPF_BUSY_STAT", SEG0, 0x0088), ("CP_CPF_STALLED_STAT1", SEG0, 0x0089),
          ("CP_BUSY_STAT", SEG0, 0x019f), ("CP_STALLED_STAT1", SEG0, 0x019d), ("CP_STALLED_STAT2", SEG0, 0x019e),
          ("CP_STALLED_STAT3", SEG0, 0x019c), ("CP_MEC_ME1_HEADER_DUMP", SEG0, 0x008e), ("CP_MEC_ME2_HEADER_DUMP", SEG0, 0x008f),
          ("CP_ME_HEADER_DUMP", SEG0, 0x01a1), ("CP_PFP_HEADER_DUMP", SEG0, 0x01a2), ("CP_CPF_GRBM_FREE_COUNT", SEG0, 0x0092),
          ("CPC_UTCL1_STATUS", SEG0, 0x11b5), ("CPF_UTCL1_STATUS", SEG0, 0x11b6), ("CPG_UTCL1_STATUS", SEG0, 0x11b4),
          ("VM_L2_PROTECTION_FAULT_STATUS", SEG0, 0x084b)]
# Einziges beschreibbares Register (Gegenprobe Leerlauf-Interrupts): gpu-regs.py --setze CP_INT_CNTL_RING0 0x...
SCHREIBBAR = {"CP_INT_CNTL_RING0": (SEG0, 0x106a)}
HQD = [("ACTIVE", 0x1247), ("VMID", 0x1248), ("MQD_LO", 0x1245), ("PQ_CONTROL", 0x1256), ("PQ_RPTR", 0x124f),
       ("PQ_WPTR_LO", 0x127b), ("DEQUEUE_REQ", 0x125d)]

dev = glob.glob("/sys/kernel/debug/dri/*/amdgpu_regs")
pfad = [d for d in dev if "0000:" in d][0] if any("0000:" in d for d in dev) else dev[0]
if len(sys.argv) == 4 and sys.argv[1] == "--setze" and sys.argv[2] in SCHREIBBAR:
    seg, reg = SCHREIBBAR[sys.argv[2]]
    wfd = os.open(pfad, os.O_WRONLY)
    os.pwrite(wfd, struct.pack("<I", int(sys.argv[3], 0)), (seg + reg) * 4)
    os.close(wfd)
    print(f"{sys.argv[2]} := {int(sys.argv[3], 0):#010x}")
    sys.exit(0)
if len(sys.argv) > 1:
    sys.exit(f"Aufruf: {sys.argv[0]} [--setze {'|'.join(SCHREIBBAR)} WERT]")
fd = os.open(pfad, os.O_RDONLY)

def rd(seg, reg, me=None, pipe=0, queue=0):
    pos = (seg + reg) * 4
    if me is not None:
        pos |= (1 << 61) | (me << 24) | (pipe << 34) | (queue << 44)
    try:
        return struct.unpack("<I", os.pread(fd, 4, pos))[0]
    except OSError as e:
        return f"ERR{e.errno}"

def fmt(v, breite=10):
    return f"{v:#0{breite}x}" if isinstance(v, int) else str(v)

print(" ".join(f"{n}={fmt(rd(s, r))}" for n, s, r in GLOBAL))
aktiv, fehler = 0, 0
for me in (1, 2):
    for pipe in range(4):
        for queue in range(8):
            a = rd(SEG0, 0x1247, me, pipe, queue)
            if not isinstance(a, int):
                fehler += 1
                continue
            if a != 0:
                aktiv += 1
                werte = " ".join(f"{n}={fmt(rd(SEG0, r, me, pipe, queue), 0)}" for n, r in HQD)
                print(f"HQD me{me} pipe{pipe} q{queue}: {werte}")
print(f"aktive HQDs: {aktiv} (MEC2 Pipe 2/3 spiegeln auf Renoir Pipe 0/1){' Lesefehler: ' + str(fehler) if fehler else ''}")
