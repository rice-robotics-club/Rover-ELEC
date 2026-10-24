from rover_link.protocol import LineDecoder, encode, motor_value


def test_roundtrip():
    assert LineDecoder().feed(encode(t='motor', v=1)) == [{'t': 'motor', 'v': 1}]


def test_partial_line_waits_for_newline():
    d = LineDecoder()
    assert d.feed(b'{"t":"motor","v"') == []
    assert d.feed(b':0}\n') == [{'t': 'motor', 'v': 0}]


def test_garbage_is_dropped_and_stream_recovers():
    d = LineDecoder()
    assert d.feed(bytes([0xff, 0xfe]) + b'{{\n{"t":"motor","v":1}\n') == [
        {'t': 'motor', 'v': 1}]


def test_runaway_junk_without_newline_is_discarded():
    d = LineDecoder()
    d.feed(b'x' * 500)
    assert d.feed(b'{"t":"motor","v":1}\n') == [{'t': 'motor', 'v': 1}]


def test_motor_value_validation():
    assert motor_value({'t': 'motor', 'v': 1}) is True
    assert motor_value({'t': 'motor', 'v': 0}) is False
    assert motor_value({'t': 'motor', 'v': 2}) is None
    assert motor_value({'t': 'ack', 'v': 1}) is None
