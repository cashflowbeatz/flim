import struct, sys
def events(d):
    j = d.find(b'FLdt'); p = j + 8; out = []
    while p < len(d):
        e = d[p]; p += 1
        if e < 64: v = d[p:p+1]; p += 1
        elif e < 128: v = d[p:p+2]; p += 2
        elif e < 192: v = d[p:p+4]; p += 4
        else:
            n = 0; sh = 0
            while True:
                b = d[p]; p += 1; n |= (b & 0x7f) << sh; sh += 7
                if b < 128: break
            v = d[p:p+n]; p += n
        out.append((e, v))
    return out
