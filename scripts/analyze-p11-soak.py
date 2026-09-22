import re, sys, collections
rec = open(sys.argv[1], errors="replace").read().splitlines()
col = open(sys.argv[2], errors="replace").read().splitlines()

# Only the last boot: the RTT buffer keeps pre-reset text ahead of the new banner.
def last_boot(lines):
    idx = [i for i, l in enumerate(lines) if "*** Booting Zephyr" in l]
    return lines[idx[-1]:] if idx else lines
rec, col = last_boot(rec), last_boot(col)

ok_re = re.compile(r"DATA_REQ/DATA OK: cycle=(\d+) slot=(\d+) peer=(\S+) seq=(\d+) "
                   r"sample=(\d+) bytes=(\d+) attempt=(\d+) offset=(\d+)")
fail_att = re.compile(r"DATA_REQ attempt failed: seq=(\d+) attempt=(\d+) offset=(\d+) error=(-?\d+) radio=(0x[0-9A-Fa-f]+)")
fail_txn = re.compile(r"DATA_REQ/DATA failed: seq=(\d+) error=(-?\d+)")
ctr = re.compile(r"Protocol counters: txns=(\d+)/(\d+) attempts=(\d+) timeouts=(\d+) rx_errors=(\d+) "
                 r"exhausted=(\d+) late=(\d+) invalid=(\d+)/(\d+)/(\d+)/(\d+)/(\d+)/(\d+) duplicate_payload=(\d+)")

oks = [tuple(map(int, m.groups()[:2])) + (m.group(3),) + tuple(map(int, m.groups()[3:]))
       for l in rec if (m := ok_re.search(l))]
print("RECEPTEC OK lines:", len(oks))
print("peers/slots seen:", collections.Counter((o[1], o[2]) for o in oks))
cycles = [o[0] for o in oks]
gaps = [(a, b) for a, b in zip(cycles, cycles[1:]) if b != a + 1]
print("first/last cycle:", cycles[0], cycles[-1], "| non-contiguous cycle pairs:", gaps)
seqs = [o[3] for o in oks]
bad_seq = [(c, s) for c, s in zip(cycles, seqs) if s != c % 256]
print("seq != cycle mod 256:", bad_seq[:5], "count", len(bad_seq))
att = collections.Counter(o[6] for o in oks)
print("completed at attempt:", dict(att))
print("bytes values:", collections.Counter(o[5] for o in oks), "| sample values:", collections.Counter(o[4] for o in oks))
print("retried transactions:", [(o[0], o[3], o[6], o[7]) for o in oks if o[6] != 1])
for l in rec:
    if fail_att.search(l) or fail_txn.search(l) or "qualification reset" in l or "missing peer" in l:
        print("  FAIL-LINE:", l[:170])
progress = [ctr.search(l).groups() for l in rec if ctr.search(l)]
print("counter snapshots:", len(progress))
for p in progress[-1:]:
    names = "txns_done txns_started attempts timeouts rx_err exhausted late inv_len inv_fcf inv_type inv_addr inv_seq inv_payload dup_mismatch".split()
    print("final counters:", dict(zip(names, map(int, p))))
offs = sorted(o[7] for o in oks)
print("attempt-1 offset always 0:", all(o[7] == 0 for o in oks if o[6] == 1))

print("\n--- COLETEC ---")
timing = re.compile(r"COLETEC timing: responses=(\d+) IRQ-to-arm=(\d+) us max-IRQ-to-arm=(\d+) us late=(\d+)")
t = [tuple(map(int, timing.search(l).groups())) for l in col if timing.search(l)]
print("timing lines:", len(t), "| last:", t[-1] if t else None, "| any late>0:", any(x[3] for x in t))
last_t = max(i for i, l in enumerate(col) if timing.search(l))
missed_before = sum("slot missed" in l for l in col[:last_t])
missed_after = sum("slot missed" in l for l in col[last_t:])
print("slot-missed lines before last timing line:", missed_before, "| after (RECEPTEC stopped):", missed_after)
print("sync lost before last timing line:", sum("synchronization lost" in l for l in col[:last_t]))
print("hw-filter-rejections values:", sorted(set(re.findall(r"hw-filter-rejections=(\d+)", "\n".join(col)))))
dup = [l for l in col if "duplicate=1" in l]
print("COLETEC duplicate=1 lines:", len(dup))
for l in dup[:3]: print("  ", l[:170])
