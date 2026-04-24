"""
诊断 PMIC Set Voltage (cmd=0x09) 超时问题
模拟上位机行为：打开串口 → 等待 → 发命令

用法: python test_pmic_cmd09_0420.py
"""

import serial
import time

PORT = 'COM6'
BAUD = 115200


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8005 if (crc & 1) else crc >> 1
    return crc


def build_frame(seq: int, cmd: int, data: bytes = b'') -> bytes:
    payload = bytes([seq, cmd, len(data)]) + data
    crc = crc16_modbus(payload)
    return b'\xAA\x55' + payload + bytes([crc >> 8, crc & 0xFF])


def hex_str(data: bytes) -> str:
    return ' '.join(f'{b:02X}' for b in data)


def send_and_recv(ser, seq, cmd, data=b'', label=''):
    frame = build_frame(seq, cmd, data)
    print(f"  TX: {hex_str(frame)}")
    ser.write(frame)
    resp = ser.read(64)
    result = 'PASS' if resp else 'FAIL (no response)'
    print(f"  RX: {hex_str(resp) if resp else '(no response)'}")
    print(f"  Result: {result}")
    return len(resp) > 0


pmic_data = bytes([0x00, 0xB4, 0x01, 0x18, 0x01, 0x40])

# === Test A: 快速发送（与之前相同） ===
print("=" * 60)
print("Test A: Open → short delay (0.1s) → send heartbeat → send PMIC")
print("=" * 60)
ser = serial.Serial(PORT, BAUD, timeout=0.5)
time.sleep(0.1)
ser.reset_input_buffer()

print("\n[Heartbeat]")
send_and_recv(ser, 0x00, 0x00)

time.sleep(0.1)

print("\n[PMIC Set Voltage]")
send_and_recv(ser, 0x01, 0x09, pmic_data)

ser.close()
time.sleep(1.0)

# === Test B: 模拟上位机 — 打开串口后等 5 秒再发 ===
print("\n" + "=" * 60)
print("Test B: Open → long delay (5s) → send PMIC (no heartbeat first)")
print("=" * 60)
ser = serial.Serial(PORT, BAUD, timeout=0.5)
print("  Port opened, waiting 5 seconds...")
time.sleep(5.0)
ser.reset_input_buffer()

print("\n[PMIC Set Voltage]")
send_and_recv(ser, 0x00, 0x09, pmic_data)

ser.close()
time.sleep(1.0)

# === Test C: 检查串口是否有残留数据干扰 ===
print("\n" + "=" * 60)
print("Test C: Open → flush → read any garbage → send PMIC")
print("=" * 60)
ser = serial.Serial(PORT, BAUD, timeout=0.5)
time.sleep(0.1)

# Read any garbage bytes
garbage = ser.read(256)
if garbage:
    print(f"  Garbage on open: {hex_str(garbage)}")
else:
    print("  No garbage on open")

ser.reset_input_buffer()

print("\n[PMIC Set Voltage]")
send_and_recv(ser, 0x00, 0x09, pmic_data)

ser.close()

print("\nAll tests done.")
