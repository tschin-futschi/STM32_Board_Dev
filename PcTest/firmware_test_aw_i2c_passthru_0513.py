"""
AW Firmware I2C 透传读写测试 (0x30 / 0x31)

覆盖 protocol.MD v2.5 新增的 AW Firmware I2C 读写指令：
    T7.1  0x30 参数校验 — DevId > 0x7F                 → 错误码 0x03
    T7.2  0x30 参数校验 — LEN 与 AddrSize+DataLen 不符 → 错误码 0x03
    T7.3  0x31 参数校验 — ReadLen = 0                   → 错误码 0x03
    T7.4  0x30 NACK     — 不存在 DevId                 → 错误码 0x03 (需要 I2C 总线空闲)
    T7.5  0x30 AddrSize=0 + 0x31 AddrSize=2 — 烧录回调 (AW SDK 主用法)
    T7.6  0x30/0x31 AddrSize=2 — 寄存器写 + 读回往返
    T7.7  0x30 大数据量 — DataLen=120 透传 (压载链路)
    T7.8  0x31 AddrSize=0 直读 — 命令式 I2C 路径 (信息性,不强校验值)

需要电机在线的用例通过 --motor 控制；0x00 = 跳过。

用法:
    pip install pyserial
    python firmware_test_aw_i2c_passthru_0513.py --port COM5 --motor 0x40 --reg 0x0010

参数:
    --port   串口端口         (默认 COM3)
    --baud   波特率           (默认 460800,v2.0 起默认值)
    --motor  电机 IC 7-bit 地址,0x00 = 跳过电机相关用例
    --reg    透传读写测试寄存器地址 (默认 0x0010)
"""

import sys
import time
import struct
import argparse
import serial


CFG = {
    'port':       'COM3',
    'baud':       460800,
    'motor_addr': 0x00,
    'test_reg':   0x0010,
}

_seq = 0
_results = []


# ---------------------------------------------------------------------------
# 协议工具
# ---------------------------------------------------------------------------

def next_seq() -> int:
    global _seq
    _seq = (_seq + 1) & 0xFF
    return _seq


def crc16(data: bytes) -> int:
    """协议 CRC16: poly=0x8005, init=0xFFFF, 右移低位先行 (与 firmware 一致)"""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8005 if (crc & 1) else crc >> 1
    return crc


def build_frame(seq: int, cmd: int, data: bytes = b'') -> bytes:
    payload = bytes([seq, cmd, len(data)]) + data
    c = crc16(payload)
    return b'\xAA\x55' + payload + bytes([c >> 8, c & 0xFF])


def parse_frame(raw: bytes) -> dict | None:
    if len(raw) < 7 or raw[0] != 0xAA or raw[1] != 0x55:
        return None
    seq, cmd, ln = raw[2], raw[3], raw[4]
    if len(raw) < 7 + ln:
        return None
    data     = raw[5:5 + ln]
    crc_recv = (raw[5 + ln] << 8) | raw[6 + ln]
    crc_calc = crc16(raw[2:5 + ln])
    return {'seq': seq, 'cmd': cmd, 'data': data, 'crc_ok': crc_recv == crc_calc}


def flush_rx(ser: serial.Serial):
    ser.reset_input_buffer()
    ser.timeout = 0.05
    while ser.read(256):
        pass


def _skip_stream_frame(ser: serial.Serial):
    """已读到 0xBB 首字节,跳过剩余流帧 (mask + len + data + xor)"""
    hdr = ser.read(2)
    if len(hdr) >= 2:
        ser.read(hdr[1] + 1)


def recv_control_frame(ser: serial.Serial, timeout: float = 1.5) -> dict | None:
    """接收一个 0xAA 0x55 控制帧;遇到 0xBB 流帧自动跳过"""
    ser.timeout = timeout
    deadline = time.time() + timeout
    while time.time() < deadline:
        b = ser.read(1)
        if not b:
            return None
        if b[0] == 0xBB:
            _skip_stream_frame(ser)
            continue
        if b[0] != 0xAA:
            continue
        b2 = ser.read(1)
        if not b2 or b2[0] != 0x55:
            continue
        hdr = ser.read(3)            # seq, cmd, len
        if len(hdr) < 3:
            return None
        ln  = hdr[2]
        rest = ser.read(ln + 2)      # data + CRC
        if len(rest) < ln + 2:
            return None
        return parse_frame(b'\xAA\x55' + hdr + rest)
    return None


# ---------------------------------------------------------------------------
# 0x30 / 0x31 帧构造助手
# ---------------------------------------------------------------------------

def build_pass_write(dev_id: int, addr_bytes: bytes, data: bytes) -> bytes:
    """0x30 载荷: [DevId][AddrSize][AddrBytes...][DataLen][Data...]"""
    payload = bytes([dev_id, len(addr_bytes)]) + addr_bytes \
            + bytes([len(data)]) + data
    return build_frame(next_seq(), 0x30, payload)


def build_pass_read(dev_id: int, addr_bytes: bytes, read_len: int) -> bytes:
    """0x31 载荷: [DevId][AddrSize][AddrBytes...][ReadLen]"""
    payload = bytes([dev_id, len(addr_bytes)]) + addr_bytes + bytes([read_len])
    return build_frame(next_seq(), 0x31, payload)


def expect_err(resp, code: int):
    assert resp is not None,                  '无响应'
    assert resp['crc_ok'],                    '响应 CRC 错误'
    assert resp['cmd'] == 0x01,               f'CMD 期望 0x01 错误响应, 实际 {resp["cmd"]:#04x}'
    assert len(resp['data']) == 1,            f'错误码长度期望 1, 实际 {len(resp["data"])}'
    assert resp['data'][0] == code,           f'错误码期望 {code:#04x}, 实际 {resp["data"][0]:#04x}'


def expect_ok(resp, cmd: int, expect_data_len: int | None = None):
    assert resp is not None,                  '无响应'
    assert resp['crc_ok'],                    '响应 CRC 错误'
    if resp['cmd'] == 0x01:
        raise AssertionError(f'命令执行失败,错误码 {resp["data"].hex()}')
    assert resp['cmd'] == cmd,                f'CMD 期望 {cmd:#04x}, 实际 {resp["cmd"]:#04x}'
    if expect_data_len is not None:
        assert len(resp['data']) == expect_data_len, \
            f'数据长度期望 {expect_data_len}, 实际 {len(resp["data"])}'


# ---------------------------------------------------------------------------
# 测试运行器
# ---------------------------------------------------------------------------

def run_test(name: str, fn, *args):
    print(f'[TEST] {name}')
    try:
        fn(*args)
        print('  ✓ PASS\n')
        _results.append((name, True, ''))
    except AssertionError as e:
        print(f'  ✗ FAIL: {e}\n')
        _results.append((name, False, str(e)))
    except Exception as e:
        print(f'  ✗ ERROR: {type(e).__name__}: {e}\n')
        _results.append((name, False, f'{type(e).__name__}: {e}'))


def skip_test(name: str, reason: str):
    print(f'[SKIP] {name} — {reason}\n')
    _results.append((name, None, reason))


# ---------------------------------------------------------------------------
# T7 — AW Firmware I2C 透传读写
# ---------------------------------------------------------------------------

def test_t71_pass_write_bad_devid(ser: serial.Serial):
    """0x30 DevId > 0x7F 应回错误码 0x03"""
    ser.write(build_pass_write(0x80, b'', b'\x00'))
    expect_err(recv_control_frame(ser), 0x03)


def test_t72_pass_write_len_mismatch(ser: serial.Serial):
    """0x30 LEN 与 AddrSize+DataLen 不一致应回错误码 0x03
       构造: AddrSize=2, AddrBytes=2B, 但 DataLen=5 而实际 Data 只给 2 字节 → LEN 不匹配"""
    bogus = bytes([0x40, 0x02, 0x00, 0x10, 0x05, 0xAA, 0xBB])   # 期望 LEN=8 实际 LEN=7
    ser.write(build_frame(next_seq(), 0x30, bogus))
    expect_err(recv_control_frame(ser), 0x03)


def test_t73_pass_read_zero_len(ser: serial.Serial):
    """0x31 ReadLen=0 应回错误码 0x03"""
    ser.write(build_pass_read(0x40, b'\x00\x10', read_len=0))
    expect_err(recv_control_frame(ser), 0x03)


def test_t74_pass_write_nack(ser: serial.Serial):
    """0x30 写到必不存在的 DevId (0x7F) 应被从机 NACK,固件返回 0x03"""
    ser.write(build_pass_write(0x7F, b'', b'\xDE\xAD'))
    expect_err(recv_control_frame(ser), 0x03)


def test_t75_aw_sdk_emulation(ser: serial.Serial, dev_id: int, reg: int):
    """AW SDK 主用法仿真:
       0x30 AddrSize=0,WrData=[regH][regL][valH][valL] (DLL 自拼地址)
       然后 0x31 AddrSize=2 标准读回验证."""
    write_val = 0xA55A
    wr_payload = struct.pack('>HH', reg, write_val)        # [regH][regL][valH][valL]

    # 写 (AW SDK 风格,AddrSize=0,Data 段全包含)
    ser.write(build_pass_write(dev_id, b'', wr_payload))
    expect_ok(recv_control_frame(ser, timeout=2.0), 0x30, expect_data_len=0)
    print(f'  写入 DevId={dev_id:#04x} reg={reg:#06x} ← {write_val:#06x} (AddrSize=0)')

    # 读回 (AddrSize=2 标准 RepeatedStart)
    ser.write(build_pass_read(dev_id, struct.pack('>H', reg), read_len=2))
    resp = recv_control_frame(ser, timeout=2.0)
    expect_ok(resp, 0x31, expect_data_len=2)
    read_val = struct.unpack('>H', resp['data'])[0]
    assert read_val == write_val, f'读回 {read_val:#06x} ≠ 写入 {write_val:#06x}'
    print(f'  读回 {read_val:#06x} ✓')


def test_t76_pass_write_addrsize2_roundtrip(ser: serial.Serial, dev_id: int, reg: int):
    """0x30 AddrSize=2 (协议层显式拆分地址) + 0x31 AddrSize=2 读回"""
    write_val = 0x1234
    addr_bytes = struct.pack('>H', reg)
    data_bytes = struct.pack('>H', write_val)

    ser.write(build_pass_write(dev_id, addr_bytes, data_bytes))
    expect_ok(recv_control_frame(ser, timeout=2.0), 0x30, expect_data_len=0)
    print(f'  写入 reg={reg:#06x} ← {write_val:#06x} (AddrSize=2)')

    ser.write(build_pass_read(dev_id, addr_bytes, read_len=2))
    resp = recv_control_frame(ser, timeout=2.0)
    expect_ok(resp, 0x31, expect_data_len=2)
    read_val = struct.unpack('>H', resp['data'])[0]
    assert read_val == write_val, f'读回 {read_val:#06x} ≠ 写入 {write_val:#06x}'
    print(f'  读回 {read_val:#06x} ✓')


def test_t77_pass_write_large_payload(ser: serial.Serial, dev_id: int):
    """0x30 透传大块数据 (DataLen=120),验证链路与定时不超限
       注: 这是写到一个伪寄存器地址,不验证从机内容,只验证透传成功."""
    addr_bytes = struct.pack('>H', 0x0100)            # 假寄存器基址
    data = bytes((i & 0xFF) for i in range(120))      # 120 字节流水值
    ser.write(build_pass_write(dev_id, addr_bytes, data))
    resp = recv_control_frame(ser, timeout=3.0)
    # 从机可能 ACK 也可能 NACK,只要不超时即说明协议层完整透传到 I2C;
    # 真实从机通常会 ACK 全段流,故按 SUCCESS 校验;失败时打印不强制断言
    assert resp is not None,             '无响应或超时'
    assert resp['crc_ok'],               '响应 CRC 错误'
    if resp['cmd'] == 0x01:
        print(f'  (从机 NACK 大段写,错误码 {resp["data"].hex()},协议层透传 OK)')
    else:
        assert resp['cmd'] == 0x30,      f'CMD 期望 0x30, 实际 {resp["cmd"]:#04x}'
        print(f'  120B 透传成功')


def test_t78_pass_read_addrsize0(ser: serial.Serial, dev_id: int):
    """0x31 AddrSize=0 直读路径 (命令式 I2C / BootLoader 场景)
       AW SDK 中此分支为 OPEN 项;此处仅验证协议链路能完整往返,不强校验数据值"""
    ser.write(build_pass_read(dev_id, b'', read_len=2))
    resp = recv_control_frame(ser, timeout=2.0)
    assert resp is not None,             '无响应或超时'
    assert resp['crc_ok'],               '响应 CRC 错误'
    if resp['cmd'] == 0x01:
        print(f'  (从机不支持裸读,错误码 {resp["data"].hex()},协议链路 OK)')
    else:
        assert resp['cmd'] == 0x31,      f'CMD 期望 0x31, 实际 {resp["cmd"]:#04x}'
        assert len(resp['data']) == 2,   f'读回长度期望 2, 实际 {len(resp["data"])}'
        print(f'  裸读 2B = {resp["data"].hex(" ").upper()}')


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description='AW Firmware I2C 透传读写测试 (0x30/0x31)')
    parser.add_argument('--port',  default='COM3',   help='串口端口 (默认 COM3)')
    parser.add_argument('--baud',  type=int, default=460800, help='波特率 (默认 460800)')
    parser.add_argument('--motor', default='0x00',
                        help='电机 IC 7-bit 地址,0x00 = 跳过电机相关用例')
    parser.add_argument('--reg',   default='0x0010', help='透传读写测试寄存器地址 (默认 0x0010)')
    args = parser.parse_args()

    CFG['port']       = args.port
    CFG['baud']       = args.baud
    CFG['motor_addr'] = int(args.motor, 0)
    CFG['test_reg']   = int(args.reg, 0)

    has_motor = (CFG['motor_addr'] != 0x00)

    sep = '=' * 60
    print(sep)
    print('  AW Firmware I2C 透传读写测试 (0x30 / 0x31)')
    print(f'  端口     : {CFG["port"]} @ {CFG["baud"]} bps')
    print(f'  电机地址 : {CFG["motor_addr"]:#04x}'
          + ('' if has_motor else '  (需要从机的用例将被跳过)'))
    print(f'  测试寄存器: {CFG["test_reg"]:#06x}')
    print(sep + '\n')

    try:
        ser = serial.Serial(CFG['port'], CFG['baud'], timeout=1.5)
    except serial.SerialException as e:
        print(f'错误:无法打开串口 {CFG["port"]}: {e}')
        sys.exit(1)

    time.sleep(0.1)
    flush_rx(ser)

    # 心跳确认链路可用
    ser.write(build_frame(next_seq(), 0x00))
    hb = recv_control_frame(ser, timeout=1.0)
    if hb is None or hb['cmd'] != 0x00:
        print('错误:心跳无响应,请检查串口/波特率/STM32 状态')
        ser.close()
        sys.exit(1)

    motor = CFG['motor_addr']
    reg   = CFG['test_reg']

    # ── T7 参数校验 (无需从机) ────────────────────────────────────────────
    print('── T7  AW Firmware I2C 透传读写 ' + '─' * 27)
    run_test('T7.1  0x30 非法 DevId',                   test_t71_pass_write_bad_devid, ser)
    run_test('T7.2  0x30 LEN 不匹配',                   test_t72_pass_write_len_mismatch, ser)
    run_test('T7.3  0x31 ReadLen=0',                    test_t73_pass_read_zero_len, ser)
    run_test('T7.4  0x30 NACK (DevId=0x7F)',            test_t74_pass_write_nack, ser)

    # ── 需要从机在线的用例 ───────────────────────────────────────────────
    if has_motor:
        run_test('T7.5  AW SDK 仿真 (AddrSize=0 写 + AddrSize=2 读回)',
                 test_t75_aw_sdk_emulation, ser, motor, reg)
        run_test('T7.6  AddrSize=2 写 + 读回',          test_t76_pass_write_addrsize2_roundtrip, ser, motor, reg)
        run_test('T7.7  0x30 大块透传 (120B)',           test_t77_pass_write_large_payload, ser, motor)
        run_test('T7.8  0x31 AddrSize=0 裸读',           test_t78_pass_read_addrsize0, ser, motor)
    else:
        for name in ('T7.5  AW SDK 仿真',
                     'T7.6  AddrSize=2 写 + 读回',
                     'T7.7  0x30 大块透传 (120B)',
                     'T7.8  0x31 AddrSize=0 裸读'):
            skip_test(name, '未指定 --motor')

    # 汇总
    ser.close()
    print('=' * 60)
    passed = sum(1 for _, ok, _ in _results if ok is True)
    failed = sum(1 for _, ok, _ in _results if ok is False)
    skipped = sum(1 for _, ok, _ in _results if ok is None)
    print(f'  PASS: {passed}    FAIL: {failed}    SKIP: {skipped}')
    print('=' * 60)
    sys.exit(0 if failed == 0 else 1)


if __name__ == '__main__':
    main()
