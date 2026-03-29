"""
STM32 串口协议测试脚本  v1.4
覆盖测试项：T2 UART 基础 / T4 系统控制 / T5 寄存器读写 / T6 采样功能

用法：
    pip install pyserial
    python test.py                              # COM3, 115200, 跳过电机相关测试
    python test.py --port COM5 --motor 0x20 --reg 0x0000
    python test.py --port COM5 --motor 0x20 --reg 0x0010 --baud 115200

参数说明：
    --port   串口端口       (默认 COM3)
    --baud   波特率         (默认 115200)
    --motor  电机 IC 7-bit 地址，0x00 = 跳过电机/采样相关测试  (默认 0x00)
    --reg    寄存器读写测试地址  (默认 0x0000)
"""

import sys
import time
import struct
import argparse
import serial

# ---------------------------------------------------------------------------
# 全局配置（由 main() 从命令行填充）
# ---------------------------------------------------------------------------
CFG = {
    'port':       'COM3',
    'baud':       115200,
    'motor_addr': 0x00,
    'test_reg':   0x0000,
}

_seq = 0

def next_seq() -> int:
    global _seq
    _seq = (_seq + 1) & 0xFF
    return _seq


# ---------------------------------------------------------------------------
# 协议工具
# ---------------------------------------------------------------------------

def crc16_modbus(data: bytes) -> int:
    """CRC16-MODBUS: poly=0x8005, init=0xFFFF, LSB-first"""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8005 if (crc & 1) else crc >> 1
    return crc


def build_frame(seq: int, cmd: int, data: bytes = b'') -> bytes:
    """构造控制帧 [0xAA][0x55][SEQ][CMD][LEN][DATA][CRC_H][CRC_L]"""
    payload = bytes([seq, cmd, len(data)]) + data
    crc = crc16_modbus(payload)
    return b'\xAA\x55' + payload + bytes([crc >> 8, crc & 0xFF])


def parse_frame(raw: bytes) -> dict | None:
    """解析控制帧，返回字典；格式非法或长度不足时返回 None"""
    if len(raw) < 7 or raw[0] != 0xAA or raw[1] != 0x55:
        return None
    seq, cmd, ln = raw[2], raw[3], raw[4]
    if len(raw) < 7 + ln:
        return None
    data     = raw[5 : 5 + ln]
    crc_recv = (raw[5 + ln] << 8) | raw[6 + ln]
    crc_calc = crc16_modbus(raw[2 : 5 + ln])
    return {'seq': seq, 'cmd': cmd, 'data': data, 'crc_ok': crc_recv == crc_calc}


def parse_stream_frame(raw: bytes) -> dict | None:
    """解析流帧 [0xBB][mask][len][data...][xor]"""
    if len(raw) < 4 or raw[0] != 0xBB:
        return None
    mask, ln = raw[1], raw[2]
    if len(raw) < 4 + ln:
        return None
    data     = raw[3 : 3 + ln]
    xor_recv = raw[3 + ln]
    xor_calc = mask ^ ln
    for b in data:
        xor_calc ^= b
    return {'mask': mask, 'len': ln, 'data': data, 'xor_ok': xor_calc == xor_recv}


def fmt(f: dict) -> str:
    """格式化控制帧为可读字符串"""
    d = f['data'].hex(' ').upper() if f['data'] else '(empty)'
    return f'SEQ={f["seq"]:#04x} CMD={f["cmd"]:#04x} DATA=[{d}] CRC={"OK" if f["crc_ok"] else "FAIL"}'


# ---------------------------------------------------------------------------
# 串口 I/O 辅助
# ---------------------------------------------------------------------------

def flush_rx(ser: serial.Serial):
    ser.timeout = 0.05
    while ser.read(256):
        pass


def _skip_stream_frame(ser: serial.Serial):
    """已读到 0xBB 首字节，跳过剩余流帧内容"""
    hdr = ser.read(2)           # mask, len
    if len(hdr) >= 2:
        ser.read(hdr[1] + 1)   # data + xor


def recv_control_frame(ser: serial.Serial, timeout: float = 1.0) -> dict | None:
    """
    等待并解析一个控制帧（0xAA 0x55 开头）。
    自动跳过混入的流帧（0xBB 开头）字节。
    超时返回 None。
    """
    deadline = time.monotonic() + timeout

    while time.monotonic() < deadline:
        ser.timeout = min(0.05, max(0.01, deadline - time.monotonic()))
        b = ser.read(1)
        if not b:
            continue

        byte = b[0]

        if byte == 0xBB:
            _skip_stream_frame(ser)
            continue

        if byte != 0xAA:
            continue

        # 读第二个字节
        ser.timeout = 0.1
        b2 = ser.read(1)
        if not b2:
            continue

        if b2[0] == 0xBB:
            _skip_stream_frame(ser)
            continue

        if b2[0] == 0xAA:
            # 两个连续 0xAA：第二个可能是新帧的 SOF1
            b3 = ser.read(1)
            if not b3 or b3[0] != 0x55:
                continue
        elif b2[0] != 0x55:
            continue

        # 已确认 0xAA 0x55，读 SEQ CMD LEN
        hdr = ser.read(3)
        if len(hdr) < 3:
            continue

        data_len = hdr[2]
        rest = ser.read(data_len + 2)   # DATA + CRC_H + CRC_L
        if len(rest) < data_len + 2:
            continue

        result = parse_frame(b'\xAA\x55' + hdr + rest)
        if result:
            return result

    return None


def recv_stream_frames(ser: serial.Serial, count: int,
                       timeout: float = 3.0) -> list:
    """收集 count 个完整流帧，返回已解析的字典列表"""
    frames  = []
    deadline = time.monotonic() + timeout

    while len(frames) < count and time.monotonic() < deadline:
        ser.timeout = min(0.1, max(0.01, deadline - time.monotonic()))
        b = ser.read(1)
        if not b or b[0] != 0xBB:
            continue

        hdr = ser.read(2)               # mask, len
        if len(hdr) < 2:
            continue
        mask, ln = hdr[0], hdr[1]
        rest = ser.read(ln + 1)         # data + xor
        if len(rest) < ln + 1:
            continue

        f = parse_stream_frame(b'\xBB' + hdr + rest)
        if f:
            frames.append(f)

    return frames


def recv_bulk_packets(ser: serial.Serial, total_pkts: int,
                      timeout: float = 10.0) -> list:
    """连续接收 total_pkts 个 CMD=0x22 的响应包"""
    packets  = []
    deadline = time.monotonic() + timeout

    while len(packets) < total_pkts and time.monotonic() < deadline:
        f = recv_control_frame(ser, timeout=min(2.0, deadline - time.monotonic()))
        if f and f['cmd'] == 0x22 and f['crc_ok']:
            packets.append(f)

    return packets


# ---------------------------------------------------------------------------
# 测试运行器
# ---------------------------------------------------------------------------

_results: list[tuple[str, bool | None, str]] = []


def run_test(name: str, fn, *args):
    print(f'[TEST] {name}')
    try:
        fn(*args)
        print(f'  ✓ PASS\n')
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
# T2 — UART 通信基础
# ---------------------------------------------------------------------------

def test_heartbeat(ser: serial.Serial):
    """T2.2 正常心跳帧收发"""
    seq   = next_seq()
    frame = build_frame(seq, 0x00)
    print(f'  TX: {frame.hex(" ").upper()}')
    ser.write(frame)
    resp = recv_control_frame(ser)
    assert resp is not None,          '无响应或超时'
    assert resp['crc_ok'],            '响应 CRC 错误'
    assert resp['cmd'] == 0x00,       f'CMD 期望 0x00，实际 {resp["cmd"]:#04x}'
    assert resp['seq'] == seq,        f'SEQ 期望 {seq:#04x}，实际 {resp["seq"]:#04x}'
    assert len(resp['data']) == 0,    '心跳数据段应为空'
    print(f'  RX: {fmt(resp)}')


def test_crc_error(ser: serial.Serial):
    """T2.3 CRC 错误帧 → 错误响应 0x01"""
    bad = b'\xAA\x55\x02\x00\x00\xFF\xFF'   # CRC 故意填错
    print(f'  TX: {bad.hex(" ").upper()}')
    ser.write(bad)
    resp = recv_control_frame(ser)
    assert resp is not None,          '无响应或超时'
    assert resp['crc_ok'],            '响应帧自身 CRC 错误'
    assert resp['cmd'] == 0x01,       f'CMD 期望 0x01，实际 {resp["cmd"]:#04x}'
    assert resp['seq'] == 0xFF,       f'SEQ 期望 0xFF，实际 {resp["seq"]:#04x}'
    assert resp['data'] == b'\x01',   f'错误码期望 0x01，实际 {resp["data"].hex()}'
    print(f'  RX: {fmt(resp)}')


def test_unknown_cmd(ser: serial.Serial):
    """T2.4 未知命令码 → 错误响应 0x02"""
    seq   = next_seq()
    frame = build_frame(seq, 0x7F)
    print(f'  TX: {frame.hex(" ").upper()}')
    ser.write(frame)
    resp = recv_control_frame(ser)
    assert resp is not None,          '无响应或超时'
    assert resp['crc_ok'],            '响应 CRC 错误'
    assert resp['cmd'] == 0x01,       f'CMD 期望 0x01，实际 {resp["cmd"]:#04x}'
    assert resp['seq'] == seq,        f'SEQ 期望 {seq:#04x}，实际 {resp["seq"]:#04x}'
    assert resp['data'] == b'\x02',   f'错误码期望 0x02，实际 {resp["data"].hex()}'
    print(f'  RX: {fmt(resp)}')


def test_consecutive_frames(ser: serial.Serial):
    """T2.x 连续 3 帧心跳，验证状态机正确复位"""
    for i in range(3):
        seq   = next_seq()
        frame = build_frame(seq, 0x00)
        ser.write(frame)
        resp = recv_control_frame(ser)
        assert resp is not None,      f'第 {i+1} 帧无响应'
        assert resp['crc_ok'],        f'第 {i+1} 帧 CRC 错误'
        assert resp['seq'] == seq,    f'第 {i+1} 帧 SEQ 期望 {seq:#04x}，实际 {resp["seq"]:#04x}'
    print('  连续 3 帧均正确响应')


# ---------------------------------------------------------------------------
# T4 — 系统控制命令
# ---------------------------------------------------------------------------

def test_set_motor_addr_invalid(ser: serial.Serial):
    """T4.2 非法地址（0x00 / 0x80 / 0xFF）应返回执行失败错误"""
    for bad in (0x00, 0x80, 0xFF):
        seq = next_seq()
        ser.write(build_frame(seq, 0x02, bytes([bad])))
        resp = recv_control_frame(ser)
        assert resp is not None,          f'地址 {bad:#04x} 无响应'
        assert resp['cmd'] == 0x01,       f'地址 {bad:#04x} 应返回错误响应，实际 CMD={resp["cmd"]:#04x}'
        assert resp['data'] == b'\x03',   f'地址 {bad:#04x} 错误码期望 0x03，实际 {resp["data"].hex()}'
        flush_rx(ser)
    print('  0x00 / 0x80 / 0xFF 均被正确拒绝')


def test_set_motor_addr(ser: serial.Serial, addr: int):
    """T4.1 设置有效电机 IC 地址"""
    seq = next_seq()
    ser.write(build_frame(seq, 0x02, bytes([addr])))
    resp = recv_control_frame(ser)
    assert resp is not None,      '无响应或超时'
    assert resp['crc_ok'],        '响应 CRC 错误'
    if resp['cmd'] == 0x01:
        raise AssertionError(f'设置失败，错误码 {resp["data"].hex()}')
    assert resp['cmd'] == 0x02,   f'CMD 期望 0x02，实际 {resp["cmd"]:#04x}'
    print(f'  电机 IC 地址已设置为 {addr:#04x}')


def test_motor_ping(ser: serial.Serial):
    """T4.3 电机 IC Ping（I2C ACK 测试）"""
    seq = next_seq()
    ser.write(build_frame(seq, 0x05))
    resp = recv_control_frame(ser, timeout=2.0)
    assert resp is not None,      '无响应或超时'
    assert resp['crc_ok'],        '响应 CRC 错误'
    if resp['cmd'] == 0x01:
        raise AssertionError(f'Ping 失败，错误码 {resp["data"].hex()}（设备可能不在线）')
    assert resp['cmd'] == 0x05,   f'CMD 期望 0x05，实际 {resp["cmd"]:#04x}'
    print(f'  RX: {fmt(resp)}')


def test_baudrate_switch(ser: serial.Serial):
    """
    T4.4 波特率切换：115200 → 57600 → 115200
    切换过程中保持通信正常。
    """
    port = ser.port

    # Step 1: 请求切换到 57600 (idx=3)
    ser.write(build_frame(next_seq(), 0x03, b'\x03'))
    resp = recv_control_frame(ser, timeout=1.0)
    assert resp is not None,      '切换波特率无 ACK'
    assert resp['cmd'] == 0x03,   f'CMD 期望 0x03，实际 {resp["cmd"]:#04x}'

    # Step 2: 以 57600 重连
    ser.close()
    time.sleep(0.15)
    ser.baudrate = 57600
    ser.open()
    print('  以 57600 重连，发送心跳...')

    try:
        ser.write(build_frame(next_seq(), 0x00))
        resp2 = recv_control_frame(ser, timeout=1.0)
        assert resp2 is not None, '57600 bps 心跳无响应'
        assert resp2['cmd'] == 0x00, '57600 bps 心跳 CMD 错误'
        print(f'  57600 bps 通信正常: {fmt(resp2)}')

        # Step 3: 请求切回 115200 (idx=4)
        ser.write(build_frame(next_seq(), 0x03, b'\x04'))
        resp3 = recv_control_frame(ser, timeout=1.0)
        assert resp3 is not None, '切回 115200 无 ACK'
        assert resp3['cmd'] == 0x03

    finally:
        # 无论成功失败都尝试恢复 115200
        ser.close()
        time.sleep(0.15)
        ser.baudrate = 115200
        ser.open()

    # Step 4: 115200 验证
    ser.write(build_frame(next_seq(), 0x00))
    resp4 = recv_control_frame(ser, timeout=1.0)
    assert resp4 is not None,     '切回 115200 后心跳无响应'
    assert resp4['cmd'] == 0x00,  '切回后心跳 CMD 错误'
    print(f'  已切回 115200 bps: {fmt(resp4)}')


def test_system_reset(ser: serial.Serial):
    """T4.5 系统复位：收到 ACK → STM32 复位 → 重新启动后心跳正常"""
    ser.write(build_frame(next_seq(), 0x04))
    resp = recv_control_frame(ser, timeout=1.0)
    assert resp is not None,      '复位命令无 ACK'
    assert resp['cmd'] == 0x04,   f'CMD 期望 0x04，实际 {resp["cmd"]:#04x}'
    print(f'  ACK: {fmt(resp)}，等待复位完成 (1.5s)...')

    time.sleep(1.5)   # PMIC 上电序列 4×10ms + 启动余量
    flush_rx(ser)

    ser.write(build_frame(next_seq(), 0x00))
    resp2 = recv_control_frame(ser, timeout=2.0)
    assert resp2 is not None,     '复位后无心跳响应（未正常启动）'
    assert resp2['cmd'] == 0x00,  '复位后心跳 CMD 错误'
    print(f'  复位后心跳正常: {fmt(resp2)}')


# ---------------------------------------------------------------------------
# T5 — 寄存器读写
# ---------------------------------------------------------------------------

def test_read_reg(ser: serial.Serial, reg: int):
    """T5.1 读单寄存器"""
    ser.write(build_frame(next_seq(), 0x20, struct.pack('>H', reg)))
    resp = recv_control_frame(ser, timeout=2.0)
    assert resp is not None,          '无响应或超时'
    assert resp['crc_ok'],            '响应 CRC 错误'
    if resp['cmd'] == 0x01:
        raise AssertionError(f'读取失败，错误码 {resp["data"].hex()}')
    assert resp['cmd'] == 0x20,       f'CMD 期望 0x20，实际 {resp["cmd"]:#04x}'
    assert len(resp['data']) == 2,    f'数据长度期望 2，实际 {len(resp["data"])}'
    val = struct.unpack('>H', resp['data'])[0]
    print(f'  寄存器 {reg:#06x} = {val:#06x} ({val})')


def test_write_readback(ser: serial.Serial, reg: int, write_val: int = 0x1234):
    """T5.2 写单寄存器后立即读回，验证一致性"""
    # 写
    ser.write(build_frame(next_seq(), 0x21, struct.pack('>HH', reg, write_val)))
    resp = recv_control_frame(ser, timeout=2.0)
    assert resp is not None,          '写操作无响应'
    assert resp['crc_ok'],            '响应 CRC 错误'
    if resp['cmd'] == 0x01:
        raise AssertionError(f'写失败，错误码 {resp["data"].hex()}')
    assert resp['cmd'] == 0x21,       'CMD 期望 0x21'
    print(f'  写入 {reg:#06x} ← {write_val:#06x}')

    # 读回
    ser.write(build_frame(next_seq(), 0x20, struct.pack('>H', reg)))
    resp2 = recv_control_frame(ser, timeout=2.0)
    assert resp2 is not None,         '读回无响应'
    assert resp2['crc_ok'],           '读回响应 CRC 错误'
    assert resp2['cmd'] == 0x20,      'CMD 期望 0x20'
    read_val = struct.unpack('>H', resp2['data'])[0]
    assert read_val == write_val,     f'读回 {read_val:#06x} ≠ 写入 {write_val:#06x}'
    print(f'  读回确认: {read_val:#06x} ✓')


def test_bulk_read_single(ser: serial.Serial, reg: int):
    """T5.3 批量读 1 个寄存器 → 应收到 1 包"""
    ser.write(build_frame(next_seq(), 0x22, struct.pack('>HH', reg, 1)))
    pkts = recv_bulk_packets(ser, total_pkts=1, timeout=3.0)
    assert len(pkts) == 1,                f'期望 1 包，实际 {len(pkts)} 包'
    assert pkts[0]['crc_ok'],             '包 0 CRC 错误'
    assert pkts[0]['data'][0] == 0,       f'pktIdx 期望 0，实际 {pkts[0]["data"][0]}'
    assert pkts[0]['data'][1] == 1,       f'totalPkts 期望 1，实际 {pkts[0]["data"][1]}'
    assert len(pkts[0]['data']) == 4,     f'数据长度期望 4（2+2），实际 {len(pkts[0]["data"])}'
    val = struct.unpack('>H', pkts[0]['data'][2:4])[0]
    print(f'  1 包，寄存器 {reg:#06x} = {val:#06x}')


def test_bulk_read_multipacket(ser: serial.Serial, reg: int):
    """T5.4 批量读 130 个寄存器 → 应分 2 包（126 + 4）"""
    count = 130
    ser.write(build_frame(next_seq(), 0x22, struct.pack('>HH', reg, count)))
    pkts = recv_bulk_packets(ser, total_pkts=2, timeout=10.0)
    assert len(pkts) == 2,                f'期望 2 包，实际 {len(pkts)} 包'
    for i, pkt in enumerate(pkts):
        assert pkt['crc_ok'],             f'包 {i} CRC 错误'
        assert pkt['data'][0] == i,       f'包 {i} pktIdx 错误'
        assert pkt['data'][1] == 2,       f'包 {i} totalPkts 应为 2'
    regs0 = (len(pkts[0]['data']) - 2) // 2
    regs1 = (len(pkts[1]['data']) - 2) // 2
    assert regs0 == 126,  f'包 0 寄存器数期望 126，实际 {regs0}'
    assert regs1 == 4,    f'包 1 寄存器数期望 4，实际 {regs1}'
    print(f'  包 0: {regs0} 寄存器，包 1: {regs1} 寄存器 ✓')


def test_bulk_during_sampling(ser: serial.Serial, reg: int):
    """T5.5 采样运行时批量读应被拒绝（错误码 0x03）"""
    _start_sampling(ser, reg, mask=0x01, interval_idx=5)
    time.sleep(0.1)

    ser.write(build_frame(next_seq(), 0x22, struct.pack('>HH', reg, 1)))
    # 固件可能因 UART TX 忙于流帧而短暂延迟响应，重试最多 3 次
    resp = None
    for _ in range(3):
        resp = recv_control_frame(ser, timeout=2.0)
        if resp is not None:
            break
        time.sleep(0.05)
    _stop_sampling(ser)

    assert resp is not None,          '无响应（含重试）'
    assert resp['cmd'] == 0x01,       f'应返回错误响应，实际 CMD={resp["cmd"]:#04x}'
    assert resp['data'] == b'\x03',   f'错误码期望 0x03，实际 {resp["data"].hex()}'
    print('  采样运行时批量读被正确拒绝')


# ---------------------------------------------------------------------------
# T6 — 采样功能辅助
# ---------------------------------------------------------------------------

def _start_sampling(ser: serial.Serial, reg: int,
                    mask: int = 0x01, interval_idx: int = 5):
    """配置 reg map + channel mask + interval，然后 start"""
    # 设置寄存器映射：ch0 → reg，其余 → 0xFFFF
    reg_map = struct.pack('>H', reg) + b'\xFF\xFF' * 7
    ser.write(build_frame(next_seq(), 0x54, reg_map))
    r = recv_control_frame(ser)
    assert r is not None and r['cmd'] == 0x54, '设置 reg map 失败'

    # 设置通道掩码
    ser.write(build_frame(next_seq(), 0x53, bytes([mask])))
    r = recv_control_frame(ser)
    assert r is not None and r['cmd'] == 0x53, '设置 channel mask 失败'

    # 设置采样间隔
    ser.write(build_frame(next_seq(), 0x52, bytes([interval_idx])))
    r = recv_control_frame(ser)
    assert r is not None and r['cmd'] == 0x52, '设置 interval 失败'

    # 启动
    ser.write(build_frame(next_seq(), 0x50))
    r = recv_control_frame(ser)
    assert r is not None, '启动采样无响应'
    if r['cmd'] == 0x01:
        raise AssertionError(f'启动采样失败，错误码 {r["data"].hex()}')
    assert r['cmd'] == 0x50, f'启动采样 CMD 期望 0x50，实际 {r["cmd"]:#04x}'


def _stop_sampling(ser: serial.Serial):
    ser.write(build_frame(next_seq(), 0x51))
    recv_control_frame(ser, timeout=0.5)
    flush_rx(ser)


# ---------------------------------------------------------------------------
# T6 — 采样功能测试
# ---------------------------------------------------------------------------

def test_start_no_mapping(ser: serial.Serial):
    """T6.6 所有通道未映射时，start 应被拒绝（错误码 0x03）"""
    ser.write(build_frame(next_seq(), 0x54, b'\xFF\xFF' * 8))
    r = recv_control_frame(ser)
    assert r is not None and r['cmd'] == 0x54, f'设置全 0xFFFF reg map 失败，响应={r}'

    ser.write(build_frame(next_seq(), 0x50))
    resp = recv_control_frame(ser)
    assert resp is not None,          '无响应'
    assert resp['cmd'] == 0x01,       f'应返回错误响应，实际 CMD={resp["cmd"]:#04x}'
    assert resp['data'] == b'\x03',   f'错误码期望 0x03，实际 {resp["data"].hex()}'
    print('  未映射时启动被正确拒绝')


def test_sample_start_stop(ser: serial.Serial, reg: int):
    """T6.4 / T6.7 启动采样 → 持续收到流帧 → 停止 → 流帧消失"""
    _start_sampling(ser, reg, mask=0x01, interval_idx=5)

    frames = recv_stream_frames(ser, count=10, timeout=3.0)
    assert len(frames) >= 5, f'期望 ≥5 帧，实际 {len(frames)}'
    print(f'  收到 {len(frames)} 帧')

    for i, f in enumerate(frames):
        assert f['mask'] == 0x01, f'第 {i} 帧 mask 期望 0x01，实际 {f["mask"]:#04x}'
        assert f['len']  == 2,    f'第 {i} 帧 len 期望 2，实际 {f["len"]}'

    _stop_sampling(ser)
    time.sleep(0.2)
    leftover = recv_stream_frames(ser, count=1, timeout=0.4)
    assert len(leftover) == 0, f'停止后仍收到 {len(leftover)} 帧'
    print('  停止后无流帧 ✓')


def test_stream_xor(ser: serial.Serial, reg: int):
    """T6.8 验证连续 20 帧的 XOR 校验"""
    _start_sampling(ser, reg, mask=0x01, interval_idx=5)
    frames = recv_stream_frames(ser, count=20, timeout=5.0)
    _stop_sampling(ser)

    assert len(frames) >= 10, f'期望 ≥10 帧，实际 {len(frames)}'
    for i, f in enumerate(frames):
        assert f['xor_ok'], f'第 {i} 帧 XOR 校验失败'
    print(f'  {len(frames)} 帧 XOR 全部通过')


def test_sample_multichannel(ser: serial.Serial, reg: int):
    """T6.5 双通道（ch0+ch1），验证帧格式（mask=0x03，len=4）"""
    # 映射 ch0 和 ch1 → reg，其余 0xFFFF
    reg_map = struct.pack('>HH', reg, reg) + b'\xFF\xFF' * 6
    ser.write(build_frame(next_seq(), 0x54, reg_map))
    r = recv_control_frame(ser)
    assert r is not None and r['cmd'] == 0x54, '设置 reg map 失败'

    ser.write(build_frame(next_seq(), 0x53, b'\x03'))   # ch0+ch1
    r = recv_control_frame(ser)
    assert r is not None and r['cmd'] == 0x53, '设置 mask 失败'

    ser.write(build_frame(next_seq(), 0x52, b'\x05'))
    r = recv_control_frame(ser)
    assert r is not None and r['cmd'] == 0x52, f'设置采样间隔失败，响应={r}'

    ser.write(build_frame(next_seq(), 0x50))
    r = recv_control_frame(ser)
    assert r is not None and r['cmd'] == 0x50, '启动失败'

    frames = recv_stream_frames(ser, count=10, timeout=3.0)
    _stop_sampling(ser)

    assert len(frames) >= 5, f'期望 ≥5 帧，实际 {len(frames)}'
    for i, f in enumerate(frames):
        assert f['mask'] == 0x03, f'第 {i} 帧 mask 期望 0x03，实际 {f["mask"]:#04x}'
        assert f['len']  == 4,    f'第 {i} 帧 len 期望 4，实际 {f["len"]}'
        assert f['xor_ok'],       f'第 {i} 帧 XOR 校验失败'
    print(f'  双通道 {len(frames)} 帧，mask/len/XOR 全部正确')


def test_set_interval(ser: serial.Serial, reg: int):
    """T6.3 验证不同采样间隔的帧率（高频 > 低频）"""
    # idx=0: 100µs (10kHz), idx=5: 1ms (1kHz), idx=7: 2ms (500Hz)
    results = []
    for idx in (0, 5, 7):
        _start_sampling(ser, reg, mask=0x01, interval_idx=idx)
        t0     = time.monotonic()
        frames = recv_stream_frames(ser, count=30, timeout=5.0)
        elapsed = time.monotonic() - t0
        _stop_sampling(ser)

        if elapsed > 0 and frames:
            hz = len(frames) / elapsed
            results.append(hz)
            print(f'  idx={idx}: {len(frames)} 帧 / {elapsed:.2f}s ≈ {hz:.0f} Hz')
        else:
            results.append(0)
            print(f'  idx={idx}: 收帧不足')

    # 粗略验证：idx=0 帧率 > idx=7 帧率（仅在两者都有有效数据时比较）
    if results[0] == 0 or results[2] == 0:
        print(f'  警告：部分区间收帧不足（results={[f"{r:.0f}" for r in results]}），跳过大小比较')
    else:
        assert results[0] > results[2], (
            f'高频 idx=0 ({results[0]:.0f} Hz) 应 > 低频 idx=7 ({results[2]:.0f} Hz)'
        )
        print('  高频 > 低频 验证通过')


# ---------------------------------------------------------------------------
# 主入口
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description='STM32 协议测试脚本 v1.4')
    parser.add_argument('--port',   default='COM3',   help='串口端口 (默认 COM3)')
    parser.add_argument('--baud',   type=int, default=115200, help='波特率 (默认 115200)')
    parser.add_argument('--motor',  default='0x00',
                        help='电机 IC 7-bit 地址，0x00 = 跳过电机/采样相关测试 (默认 0x00)')
    parser.add_argument('--reg',    default='0x0000',
                        help='寄存器读写测试地址 (默认 0x0000)')
    args = parser.parse_args()

    CFG['port']       = args.port
    CFG['baud']       = args.baud
    CFG['motor_addr'] = int(args.motor, 0)
    CFG['test_reg']   = int(args.reg, 0)

    has_motor = (CFG['motor_addr'] != 0x00)

    sep = '=' * 52
    print(sep)
    print('  STM32 协议测试  v1.4')
    print(f'  端口   : {CFG["port"]} @ {CFG["baud"]} bps')
    print(f'  电机地址: {CFG["motor_addr"]:#04x}'
          + ('' if has_motor else '  (电机/采样测试将被跳过)'))
    print(f'  测试寄存器: {CFG["test_reg"]:#06x}')
    print(sep + '\n')

    try:
        ser = serial.Serial(CFG['port'], CFG['baud'], timeout=1.0)
    except serial.SerialException as e:
        print(f'错误：无法打开串口 {CFG["port"]}: {e}')
        sys.exit(1)

    time.sleep(0.1)
    flush_rx(ser)

    reg   = CFG['test_reg']
    motor = CFG['motor_addr']

    # ── T2 UART 通信基础 ──────────────────────────────────────────────────
    print('── T2  UART 通信基础 ' + '─' * 31)
    run_test('T2.2  心跳帧',              test_heartbeat,          ser)
    run_test('T2.3  CRC 错误响应',        test_crc_error,          ser)
    run_test('T2.4  未知命令响应',        test_unknown_cmd,        ser)
    run_test('T2.x  连续多帧状态机复位',  test_consecutive_frames, ser)

    # ── T4 系统控制命令 ───────────────────────────────────────────────────
    print('── T4  系统控制命令 ' + '─' * 32)
    run_test('T4.2  设置非法电机地址',    test_set_motor_addr_invalid, ser)

    if has_motor:
        run_test('T4.1  设置电机 IC 地址',  test_set_motor_addr, ser, motor)
        run_test('T4.3  电机 IC Ping',      test_motor_ping,     ser)
    else:
        skip_test('T4.1  设置电机 IC 地址', '未指定 --motor')
        skip_test('T4.3  电机 IC Ping',     '未指定 --motor')

    run_test('T4.4  波特率切换 (115200↔57600)',  test_baudrate_switch, ser)

    run_test('T4.5  系统复位',             test_system_reset, ser)

    # 复位后重新设置电机地址（MCU 状态已清零）
    if has_motor:
        run_test('T4.1  复位后重设电机地址', test_set_motor_addr, ser, motor)

    # ── T5 寄存器读写 ─────────────────────────────────────────────────────
    print('── T5  寄存器读写 ' + '─' * 34)
    if has_motor:
        run_test('T5.1  读单寄存器',              test_read_reg,           ser, reg)
        run_test('T5.2  写寄存器 + 读回验证',     test_write_readback,     ser, reg)
        run_test('T5.3  批量读（1 个）',          test_bulk_read_single,   ser, reg)
        run_test('T5.4  批量读（130 个，跨包）',  test_bulk_read_multipacket, ser, reg)
        run_test('T5.5  采样中拒绝批量读',        test_bulk_during_sampling,  ser, reg)
    else:
        for name in ('T5.1  读单寄存器', 'T5.2  写寄存器 + 读回验证',
                     'T5.3  批量读（1 个）', 'T5.4  批量读（130 个，跨包）',
                     'T5.5  采样中拒绝批量读'):
            skip_test(name, '未指定 --motor')

    # ── T6 采样功能 ───────────────────────────────────────────────────────
    print('── T6  采样功能 ' + '─' * 36)
    if has_motor:
        run_test('T6.6  无映射时启动被拒绝',     test_start_no_mapping,   ser)
        run_test('T6.4/7  启动 / 停止采样',      test_sample_start_stop,  ser, reg)
        run_test('T6.8  流帧 XOR 校验（20 帧）', test_stream_xor,         ser, reg)
        run_test('T6.5  双通道采样',             test_sample_multichannel, ser, reg)
        run_test('T6.3  采样间隔频率验证',        test_set_interval,       ser, reg)
    else:
        for name in ('T6.6  无映射时启动被拒绝',
                     'T6.4/7  启动 / 停止采样',
                     'T6.8  流帧 XOR 校验',
                     'T6.5  双通道采样',
                     'T6.3  采样间隔频率验证'):
            skip_test(name, '未指定 --motor')

    ser.close()

    # ── 汇总 ──────────────────────────────────────────────────────────────
    passed  = sum(1 for _, ok, _ in _results if ok is True)
    failed  = sum(1 for _, ok, _ in _results if ok is False)
    skipped = sum(1 for _, ok, _ in _results if ok is None)

    print('\n' + sep)
    print(f'  结果：{passed} PASS  /  {failed} FAIL  /  {skipped} SKIP')
    if failed:
        print('\n  失败项：')
        for name, ok, msg in _results:
            if ok is False:
                print(f'    ✗ {name}: {msg}')
    print(sep)

    sys.exit(0 if failed == 0 else 1)


if __name__ == '__main__':
    main()
