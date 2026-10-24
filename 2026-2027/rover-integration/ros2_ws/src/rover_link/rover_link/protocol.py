"""Line-based JSON protocol shared by both radio gateways.

One packet = one JSON object on one line, terminated by '\n', e.g.
    {"t":"motor","v":1}   PC -> rover : motor command (1 = on, 0 = off)
    {"t":"ack","v":1,"ok":1}   rover -> PC : current motor state + link health
"""
import json

MAX_LINE = 256  # bytes; anything longer without a newline is treated as garbage


def encode(**fields) -> bytes:
    """Build one newline-terminated JSON packet."""
    return (json.dumps(fields, separators=(',', ':')) + '\n').encode('utf-8')


class LineDecoder:
    """Collects raw serial bytes and returns complete, valid JSON objects.

    Radios can deliver partial lines or corrupted bytes. Incomplete data is
    kept until its newline arrives; lines that are not valid JSON are dropped.
    """

    def __init__(self):
        self._buf = bytearray()

    def feed(self, data: bytes):
        self._buf.extend(data)
        messages = []
        while True:
            end = self._buf.find(b'\n')
            if end < 0:
                break
            line = bytes(self._buf[:end]).strip()
            del self._buf[:end + 1]
            if not line:
                continue
            try:
                obj = json.loads(line.decode('utf-8'))
            except (UnicodeDecodeError, json.JSONDecodeError):
                continue
            if isinstance(obj, dict):
                messages.append(obj)
        if len(self._buf) > MAX_LINE:  # no newline in sight: junk, start over
            self._buf.clear()
        return messages


def motor_value(msg: dict):
    """Return True/False for a valid motor packet, otherwise None."""
    if msg.get('t') == 'motor' and msg.get('v') in (0, 1):
        return bool(msg['v'])
    return None
