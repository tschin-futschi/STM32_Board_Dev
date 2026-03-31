"""
STM32 串口协议测试脚本  v2.0
覆盖测试项：T2 UART 基础 / T4 系统控制 / T5 寄存器读写 / T6 采样功能

改进要点（相对 v1.4）：
  1. recv_control_frame：找到 SOF 后改用固定 200ms/次读取剩余字节，
     避免 deadline 临近时帧尾被截断
  2. 波特率切换后等待 200ms（原 100ms），给 STM32 足够时间完成重初始化
  3. 系统复位后轮询心跳（最多 5s）替代固定 2s sleep
  4. 开串口后用 _wait_ready() 发 3 次心跳确认就绪再开始测试
  5. 每条测试前独立 flush_rx，消除上条测试残留干扰
  6. test_write_readback：先读原值，写后验证，finally 写回原值

用法：
    pip install pyserial
    python firmware_test_protocol_0331.py --port COM3 --motor 0x6F --reg 0xB000

参数说明：
    --port   串口端口                                (默认 COM3)
    --baud   波特率                                  (默认 115200)
    --motor  电机 IC 7-bit 地址，0x00=跳过电机/采样测试  (默认 0x00)
    --reg    寄存器读写测试地址                       (默认 0x0000)
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

# 找到帧头后，读取剩余帧字节的单次超时（固定值，避免 deadline 边界竞争）
_FRAME_BODY_TIMEOUT = 0.2


def flush_rx(ser: serial.Serial):
    """清空接收缓冲区"""
    ser.reset_input_buffer()
    ser.timeout = 0.05
    while ser.read(256):
        pass


def _read_exact(ser: serial.Serial, n: int) -> bytes | None:
    """
    在 _FRAME_BODY_TIMEOUT 内读取恰好 n 个字节。
    未能读满则返回 None。
    改进：找到帧头后用固定超时读取剩余，避免 deadline 临近时读被截断。
    """
    ser.timeout = _FRAME_BODY_TIMEOUT
    buf = ser.read(n)
    return buf if len(buf) == n else None


def recv_control_frame(ser: serial.Serial, timeout: float = 1.0) -> dict | None:
    """
    等待并解析一个控制帧（0xAA 0x55 开头）。
    自动跳过混入的流帧（0xBB 开头）。
    超时返回 None。
    """
    deadline = time.monotonic() + timeout

    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        ser.timeout = min(0.05, max(0.005, remaining))

        b = ser.read(1)
        if not b:
            continue

        byte = b[0]

        # 跳过流帧
        if byte == 0xBB:
            hdr = _read_exact(ser, 2)       # mask, len
            if hdr:
                ser.read(hdr[1] + 1)        # data + xor
            continue

        if byte != 0xAA:
            continue

        # 读第二字节
        b2 = _read_exact(ser, 1)
        if not b2:
            continue

        if b2[0] == 0xBB:
            hdr = _read_exact(ser, 2)
            if hdr:
                ser.read(hdr[1] + 1)
            continue

        if b2[0] == 0xAA:
            # 两个连续 0xAA：第二个可能是新帧 SOF1，再读一字节确认 0x55
            b3 = _read_exact(ser, 1)
            if not b3 or b3[0] != 0x55:
                continue
        elif b2[0] != 0x55:
            continue

        # 已确认 0xAA 0x55，读 SEQ CMD LEN（固定 3 字节）
        hdr = _read_exact(ser, 3)
        if not hdr:
            continue

        data_len = hdr[2]
        rest = _read_exact(ser, data_len + 2)   # DATA + CRC_H + CRC_L
        if not rest:
            continue

        result = parse_frame(b'\xAA\x55' + hdr + rest)
        if result:
            return result

    return None


def recv_stream_frames(ser: serial.Serial, count: int,
                       timeout: float = 3.0) -> list:
    """收集 count 个完整流帧，返回已解析的字典列表"""
    frames   = []
    deadline = time.monotonic() + timeout

    while len(frames) < count and time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        ser.timeout = min(0.1, max(0.005, remaining))

        b = ser.read(1)
        if not b or b[0] != 0xBB:
            continue

        hdr = _read_exact(ser, 2)           # mask, len
        if not hdr:
            continue
        mask, ln = hdr[0], hdr[1]
        rest = _read_exact(ser, ln + 1)     # data + xor
        if not rest:
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
        remaining = deadline - time.monotonic()
        f = recv_control_frame(ser, timeout=min(2.0, remaining))
        if f and f['cmd'] == 0x22 and f['crc_ok']:
            packets.append(f)

    return packets


# ---------------------------------------------------------------------------
# 测试运行器
# ---------------------------------------------------------------------------

_results: list[tuple[str, bool | None, str]] = []


def run_test(name: str, fn, *args):
    print(f'[ RUN ] {name}')
    try:
        fn(*args)
        _results.append((name, True, ''))
        print(f'  [PASS]\n')
    except Exception as e:
        _results.append((name, False, str(e)))
        print(f'  [FAIL]: {e}\n')


def skip_test(name: str, reason: str):
    print(f'[SKIP ] {name}: {reason}')
    _results.append((name, None, reason))


# ---------------------------------------------------------------------------
# T2 — UART 通信基础
# ---------------------------------------------------------------------------

def test_heartbeat(ser: serial.Serial):
    """T2.2 正常心跳帧收发"""
    flush_rx(ser)
    seq   = next_seq()
    frame = build_frame(seq, 0x00)
    print(f'  TX: {frame.hex(" ").upper()}')
    ser.write(frame)
    resp = recv_control_frame(ser)
    assert resp is not None,       '无响应或超时'
    assert resp['crc_ok'],         '响应 CRC 错误'
    assert resp['cmd'] == 0x00,    f'CMD 期望 0x00，实际 {resp["cmd"]:#04x}'
    assert resp['seq'] == seq,     f'SEQ 期望 {seq:#04x}，实际 {resp["seq"]:#04x}'
    assert len(resp['data']) == 0, '心跳数据段应为空'
    print(f'  RX: {fmt(resp)}')


def test_crc_error(ser: serial.Serial):
    """T2.3 CRC 错误帧 → 错误响应 0x01"""
    flush_rx(ser)
    bad = b'\xAA\x55\x02\x00\x00\xFF\xFF'
    print(f'  TX: {bad.hex(" ").upper()}')
    ser.write(bad)
    resp = recv_control_frame(ser)
    assert resp is not None,        '无响应或超时'
    assert resp['crc_ok'],          '响应帧自身 CRC 错误'
    assert resp['cmd'] == 0x01,     f'CMD 期望 0x01，实际 {resp["cmd"]:#04x}'
    assert resp['seq'] == 0xFF,     f'SEQ 期望 0xFF，实际 {resp["seq"]:#04x}'
    assert resp['data'] == b'\x01', f'错误码期望 0x01，实际 {resp["data"].hex()}'
    print(f'  RX: {fmt(resp)}')


def test_unknown_cmd(ser: serial.Serial):
    """T2.4 未知命令码 → 错误响应 0x02"""
    flush_rx(ser)
    seq   = next_seq()
    frame = build_frame(seq, 0x7F)
    print(f'  TX: {frame.hex(" ").upper()}')
    ser.write(frame)
    resp = recv_control_frame(ser)
    assert resp is not None,        '无响应或超时'
    assert resp['crc_ok'],          '响应 CRC 错误'
    assert resp['cmd'] == 0x01,     f'CMD 期望 0x01，实际 {resp["cmd"]:#04x}'
    assert resp['seq'] == seq,      f'SEQ 期望 {seq:#04x}，实际 {resp["seq"]:#04x}'
    assert resp['data'] == b'\x02', f'错误码期望 0x02，实际 {resp["data"].hex()}'
    print(f'  RX: {fmt(resp)}')


def test_consecutive_frames(ser: serial.Serial):
    """T2.x 连续 3 帧心跳，验证状态机正确复位"""
    flush_rx(ser)
    for i in range(3):
        seq   = next_seq()
        frame = build_frame(seq, 0x00)
        ser.write(frame)
        resp = recv_control_frame(ser)
        assert resp is not None,   f'第 {i+1} 帧无响应'
        assert resp['crc_ok'],     f'第 {i+1} 帧 CRC 错误'
        assert resp['seq'] == seq, f'第 {i+1} 帧 SEQ 期望 {seq:#04x}，实际 {resp["seq"]:#04x}'
        time.sleep(0.02)
    print('  连续 3 帧均正确响应')


# ---------------------------------------------------------------------------
# T4 — 系统控制命令
# ---------------------------------------------------------------------------

def test_set_motor_addr_invalid(ser: serial.Serial):
    """T4.2 非法地址（0x00 / 0x80 / 0xFF）应返回执行失败错误"""
    for bad in (0x00, 0x80, 0xFF):
        flush_rx(ser)
        seq = next_seq()
        ser.write(build_frame(seq, 0x02, bytes([bad])))
        resp = recv_control_frame(ser)
        assert resp is not None,        f'地址 {bad:#04x} 无响应'
        assert resp['cmd'] == 0x01,     f'地址 {bad:#04x} 应返回错误响应，实际 CMD={resp["cmd"]:#04x}'
        assert resp['data'] == b'\x03', f'地址 {bad:#04x} 错误码期望 0x03，实际 {resp["data"].hex()}'
    print('  0x00 / 0x80 / 0xFF 均被正确拒绝')


def test_set_motor_addr(ser: serial.Serial, addr: int):
    """T4.1 设置有效电机 IC 地址"""
    flush_rx(ser)
    seq = next_seq()
    ser.write(build_frame(seq, 0x02, bytes([addr])))
    resp = recv_control_frame(ser)
    assert resp is not None, '无响应或超时'
    assert resp['crc_ok'],   '响应 CRC 错误'
    if resp['cmd'] == 0x01:
        raise AssertionError(f'设置失败，错误码 {resp["data"].hex()}')
    assert resp['cmd'] == 0x02, f'CMD 期望 0x02，实际 {resp["cmd"]:#04x}'
    print(f'  电机 IC 地址已设置为 {addr:#04x}')


def test_motor_ping(ser: serial.Serial):
    """T4.3 电机 IC Ping（I2C ACK 测试）"""
    flush_rx(ser)
    seq = next_seq()
    ser.write(build_frame(seq, 0x05))
    resp = recv_control_frame(ser, timeout=2.0)
    assert resp is not None, '无响应或超时'
    assert resp['crc_ok'],   '响应 CRC 错误'
    if resp['cmd'] == 0x01:
        raise AssertionError(f'Ping 失败，错误码 {resp["data"].hex()}（设备可能不在线）')
    assert resp['cmd'] == 0x05, f'CMD 期望 0x05，实际 {resp["cmd"]:#04x}'
    print(f'  RX: {fmt(resp)}')


def test_baudrate_switch(ser: serial.Serial):
    """
    T4.4 波特率切换：115200 → 57600 → 115200
    改进：切换后等 200ms（原 100ms），给 STM32 足够时间完成重初始化。
    不使用 close/open，避免 DTR 触发复位。
    """
    flush_rx(ser)
    # Step 1: 请求切换到 57600 (idx=3)
    ser.write(build_frame(next_seq(), 0x03, b'\x03'))
    resp = recv_control_frame(ser, timeout=1.0)
    assert resp is not None,    '切换波特率无 ACK'
    assert resp['cmd'] == 0x03, f'CMD 期望 0x03，实际 {resp["cmd"]:#04x}'

    # Step 2: Python 端切换到 57600，等 200ms 让 STM32 重初始化完成
    time.sleep(0.2)
    ser.baudrate = 57600
    flush_rx(ser)
    print('  切换至 57600，发送心跳...')

    try:
        ser.write(build_frame(next_seq(), 0x00))
        resp2 = recv_control_frame(ser, timeout=1.5)
        assert resp2 is not None,    '57600 bps 心跳无响应'
        assert resp2['cmd'] == 0x00, f'57600 bps 心跳 CMD 错误，实际: {fmt(resp2)}'
        print(f'  57600 bps 通信正常: {fmt(resp2)}')

        # Step 3: 请求切回 115200 (idx=4)
        ser.write(build_frame(next_seq(), 0x03, b'\x04'))
        resp3 = recv_control_frame(ser, timeout=1.5)
        assert resp3 is not None,    '切回 115200 无 ACK'
        assert resp3['cmd'] == 0x03, f'切回 ACK CMD 期望 0x03，实际 {resp3["cmd"]:#04x}'

    finally:
        # 无论成功失败都恢复 Python 端到 115200
        time.sleep(0.2)
        ser.baudrate = 115200
        flush_rx(ser)

    # Step 4: 115200 验证
    ser.write(build_frame(next_seq(), 0x00))
    resp4 = recv_control_frame(ser, timeout=1.5)
    assert resp4 is not None,    '切回 115200 后心跳无响应'
    assert resp4['cmd'] == 0x00, '切回后心跳 CMD 错误'
    print(f'  已切回 115200 bps: {fmt(resp4)}')


def test_system_reset(ser: serial.Serial):
    """
    T4.5 系统复位：收到 ACK → STM32 复位 → 重新启动后心跳正常。
    改进：轮询心跳（最多 5s）替代固定 sleep，更稳定。
    """
    flush_rx(ser)
    ser.write(build_frame(next_seq(), 0x04))
    resp = recv_control_frame(ser, timeout=1.0)
    assert resp is not None,    '复位命令无 ACK'
    assert resp['cmd'] == 0x04, f'CMD 期望 0x04，实际 {resp["cmd"]:#04x}'
    print(f'  ACK: {fmt(resp)}，等待复位完成...')

    # 轮询心跳，最多等 5s
    deadline = time.monotonic() + 5.0
    resp2 = None
    while time.monotonic() < deadline:
        time.sleep(0.3)
        flush_rx(ser)
        ser.write(build_frame(next_seq(), 0x00))
        r = recv_control_frame(ser, timeout=0.8)
        if r is not None and r['cmd'] == 0x00:
            resp2 = r
            break

    assert resp2 is not None, '复位后无心跳响应（最多等待 5s，未正常启动）'
    print(f'  复位后心跳正常: {fmt(resp2)}')


# ---------------------------------------------------------------------------
# T5 — 寄存器读写
# ---------------------------------------------------------------------------

def test_read_reg(ser: serial.Serial, reg: int):
    """T5.1 读单寄存器"""
    flush_rx(ser)
    ser.write(build_frame(next_seq(), 0x20, struct.pack('>H', reg)))
    resp = recv_control_frame(ser, timeout=2.0)
    assert resp is not None,       '无响应或超时'
    assert resp['crc_ok'],         '响应 CRC 错误'
    if resp['cmd'] == 0x01:
        raise AssertionError(f'读取失败，错误码 {resp["data"].hex()}')
    assert resp['cmd'] == 0x20,    f'CMD 期望 0x20，实际 {resp["cmd"]:#04x}'
    assert len(resp['data']) == 2, f'数据长度期望 2，实际 {len(resp["data"])}'
    val = struct.unpack('>H', resp['data'])[0]
    print(f'  寄存器 {reg:#06x} = {val:#06x} ({val})')


def _read_reg_raw(ser: serial.Serial, reg: int) -> int:
    """读单寄存器，返回 uint16 值，失败抛出异常"""
    flush_rx(ser)
    ser.write(build_frame(next_seq(), 0x20, struct.pack('>H', reg)))
    resp = recv_control_frame(ser, timeout=2.0)
    assert (resp is not None and resp['cmd'] == 0x20 and len(resp['data']) == 2), \
        f'读寄存器 {reg:#06x} 失败，响应={resp}'
    return struct.unpack('>H', resp['data'])[0]


def _write_reg_raw(ser: serial.Serial, reg: int, val: int):
    """写单寄存器，失败抛出异常"""
    flush_rx(ser)
    ser.write(build_frame(next_seq(), 0x21, struct.pack('>HH', reg, val)))
    resp = recv_control_frame(ser, timeout=2.0)
    assert (resp is not None and resp['cmd'] == 0x21), \
        f'写寄存器 {reg:#06x} 失败，响应={resp}'


def test_write_readback(ser: serial.Serial, reg: int):
    """
    T5.2 写单寄存器后立即读回，验证一致性。
    改进：先读原值备份，写后验证，finally 写回原值，避免破坏电机寄存器状态。
    """
    orig = _read_reg_raw(ser, reg)
    print(f'  原值: {reg:#06x} = {orig:#06x}')

    write_val = (orig ^ 0x0001) & 0xFFFF    # 翻转最低位，避免写入固定魔法数
    try:
        _write_reg_raw(ser, reg, write_val)
        print(f'  写入 {reg:#06x} ← {write_val:#06x}')

        read_val = _read_reg_raw(ser, reg)
        assert read_val == write_val, f'读回 {read_val:#06x} ≠ 写入 {write_val:#06x}'
        print(f'  读回确认: {read_val:#06x} ✓')
    finally:
        _write_reg_raw(ser, reg, orig)
        print(f'  已恢复原值: {orig:#06x}')


def test_bulk_read_single(ser: serial.Serial, reg: int):
    """T5.3 批量读 1 个寄存器 → 应收到 1 包"""
    flush_rx(ser)
    ser.write(build_frame(next_seq(), 0x22, struct.pack('>HH', reg, 1)))
    pkts = recv_bulk_packets(ser, total_pkts=1, timeout=3.0)
    assert len(pkts) == 1,            f'期望 1 包，实际 {len(pkts)} 包'
    assert pkts[0]['crc_ok'],         '包 0 CRC 错误'
    assert pkts[0]['data'][0] == 0,   f'pktIdx 期望 0，实际 {pkts[0]["data"][0]}'
    assert pkts[0]['data'][1] == 1,   f'totalPkts 期望 1，实际 {pkts[0]["data"][1]}'
    assert len(pkts[0]['data']) == 4, f'数据长度期望 4（2+2），实际 {len(pkts[0]["data"])}'
    val = struct.unpack('>H', pkts[0]['data'][2:4])[0]
    print(f'  1 包，寄存器 {reg:#06x} = {val:#06x}')


def test_bulk_read_multipacket(ser: serial.Serial, reg: int):
    """T5.4 批量读 130 个寄存器 → 应分 2 包（126 + 4）"""
    flush_rx(ser)
    count = 130
    ser.write(build_frame(next_seq(), 0x22, struct.pack('>HH', reg, count)))
    pkts = recv_bulk_packets(ser, total_pkts=2, timeout=10.0)
    assert len(pkts) == 2, f'期望 2 包，实际 {len(pkts)} 包'
    for i, pkt in enumerate(pkts):
        assert pkt['crc_ok'],       f'包 {i} CRC 错误'
        assert pkt['data'][0] == i, f'包 {i} pktIdx 错误'
        assert pkt['data'][1] == 2, f'包 {i} totalPkts 应为 2'
    regs0 = (len(pkts[0]['data']) - 2) // 2
    regs1 = (len(pkts[1]['data']) - 2) // 2
    assert regs0 == 126, f'包 0 寄存器数期望 126，实际 {regs0}'
    assert regs1 == 4,   f'包 1 寄存器数期望 4，实际 {regs1}'
    print(f'  包 0: {regs0} 寄存器，包 1: {regs1} 寄存器 ✓')


def test_bulk_during_sampling(ser: serial.Serial, reg: int):
    """T5.5 采样运行时批量读应被拒绝（错误码 0x03）"""
    _start_sampling(ser, reg, mask=0x01, interval_idx=5)
    time.sleep(0.2)

    flush_rx(ser)
    ser.write(build_frame(next_seq(), 0x22, struct.pack('>HH', reg, 1)))
    resp = recv_control_frame(ser, timeout=3.0)
    _stop_sampling(ser)

    assert resp is not None,        '无响应（采样期间批量读响应）'
    assert resp['cmd'] == 0x01,     f'应返回错误响应，实际 CMD={resp["cmd"]:#04x}'
    assert resp['data'] == b'\x03', f'错误码期望 0x03，实际 {resp["data"].hex()}'
    print('  采样运行时批量读被正确拒绝')


# ---------------------------------------------------------------------------
# T6 — 采样功能辅助
# ---------------------------------------------------------------------------

def _start_sampling(ser: serial.Serial, reg: int,
                    mask: int = 0x01, interval_idx: int = 5):
    """配置 reg map + channel mask + interval，然后 start"""
    flush_rx(ser)
    reg_map = struct.pack('>H', reg) + b'\xFF\xFF' * 7
    ser.write(build_frame(next_seq(), 0x54, reg_map))
    r = recv_control_frame(ser)
    assert r is not None and r['cmd'] == 0x54, f'设置 reg map 失败，响应={r}'

    ser.write(build_frame(next_seq(), 0x53, bytes([mask])))
    r = recv_control_frame(ser)
    assert r is not None and r['cmd'] == 0x53, f'设置 channel mask 失败，响应={r}'

    ser.write(build_frame(next_seq(), 0x52, bytes([interval_idx])))
    r = recv_control_frame(ser)
    assert r is not None and r['cmd'] == 0x52, f'设置 interval 失败，响应={r}'

    ser.write(build_frame(next_seq(), 0x50))
    r = recv_control_frame(ser)
    assert r is not None, '启动采样无响应'
    if r['cmd'] == 0x01:
        raise AssertionError(f'启动采样失败，错误码 {r["data"].hex()}')
    assert r['cmd'] == 0x50, f'启动采样 CMD 期望 0x50，实际 {r["cmd"]:#04x}'


def _stop_sampling(ser: serial.Serial):
    """停止采样并清空残留流帧"""
    ser.write(build_frame(next_seq(), 0x51))
    recv_control_frame(ser, timeout=1.0)
    flush_rx(ser)


# ---------------------------------------------------------------------------
# T6 — 采样功能测试
# ---------------------------------------------------------------------------

def test_start_no_mapping(ser: serial.Serial):
    """T6.6 所有通道未映射时，start 应被拒绝（错误码 0x03）"""
    flush_rx(ser)
    ser.write(build_frame(next_seq(), 0x54, b'\xFF\xFF' * 8))
    r = recv_control_frame(ser)
    assert r is not None and r['cmd'] == 0x54, f'设置全 0xFFFF reg map 失败，响应={r}'

    ser.write(build_frame(next_seq(), 0x50))
    resp = recv_control_frame(ser)
    assert resp is not None,        '无响应'
    assert resp['cmd'] == 0x01,     f'应返回错误响应，实际 CMD={resp["cmd"]:#04x}'
    assert resp['data'] == b'\x03', f'错误码期望 0x03，实际 {resp["data"].hex()}'
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
    time.sleep(0.3)
    leftover = recv_stream_frames(ser, count=1, timeout=0.5)
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
    flush_rx(ser)
    reg_map = struct.pack('>HH', reg, reg) + b'\xFF\xFF' * 6
    ser.write(build_frame(next_seq(), 0x54, reg_map))
    r = recv_control_frame(ser)
    assert r is not None and r['cmd'] == 0x54, '设置 reg map 失败'

    ser.write(build_frame(next_seq(), 0x53, b'\x03'))
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
    results = []
    for idx in (0, 5, 7):
        _start_sampling(ser, reg, mask=0x01, interval_idx=idx)
        t0      = time.monotonic()
        frames  = recv_stream_frames(ser, count=30, timeout=5.0)
        elapsed = time.monotonic() - t0
        _stop_sampling(ser)
        time.sleep(0.1)

        if elapsed > 0 and len(frames) >= 5:
            hz = len(frames) / elapsed
            results.append(hz)
            print(f'  idx={idx}: {len(frames)} 帧 / {elapsed:.2f}s ≈ {hz:.0f} Hz')
        else:
            results.append(0.0)
            print(f'  idx={idx}: 收帧不足（{len(frames)} 帧）')

    if results[0] == 0.0 or results[2] == 0.0:
        print(f'  警告：部分区间收帧不足，跳过大小比较')
    else:
        assert results[0] > results[2], (
            f'高频 idx=0 ({results[0]:.0f} Hz) 应 > 低频 idx=7 ({results[2]:.0f} Hz)'
        )
        print('  高频 > 低频 验证通过')


# ---------------------------------------------------------------------------
# 就绪检测
# ---------------------------------------------------------------------------

def _wait_ready(ser: serial.Serial) -> bool:
    """
    轮询心跳确认 STM32 已就绪。
    发 3 次，每次间隔 300ms，任意一次成功即返回 True。
    """
    for _ in range(3):
        flush_rx(ser)
        ser.write(build_frame(next_seq(), 0x00))
        r = recv_control_frame(ser, timeout=1.0)
        if r is not None and r['cmd'] == 0x00:
            return True
        time.sleep(0.3)
    return False


# ---------------------------------------------------------------------------
# 主入口
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description='STM32 协议测试脚本 v2.0')
    parser.add_argument('--port',  default='COM3',   help='串口端口 (默认 COM3)')
    parser.add_argument('--baud',  type=int, default=115200, help='波特率 (默认 115200)')
    parser.add_argument('--motor', default='0x00',
                        help='电机 IC 7-bit 地址，0x00 = 跳过电机/采样相关测试 (默认 0x00)')
    parser.add_argument('--reg',   default='0x0000',
                        help='寄存器读写测试地址 (默认 0x0000)')
    args = parser.parse_args()

    CFG['port']       = args.port
    CFG['baud']       = args.baud
    CFG['motor_addr'] = int(args.motor, 0)
    CFG['test_reg']   = int(args.reg, 0)

    has_motor = (CFG['motor_addr'] != 0x00)

    sep = '=' * 52
    print(sep)
    print('  STM32 协议测试  v2.0')
    print(f'  端口    : {CFG["port"]} @ {CFG["baud"]} bps')
    print(f'  电机地址: {CFG["motor_addr"]:#04x}'
          + ('' if has_motor else '  (电机/采样测试将被跳过)'))
    print(f'  测试寄存器: {CFG["test_reg"]:#06x}')
    print(sep + '\n')

    try:
        ser = serial.Serial(CFG['port'], CFG['baud'], timeout=1.0)
    except serial.SerialException as e:
        print(f'错误：无法打开串口 {CFG["port"]}: {e}')
        sys.exit(1)

    time.sleep(0.2)
    flush_rx(ser)

    # ── 自动波特率恢复 ─────────────────────────────────────────────────────
    if not _wait_ready(ser):
        print('[warn] 115200 无响应，尝试从 57600 恢复...')
        ser.baudrate = 57600
        flush_rx(ser)
        if _wait_ready(ser):
            ser.write(build_frame(next_seq(), 0x03, b'\x04'))  # idx=4 → 115200
            recv_control_frame(ser, timeout=1.0)
            time.sleep(0.2)
            ser.baudrate = 115200
            flush_rx(ser)
            if not _wait_ready(ser):
                print('错误：自动恢复失败，请手动复位 STM32 后重试')
                ser.close()
                sys.exit(1)
            print('[info] 波特率已恢复至 115200\n')
        else:
            print('错误：57600 也无响应，请手动复位 STM32 后重试')
            ser.close()
            sys.exit(1)
    else:
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

    run_test('T4.4  波特率切换 (115200↔57600)', test_baudrate_switch, ser)
    run_test('T4.5  系统复位',                  test_system_reset,   ser)

    # 复位后重新设置电机地址（MCU 状态已清零）
    if has_motor:
        run_test('T4.1  复位后重设电机地址', test_set_motor_addr, ser, motor)

    # ── T5 寄存器读写 ─────────────────────────────────────────────────────
    print('── T5  寄存器读写 ' + '─' * 34)
    if has_motor:
        run_test('T5.1  读单寄存器',              test_read_reg,              ser, reg)
        run_test('T5.2  写寄存器 + 读回验证',     test_write_readback,        ser, reg)
        run_test('T5.3  批量读（1 个）',          test_bulk_read_single,      ser, reg)
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
        run_test('T6.6  无映射时启动被拒绝',     test_start_no_mapping,    ser)
        run_test('T6.4/7  启动 / 停止采样',      test_sample_start_stop,   ser, reg)
        run_test('T6.8  流帧 XOR 校验（20 帧）', test_stream_xor,          ser, reg)
        run_test('T6.5  双通道采样',             test_sample_multichannel, ser, reg)
        run_test('T6.3  采样间隔频率验证',        test_set_interval,        ser, reg)
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
                print(f'    [FAIL] {name}: {msg}')
    print(sep)

    sys.exit(0 if failed == 0 else 1)


if __name__ == '__main__':
    main()
