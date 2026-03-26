"""
STM32 串口协议测试脚本
协议规范：protocol.MD v1.3

用法：
    pip install pyserial
    python test.py          # 使用默认串口 COM3
    python test.py COM5     # 指定串口
"""

import sys
import serial


# ---------------------------------------------------------------------------
# 协议工具函数
# ---------------------------------------------------------------------------

def crc16_modbus(data: bytes) -> int:
    """CRC16-MODBUS: poly=0x8005, init=0xFFFF, LSB-first, 结果为整数。"""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0x8005
            else:
                crc >>= 1
    return crc


def build_frame(seq: int, cmd: int, data: bytes = b'') -> bytes:
    """构造控制帧：[0xAA][0x55][SEQ][CMD][LEN][DATA][CRC_H][CRC_L]"""
    payload = bytes([seq, cmd, len(data)]) + data
    crc = crc16_modbus(payload)
    return b'\xAA\x55' + payload + bytes([crc >> 8, crc & 0xFF])


def parse_frame(raw: bytes) -> dict | None:
    """
    解析控制帧，返回字段字典，CRC 不匹配时 crc_ok=False。
    帧头或长度不合法时返回 None。
    """
    if len(raw) < 7 or raw[0] != 0xAA or raw[1] != 0x55:
        return None
    seq    = raw[2]
    cmd    = raw[3]
    length = raw[4]
    if len(raw) < 7 + length:
        return None
    data      = raw[5:5 + length]
    crc_recv  = (raw[5 + length] << 8) | raw[6 + length]
    crc_calc  = crc16_modbus(raw[2:5 + length])
    return {'seq': seq, 'cmd': cmd, 'data': data, 'crc_ok': crc_recv == crc_calc}


# ---------------------------------------------------------------------------
# 测试用例
# ---------------------------------------------------------------------------

def test_heartbeat(ser: serial.Serial):
    """发送正确心跳帧，期望原样回传。"""
    frame = build_frame(seq=0x01, cmd=0x00)
    print(f'  发送: {frame.hex(" ").upper()}')
    ser.write(frame)
    resp = ser.read(64)
    print(f'  收到: {resp.hex(" ").upper()}')
    parsed = parse_frame(resp)
    assert parsed is not None,              "响应帧格式非法"
    assert parsed['crc_ok'],                "响应 CRC 错误"
    assert parsed['cmd'] == 0x00,           f"CMD 期望 0x00，实际 {parsed['cmd']:#04x}"
    assert parsed['seq'] == 0x01,           f"SEQ 期望 0x01，实际 {parsed['seq']:#04x}"
    assert len(parsed['data']) == 0,        "心跳响应数据段应为空"
    print("  ✓ 通过\n")


def test_crc_error(ser: serial.Serial):
    """发送 CRC 填错的帧，期望收到错误响应（CMD=0x01，SEQ=0xFF，错误码=0x01）。"""
    bad_frame = b'\xAA\x55\x02\x00\x00\xFF\xFF'  # CRC 故意填错
    print(f'  发送: {bad_frame.hex(" ").upper()}')
    ser.write(bad_frame)
    resp = ser.read(64)
    print(f'  收到: {resp.hex(" ").upper()}')
    parsed = parse_frame(resp)
    assert parsed is not None,              "响应帧格式非法"
    assert parsed['crc_ok'],               "响应帧自身 CRC 错误"
    assert parsed['cmd'] == 0x01,          f"CMD 期望 0x01，实际 {parsed['cmd']:#04x}"
    assert parsed['seq'] == 0xFF,          f"SEQ 期望 0xFF，实际 {parsed['seq']:#04x}"
    assert parsed['data'] == b'\x01',      f"错误码期望 0x01，实际 {parsed['data'].hex()}"
    print("  ✓ 通过\n")


def test_unknown_cmd(ser: serial.Serial):
    """发送未知命令码 0x10，期望收到错误响应（CMD=0x01，错误码=0x02）。"""
    frame = build_frame(seq=0x03, cmd=0x10)
    print(f'  发送: {frame.hex(" ").upper()}')
    ser.write(frame)
    resp = ser.read(64)
    print(f'  收到: {resp.hex(" ").upper()}')
    parsed = parse_frame(resp)
    assert parsed is not None,              "响应帧格式非法"
    assert parsed['crc_ok'],               "响应帧自身 CRC 错误"
    assert parsed['cmd'] == 0x01,          f"CMD 期望 0x01，实际 {parsed['cmd']:#04x}"
    assert parsed['seq'] == 0x03,          f"SEQ 期望 0x03，实际 {parsed['seq']:#04x}"
    assert parsed['data'] == b'\x02',      f"错误码期望 0x02，实际 {parsed['data'].hex()}"
    print("  ✓ 通过\n")


def test_consecutive_frames(ser: serial.Serial):
    """连续发送两帧，验证状态机在响应后正确复位。"""
    for seq in (0x10, 0x11):
        frame = build_frame(seq=seq, cmd=0x00)
        print(f'  发送: {frame.hex(" ").upper()}')
        ser.write(frame)
        resp = ser.read(64)
        print(f'  收到: {resp.hex(" ").upper()}')
        parsed = parse_frame(resp)
        assert parsed is not None,          "响应帧格式非法"
        assert parsed['crc_ok'],            "响应 CRC 错误"
        assert parsed['seq'] == seq,        f"SEQ 期望 {seq:#04x}，实际 {parsed['seq']:#04x}"
    print("  ✓ 通过\n")


# ---------------------------------------------------------------------------
# 主入口
# ---------------------------------------------------------------------------

TESTS = [
    ("心跳帧",             test_heartbeat),
    ("CRC 错误帧",         test_crc_error),
    ("未知命令码",         test_unknown_cmd),
    ("连续两帧状态机复位", test_consecutive_frames),
]

if __name__ == '__main__':
    port = sys.argv[1] if len(sys.argv) > 1 else 'COM3'
    print(f'连接串口 {port} @ 115200 bps\n')

    try:
        ser = serial.Serial(port, 115200, timeout=0.5)
    except serial.SerialException as e:
        print(f'错误：无法打开串口 {port}: {e}')
        sys.exit(1)

    passed = 0
    failed = 0
    for name, fn in TESTS:
        print(f'[测试] {name}')
        try:
            fn(ser)
            passed += 1
        except AssertionError as e:
            print(f'  ✗ 失败: {e}\n')
            failed += 1

    ser.close()
    print(f'结果：{passed} 通过 / {failed} 失败')
    sys.exit(0 if failed == 0 else 1)
