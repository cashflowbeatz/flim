"""Bounded FLP/FST event decoding, including escaped 32-bit event IDs."""
import struct


def data_bounds(d):
    if len(d) < 22 or d[:4] != b'FLhd':
        raise ValueError('missing FLhd header')
    header_len = struct.unpack_from('<I', d, 4)[0]
    if header_len < 6 or header_len > len(d) - 16:
        raise ValueError('invalid FLhd length')
    chunk = 8 + header_len
    if d[chunk:chunk + 4] != b'FLdt':
        raise ValueError('missing FLdt chunk')
    size = struct.unpack_from('<I', d, chunk + 4)[0]
    start = chunk + 8
    if size > len(d) - start:
        raise ValueError('truncated FLdt chunk')
    return start, start + size


def event_spans(d):
    """Yield (ID, event start, payload start, event end) within FLdt."""
    p, end = data_bounds(d)
    while p < end:
        start = p
        e = d[p]
        p += 1
        # FL's reader consumes 0xac + uint32 ID before dispatching by type.
        if e == 0xac:
            if end - p < 4:
                raise ValueError('truncated extended event ID')
            e = struct.unpack_from('<I', d, p)[0]
            p += 4
        if e <= 0xff:
            kind = e >> 6
        else:
            kind = e >> 28
            if kind not in (0, 4, 8, 12):
                raise ValueError(f'unknown extended event type: {e:#x}')
            kind >>= 2
        if kind < 3:
            size = 1 << kind
        else:
            size = shift = 0
            while True:
                if p == end or shift > 28:
                    raise ValueError('truncated or overflowing event length')
                b = d[p]
                p += 1
                if shift == 28 and b & 0xf0:
                    raise ValueError('overflowing event length')
                size |= (b & 0x7f) << shift
                if not b & 0x80:
                    break
                shift += 7
        if size > end - p:
            raise ValueError(f'truncated event {e:#x}')
        yield e, start, p, p + size
        p += size


def events(d):
    return [(e, d[p:end]) for e, _, p, end in event_spans(d)]


def rewrite_payloads(d, replacements):
    """Replace variable payloads by event ordinal, preserving all other bytes."""
    start, end = data_bounds(d)
    pieces = []
    cursor = start
    pending = dict(replacements)
    for ordinal, (event, at, payload, stop) in enumerate(event_spans(d)):
        if ordinal not in pending:
            continue
        value = pending.pop(ordinal)
        if event <= 0xff:
            variable = event >= 192
        else:
            variable = event >> 28 == 12
        if not variable:
            raise ValueError('cannot resize a fixed-size event')
        if not isinstance(value, bytes) or len(value) > 0xffffffff:
            raise ValueError('invalid replacement payload')
        encoded = bytearray()
        size = len(value)
        while size >= 128:
            encoded.append((size & 127) | 128)
            size >>= 7
        encoded.append(size)
        id_end = at + (5 if d[at] == 0xac else 1)
        pieces.extend((d[cursor:id_end], bytes(encoded), value))
        cursor = stop
    if pending:
        raise ValueError('replacement event does not exist')
    pieces.append(d[cursor:end])
    stream = b''.join(pieces)
    if len(stream) > 0xffffffff:
        raise ValueError('rewritten FLdt is too large')
    return d[:start - 4] + struct.pack('<I', len(stream)) + stream + d[end:]
