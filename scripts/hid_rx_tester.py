
#!/usr/bin/env python3
"""
KeyBoard 固件 HID 接收状态机测试脚本。

依赖：
    pip install hidapi

默认目标 VID/PID 来自 USB 描述符：
    VID = 0x1A86
    PID = 0x2004
"""

from __future__ import annotations

import argparse
import sys
import time
import zlib
from dataclasses import dataclass
from typing import Optional

import hid
import traceback
import struct
from typing import Any

FRAME_TYPE_SINGLE = 0b000
FRAME_TYPE_START = 0b001
FRAME_TYPE_DATA = 0b010
FRAME_TYPE_END = 0b011
FRAME_TYPE_ACK = 0b100
FRAME_TYPE_NACK = 0b101
FRAME_TYPE_BUSY = 0b110
FRAME_TYPE_OF = 0b111

DATA_TYPE_KEY = 0
DATA_TYPE_GET_KEY = 2
DATA_TYPE_LAYER_KEYMAP = 1
DATA_TYPE_GET_LAYER_KEYMAP = 3
DATA_TYPE_GET_ALL_LAYER_KEYMAP = 4

FRAME_SIZE = 32
PAYLOAD_SIZE = 24
CRC_INPUT_SIZE = 28

# 控制脚本日志详细度：0=元信息（默认），1=包含 payload 前若干字节
HID_VERBOSE = 0



@dataclass
class ParsedFrame:
    frame_type: int
    length: int
    data_type: int
    seq: int
    reserved: int
    payload: bytes
    crc32: int
    raw: bytes


def _crc_word_stream(data_28: bytes, word_byteorder: str) -> int:
    """按 32-bit 字流计算 CRC（多项式 0x04C11DB7）。"""
    if len(data_28) != CRC_INPUT_SIZE:
        raise ValueError(f"CRC 输入必须是 {CRC_INPUT_SIZE} 字节")

    crc = 0xFFFFFFFF
    for i in range(0, CRC_INPUT_SIZE, 4):
        word = int.from_bytes(data_28[i : i + 4], word_byteorder, signed=False)
        crc ^= word
        for _ in range(32):
            if crc & 0x80000000:
                crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF
            else:
                crc = (crc << 1) & 0xFFFFFFFF
    return crc


def crc32_ch32_word_le(data_28: bytes) -> int:
    """候选1：按小端 32-bit 字喂入 CRC（最贴近当前固件写法）。"""
    return _crc_word_stream(data_28, "little")


def crc32_ch32_word_be(data_28: bytes) -> int:
    """候选2：按大端 32-bit 字喂入 CRC。"""
    return _crc_word_stream(data_28, "big")


def crc32_mpeg2_bytewise(data_28: bytes) -> int:
    """候选3：CRC-32/MPEG-2（按字节流 MSB-first）。"""
    if len(data_28) != CRC_INPUT_SIZE:
        raise ValueError(f"CRC 输入必须是 {CRC_INPUT_SIZE} 字节")
    crc = 0xFFFFFFFF
    for b in data_28:
        crc ^= (b << 24)
        for _ in range(8):
            if crc & 0x80000000:
                crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF
            else:
                crc = (crc << 1) & 0xFFFFFFFF
    return crc


def crc32_ieee(data_28: bytes) -> int:
    """候选4：标准 CRC-32/IEEE（zlib）。"""
    if len(data_28) != CRC_INPUT_SIZE:
        raise ValueError(f"CRC 输入必须是 {CRC_INPUT_SIZE} 字节")
    return zlib.crc32(data_28) & 0xFFFFFFFF


CRC_PROFILES = {
    "ch32-word-le": (crc32_ch32_word_le, "little"),
}


def build_frame(
    frame_type: int,
    data_type: int,
    seq: int,
    payload: bytes,
    crc_mode: str,
    bad_crc: bool = False,
) -> bytes:
    if len(payload) > PAYLOAD_SIZE:
        raise ValueError(f"payload 过长: {len(payload)} > {PAYLOAD_SIZE}")
    if crc_mode not in CRC_PROFILES:
        raise ValueError(f"未知 CRC 模式: {crc_mode}")

    ctrl = ((frame_type & 0x07) << 5) | (len(payload) & 0x1F)
    buf = bytearray(FRAME_SIZE)
    buf[0] = ctrl
    buf[1] = data_type & 0xFF
    buf[2] = seq & 0xFF
    buf[3] = 0
    buf[4 : 4 + len(payload)] = payload

    crc_func, crc_store_endian = CRC_PROFILES[crc_mode]
    crc = crc_func(bytes(buf[:CRC_INPUT_SIZE]))
    if bad_crc:
        crc ^= 0x00000001
    buf[28:32] = crc.to_bytes(4, crc_store_endian, signed=False)
    return bytes(buf)


def parse_frame(frame: bytes) -> ParsedFrame:
    if len(frame) != FRAME_SIZE:
        raise ValueError(f"帧长度不匹配: {len(frame)}")

    ctrl = frame[0]
    frame_type = (ctrl >> 5) & 0x07
    length = ctrl & 0x1F
    return ParsedFrame(
        frame_type=frame_type,
        length=length,
        data_type=frame[1],
        seq=frame[2],
        reserved=frame[3],
        payload=frame[4:28],
        crc32=int.from_bytes(frame[28:32], "little", signed=False),
        raw=frame,
    )


def decode_report(data: list[int]) -> Optional[bytes]:
    if not data:
        return None

    raw = bytes(data)
    if len(raw) == FRAME_SIZE:
        return raw

    if len(raw) == FRAME_SIZE + 1 and raw[0] == 0x00:
        return raw[1:]

    if len(raw) > FRAME_SIZE:
        return raw[-FRAME_SIZE:]

    return None


def flush_input(dev: hid.device) -> None:
    dev.set_nonblocking(True)
    try:
        while True:
            data = dev.read(64)
            if not data:
                break
    finally:
        dev.set_nonblocking(False)


def write_frame(dev: hid.device, frame: bytes) -> None:
    attempts = [b"\x00" + frame, frame]
    last_error = None
    for out in attempts:
        try:
            written = dev.write(out)
            if written > 0:
                return
        except OSError as exc:
            last_error = exc
    if last_error is not None:
        raise last_error
    raise RuntimeError("HID 写入失败")


def read_frame(dev: hid.device, timeout_ms: int) -> ParsedFrame:
    data = dev.read(64, timeout_ms)
    frame = decode_report(data)
    if frame is None:
        raise TimeoutError(f"{timeout_ms}ms 内未收到有效帧")
    parsed = parse_frame(frame)
    return parsed


def expect_ctrl(dev: hid.device, exp_type: int, exp_seq: int, exp_data_type: int, timeout_ms: int) -> None:
    rsp = read_frame(dev, timeout_ms)
    if rsp.frame_type != exp_type or rsp.seq != exp_seq or rsp.data_type != exp_data_type:
        raise AssertionError(
            f"响应不符合预期: type={rsp.frame_type} seq={rsp.seq} data_type={rsp.data_type}, "
            f"期望 type={exp_type} seq={exp_seq} data_type={exp_data_type}"
        )


def request_and_receive(dev: hid.device, timeout_ms: int, crc_mode: str, get_type: int):
    """对设备发送 GET 请求并接收由设备发起的 SINGLE 或 START/DATA/END 传输。

    返回 (data_type, payload_bytes)
    """
    seq = 0
    req = build_frame(FRAME_TYPE_SINGLE, get_type, seq, b"", crc_mode=crc_mode)
    flush_input(dev)
    write_frame(dev, req)
    expect_ctrl(dev, FRAME_TYPE_ACK, seq, get_type, timeout_ms)

    start_time = time.time()
    overall_timeout = max(timeout_ms / 1000.0, 0.5) * 20

    recv_buf = None
    total_expected = 0
    recved = 0
    expect_seq = None

    while time.time() - start_time < overall_timeout:
        try:
            frame = read_frame(dev, timeout_ms)
        except TimeoutError:
            continue

        # 忽略控制帧
        if frame.frame_type in (FRAME_TYPE_ACK, FRAME_TYPE_NACK, FRAME_TYPE_BUSY, FRAME_TYPE_OF):
            continue

        # SINGLE (strictly by frame type)
        if frame.frame_type == FRAME_TYPE_SINGLE:
            if frame.seq != 0:
                write_frame(dev, build_frame(FRAME_TYPE_NACK, frame.data_type, frame.seq, b"", crc_mode=crc_mode))
                raise AssertionError("SINGLE frame seq must be 0")
            write_frame(dev, build_frame(FRAME_TYPE_ACK, frame.data_type, frame.seq, b"", crc_mode=crc_mode))
            payload = frame.payload[: frame.length]
            return frame.data_type, bytes(payload)

        # START (strictly by frame type)
        if frame.frame_type == FRAME_TYPE_START:
            if frame.seq != 0:
                write_frame(dev, build_frame(FRAME_TYPE_NACK, frame.data_type, frame.seq, b"", crc_mode=crc_mode))
                raise AssertionError("START frame seq must be 0")

            if frame.length < 4:
                write_frame(dev, build_frame(FRAME_TYPE_NACK, frame.data_type, frame.seq, b"", crc_mode=crc_mode))
                raise AssertionError("START payload too short for total size")

            total_expected = int.from_bytes(frame.payload[:4], "little")
            if total_expected == 0:
                write_frame(dev, build_frame(FRAME_TYPE_NACK, frame.data_type, frame.seq, b"", crc_mode=crc_mode))
                raise AssertionError("START total size == 0")

            recv_buf = bytearray(total_expected)
            recved = 0
            first_chunk = frame.payload[4 : 4 + (frame.length - 4)]
            if first_chunk:
                to_copy = min(len(first_chunk), total_expected - recved)
                recv_buf[recved:recved + to_copy] = first_chunk[:to_copy]
                recved += to_copy

            expect_seq = (frame.seq + 1) & 0xFF
            write_frame(dev, build_frame(FRAME_TYPE_ACK, frame.data_type, frame.seq, b"", crc_mode=crc_mode))

            while recved < total_expected:
                try:
                    f2 = read_frame(dev, timeout_ms * 4)
                except TimeoutError:
                    write_frame(dev, build_frame(FRAME_TYPE_NACK, frame.data_type, expect_seq, b"", crc_mode=crc_mode))
                    raise TimeoutError("等待 DATA/END 超时")

                if f2.frame_type in (FRAME_TYPE_ACK, FRAME_TYPE_NACK, FRAME_TYPE_BUSY, FRAME_TYPE_OF):
                    continue

                if f2.data_type != frame.data_type or f2.seq != expect_seq:
                    write_frame(dev, build_frame(FRAME_TYPE_NACK, f2.data_type, f2.seq, b"", crc_mode=crc_mode))
                    raise AssertionError(f"Unexpected seq/type: got seq={f2.seq} expected={expect_seq}")

                to_copy = min(f2.length, total_expected - recved)
                if to_copy > 0:
                    recv_buf[recved:recved + to_copy] = f2.payload[:to_copy]
                    recved += to_copy

                write_frame(dev, build_frame(FRAME_TYPE_ACK, f2.data_type, f2.seq, b"", crc_mode=crc_mode))

                if f2.frame_type == FRAME_TYPE_END:
                    if recved != total_expected:
                        raise AssertionError("END received but length mismatch")
                    return frame.data_type, bytes(recv_buf)

                expect_seq = (expect_seq + 1) & 0xFF

    raise TimeoutError("未在规定时间内收到设备发起的数据")


def parse_keymap_payload(payload: bytes) -> None:
    """解析并打印 KeyMapping 数组：packed struct: uint8 modifiers; uint16 codes[3]; uint8 type"""
    KEY_TOTAL_KEYS = 24
    ENTRY_SIZE = 8
    if len(payload) % ENTRY_SIZE != 0:
        print(f"警告: keymap 长度 {len(payload)} 不是 {ENTRY_SIZE} 的整数倍")
    entries = len(payload) // ENTRY_SIZE
    fmt = "<BHHHB"  # modifiers, code0, code1, code2, type
    for i in range(entries):
        off = i * ENTRY_SIZE
        try:
            modifiers, c0, c1, c2, typ = struct.unpack_from(fmt, payload, off)
        except struct.error:
            print(f"无法解析条目 {i}")
            continue
        codes = [c0, c1, c2]
        # 若 payload 长度是多层拼接，则附带层与索引信息
        layer = i // KEY_TOTAL_KEYS
        idx = i % KEY_TOTAL_KEYS
        if entries > KEY_TOTAL_KEYS:
            print(f"Layer {layer} Entry {idx:02d}: type={typ} mods=0x{modifiers:02X} codes={[hex(x) for x in codes]}")
        else:
            print(f"Entry {idx:02d}: type={typ} mods=0x{modifiers:02X} codes={[hex(x) for x in codes]}")


def test_get_layer_keymap(dev: hid.device, timeout_ms: int, crc_mode: str) -> None:
    """请求单层 keymap 并解析打印"""
    data_type, payload = request_and_receive(dev, timeout_ms, crc_mode, DATA_TYPE_GET_LAYER_KEYMAP)
    if data_type != DATA_TYPE_LAYER_KEYMAP:
        raise AssertionError(f"期望 data_type={DATA_TYPE_LAYER_KEYMAP}，但收到 {data_type}")
    print(f"Received layer keymap, len={len(payload)}")
    parse_keymap_payload(payload)


def test_get_all_layer_keymap(dev: hid.device, timeout_ms: int, crc_mode: str) -> None:
    """请求所有层 keymap 并解析打印"""
    data_type, payload = request_and_receive(dev, timeout_ms, crc_mode, DATA_TYPE_GET_ALL_LAYER_KEYMAP)
    if data_type != DATA_TYPE_LAYER_KEYMAP:
        raise AssertionError(f"期望 data_type={DATA_TYPE_LAYER_KEYMAP}，但收到 {data_type}")
    print(f"Received all-layers keymap, len={len(payload)}")
    parse_keymap_payload(payload)


def test_single_ok(dev: hid.device, timeout_ms: int, crc_mode: str) -> None:
    seq = 0
    payload = b"hello-hid-rx"
    frame = build_frame(FRAME_TYPE_SINGLE, DATA_TYPE_KEY, seq, payload, crc_mode=crc_mode)
    write_frame(dev, frame)
    expect_ctrl(dev, FRAME_TYPE_ACK, seq, DATA_TYPE_KEY, timeout_ms)


def test_crc_error_nack(dev: hid.device, timeout_ms: int, crc_mode: str) -> None:
    seq = 0
    payload = b"crc-bad"
    frame = build_frame(FRAME_TYPE_SINGLE, DATA_TYPE_KEY, seq, payload, crc_mode=crc_mode, bad_crc=True)
    write_frame(dev, frame)
    expect_ctrl(dev, FRAME_TYPE_NACK, seq, DATA_TYPE_KEY, timeout_ms)


def test_segment_retry_recovery(dev: hid.device, timeout_ms: int, crc_mode: str) -> None:
    seq_start = 0
    data = bytes(range(30))

    # START：payload[0:4] = total_size（小端）
    start_payload = len(data).to_bytes(4, "little")
    write_frame(dev, build_frame(FRAME_TYPE_START, DATA_TYPE_KEY, seq_start, start_payload, crc_mode=crc_mode))
    expect_ctrl(dev, FRAME_TYPE_ACK, seq_start, DATA_TYPE_KEY, timeout_ms)

    # 先发错误序号的 DATA -> 期望收到 NACK 且 RX 上下文仍保留。
    wrong_seq = (seq_start + 2) & 0xFF
    chunk1 = data[:24]
    write_frame(dev, build_frame(FRAME_TYPE_DATA, DATA_TYPE_KEY, wrong_seq, chunk1, crc_mode=crc_mode))
    expect_ctrl(dev, FRAME_TYPE_NACK, wrong_seq, DATA_TYPE_KEY, timeout_ms)

    # 用正确序号重发 -> 期望收到 ACK。
    seq_data = (seq_start + 1) & 0xFF
    write_frame(dev, build_frame(FRAME_TYPE_DATA, DATA_TYPE_KEY, seq_data, chunk1, crc_mode=crc_mode))
    expect_ctrl(dev, FRAME_TYPE_ACK, seq_data, DATA_TYPE_KEY, timeout_ms)

    # END 发送剩余字节。
    seq_end = (seq_start + 2) & 0xFF
    chunk2 = data[24:]
    write_frame(dev, build_frame(FRAME_TYPE_END, DATA_TYPE_KEY, seq_end, chunk2, crc_mode=crc_mode))
    expect_ctrl(dev, FRAME_TYPE_ACK, seq_end, DATA_TYPE_KEY, timeout_ms)


def test_single_invalid_seq(dev: hid.device, timeout_ms: int, crc_mode: str) -> None:
    """发送单包但 seq != 0，应收到 NACK"""
    seq = 5
    payload = b"bad-seq-single"
    frame = build_frame(FRAME_TYPE_SINGLE, DATA_TYPE_KEY, seq, payload, crc_mode=crc_mode)
    write_frame(dev, frame)
    expect_ctrl(dev, FRAME_TYPE_NACK, seq, DATA_TYPE_KEY, timeout_ms)


def test_start_invalid_seq(dev: hid.device, timeout_ms: int, crc_mode: str) -> None:
    """发送 START 但 seq != 0，应收到 NACK"""
    seq = 3
    total_data = bytes(range(10))
    start_payload = len(total_data).to_bytes(4, "little")
    write_frame(dev, build_frame(FRAME_TYPE_START, DATA_TYPE_KEY, seq, start_payload, crc_mode=crc_mode))
    expect_ctrl(dev, FRAME_TYPE_NACK, seq, DATA_TYPE_KEY, timeout_ms)


def test_device_send_receive(dev: hid.device, timeout_ms: int, crc_mode: str) -> None:
    """等待设备主动发送数据并完成重组，接收到每个数据帧后会自动回 ACK。

    本测试用于验证设备到主机（Device->Host）方向的发送逻辑。
    """
    # 发送 GET 请求以触发 MCU 向主机发送（Device->Host）数据
    seq = 0
    req = build_frame(FRAME_TYPE_SINGLE, DATA_TYPE_GET_KEY, seq, b"", crc_mode=crc_mode)
    flush_input(dev)
    write_frame(dev, req)
    # 期望设备 ACK 我们的请求
    expect_ctrl(dev, FRAME_TYPE_ACK, seq, DATA_TYPE_GET_KEY, timeout_ms)

    # 等待并处理一次由设备发起的传输（支持 SINGLE 或 START+DATA+END）
    start_time = time.time()
    overall_timeout = max(timeout_ms / 1000.0, 0.5) * 20

    recv_buf = None
    total_expected = 0
    recved = 0
    expect_seq = None

    while time.time() - start_time < overall_timeout:
        try:
            frame = read_frame(dev, timeout_ms)
        except TimeoutError:
            # 继续等待设备发起
            continue

        # 打印诊断信息（原始 ctrl + 解析结果）
        ctrl_raw = frame.raw[0]
        typeA = (ctrl_raw >> 5) & 0x07
        lenA = ctrl_raw & 0x1F
        typeB = ctrl_raw & 0x07
        lenB = (ctrl_raw >> 3) & 0x1F
        print(
            f"[Recv] raw_ctrl=0x{ctrl_raw:02X} | A:type={typeA} len={lenA} | B:type={typeB} len={lenB} | parsed:type={frame.frame_type} seq={frame.seq} len={frame.length} data_type={frame.data_type}"
        )

        # 忽略控制帧（ACK/NACK/BUSY/OF）
        if frame.frame_type in (FRAME_TYPE_ACK, FRAME_TYPE_NACK, FRAME_TYPE_BUSY, FRAME_TYPE_OF):
            print(f"[Control] received ctrl frame type={frame.frame_type} seq={frame.seq}")
            continue

        # 优先按 SINGLE 处理（严格按 frame_type）
        is_single_like = frame.frame_type == FRAME_TYPE_SINGLE
        if is_single_like:
            if frame.seq != 0:
                write_frame(dev, build_frame(FRAME_TYPE_NACK, frame.data_type, frame.seq, b"", crc_mode=crc_mode))
                raise AssertionError(f"SINGLE frame seq must be 0, got={frame.seq}")
            write_frame(dev, build_frame(FRAME_TYPE_ACK, frame.data_type, frame.seq, b"", crc_mode=crc_mode))
            payload = frame.payload[: frame.length]
            # 当设备返回键位快照（HC165_COUNT 字节）时，以二进制位图打印便于观察按键状态
            if frame.data_type == DATA_TYPE_KEY and len(payload) >= 1:
                raw = int.from_bytes(payload, "little")
                if raw == 0:
                    keys = 0
                else:
                    keys = (~raw) & 0x00FFFFFF
                print(f"[Complete] SINGLE payload ({len(payload)}): raw=0x{raw:06X} keys=0b{keys:024b}")
            else:
                print(f"[Complete] SINGLE payload ({len(payload)}): {payload!r}")
            return

        # 处理 START（严格按 frame_type）
        is_start_like = frame.frame_type == FRAME_TYPE_START
        if is_start_like:
            if frame.seq != 0:
                write_frame(dev, build_frame(FRAME_TYPE_NACK, frame.data_type, frame.seq, b"", crc_mode=crc_mode))
                raise AssertionError(f"START frame seq must be 0, got={frame.seq}")

            if frame.length < 4:
                write_frame(dev, build_frame(FRAME_TYPE_NACK, frame.data_type, frame.seq, b"", crc_mode=crc_mode))
                raise AssertionError("START payload too short for total size")

            total_expected = int.from_bytes(frame.payload[:4], "little")
            if total_expected == 0:
                write_frame(dev, build_frame(FRAME_TYPE_NACK, frame.data_type, frame.seq, b"", crc_mode=crc_mode))
                raise AssertionError("START total size == 0")

            recv_buf = bytearray(total_expected)
            recved = 0
            # 若 START 带首段数据，复制进 buffer
            first_chunk = frame.payload[4 : 4 + (frame.length - 4)]
            if first_chunk:
                to_copy = min(len(first_chunk), total_expected - recved)
                recv_buf[recved:recved + to_copy] = first_chunk[:to_copy]
                recved += to_copy

            expect_seq = (frame.seq + 1) & 0xFF
            write_frame(dev, build_frame(FRAME_TYPE_ACK, frame.data_type, frame.seq, b"", crc_mode=crc_mode))

            # 接收后续 DATA / END
            while recved < total_expected:
                try:
                    f2 = read_frame(dev, timeout_ms * 4)
                except TimeoutError:
                    write_frame(dev, build_frame(FRAME_TYPE_NACK, frame.data_type, expect_seq, b"", crc_mode=crc_mode))
                    raise TimeoutError("等待 DATA/END 超时")

                if f2.frame_type in (FRAME_TYPE_ACK, FRAME_TYPE_NACK, FRAME_TYPE_BUSY, FRAME_TYPE_OF):
                    print(f"[Control] follow frame type={f2.frame_type} seq={f2.seq}")
                    continue

                print(f"[Recv] follow type={f2.frame_type} seq={f2.seq} len={f2.length}")
                if f2.data_type != frame.data_type or f2.seq != expect_seq:
                    write_frame(dev, build_frame(FRAME_TYPE_NACK, f2.data_type, f2.seq, b"", crc_mode=crc_mode))
                    raise AssertionError(f"Unexpected seq/type: got seq={f2.seq} expected={expect_seq}")

                to_copy = min(f2.length, total_expected - recved)
                if to_copy > 0:
                    recv_buf[recved:recved + to_copy] = f2.payload[:to_copy]
                    recved += to_copy

                write_frame(dev, build_frame(FRAME_TYPE_ACK, f2.data_type, f2.seq, b"", crc_mode=crc_mode))

                if f2.frame_type == FRAME_TYPE_END:
                    if recved != total_expected:
                        raise AssertionError(f"END received but length mismatch recved={recved} expected={total_expected}")
                    # 若是键位快照类型，打印二进制；否则直接打印原始 bytes
                    if frame.data_type == DATA_TYPE_KEY and recv_buf is not None:
                        raw = int.from_bytes(bytes(recv_buf[:recved]), "little")
                        if raw == 0:
                            keys = 0
                        else:
                            keys = (~raw) & 0x00FFFFFF
                        print(f"[Complete] START/DATA/END payload ({recved}): raw=0x{raw:06X} keys=0b{keys:024b}")
                    else:
                        print(f"[Complete] START/DATA/END payload ({recved}): {bytes(recv_buf)!r}")
                    return

                expect_seq = (expect_seq + 1) & 0xFF

    raise TimeoutError("未在规定时间内收到设备发起的数据")


def auto_detect_crc_mode(dev: hid.device, timeout_ms: int) -> str:
    """自动探测能让设备返回 ACK 的 CRC 配置。"""
    probe_payload = b"crc-probe"
    for idx, mode in enumerate(CRC_PROFILES.keys()):
        # 按协议要求，单次任务的 seq 必须以 0 开头；每次探测前会 flush_input()
        seq = 0
        flush_input(dev)
        frame = build_frame(FRAME_TYPE_SINGLE, DATA_TYPE_KEY, seq, probe_payload, crc_mode=mode)
        write_frame(dev, frame)
        try:
            rsp = read_frame(dev, timeout_ms)
        except Exception as exc:
            print(f"[探测] 模式={mode}: 无响应/超时 ({exc})")
            continue

        print(f"[探测] 模式={mode}: 收到 type={rsp.frame_type} seq={rsp.seq}")
        if rsp.frame_type == FRAME_TYPE_ACK and rsp.seq == seq:
            print(f"[探测] 已匹配 CRC 模式: {mode}")
            return mode

    raise RuntimeError("未探测到可用 CRC 模式，请检查固件 CRC 计算或抓包确认")


def run_all(dev: hid.device, timeout_ms: int, crc_mode: str) -> int:
    tests_map = {
        "single_ok": test_single_ok,
        "crc_error_nack": test_crc_error_nack,
        "segment_retry_recovery": test_segment_retry_recovery,
        "single_invalid_seq": test_single_invalid_seq,
        "start_invalid_seq": test_start_invalid_seq,
        "device_send_receive": test_device_send_receive,
        "get_layer_keymap": test_get_layer_keymap,
        "get_all_layer_keymap": test_get_all_layer_keymap,
    }

    # 默认运行全部
    tests = list(tests_map.items())

    passed = 0
    for name, fn in tests:
        flush_input(dev)
        time.sleep(0.02)
        try:
                    fn(dev, timeout_ms, crc_mode)
                    print(f"[通过] {name}")
                    passed += 1
        except Exception as exc:
                    print(f"[失败] {name}: {exc}")
                    print(f"\n结果: {passed}/{len(tests)} 项通过")
    return 0 if passed == len(tests) else 1


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="通过 hidapi 测试 HID 接收状态机")
    parser.add_argument("--vid", type=lambda x: int(x, 0), default=0x1A86, help="USB VID（默认: 0x1A86）")
    parser.add_argument("--pid", type=lambda x: int(x, 0), default=0x2004, help="USB PID（默认: 0x2004）")
    parser.add_argument("--timeout-ms", type=int, default=500, help="读取超时（毫秒，默认: 500）")
    parser.add_argument(
        "--crc-mode",
        type=str,
        default="auto",
        choices=["auto", *CRC_PROFILES.keys()],
        help="CRC 计算模式（默认: auto 自动探测）",
    )
    parser.add_argument(
        "--path",
        type=str,
        default=None,
        help="指定要打开的 HID 设备路径（优先）。可通过枚举输出获取",
    )
    parser.add_argument(
        "--interface",
        type=int,
        default=2,
        help="按枚举中的 interface_number 选择接口（整数），例如 0,1,2...",
    )
    parser.add_argument(
        "--tests",
        type=str,
        default="all",
        help="要运行的测试，逗号分隔，或 'all'（默认: all）。可用: single_ok, crc_error_nack, segment_retry_recovery, device_send_receive, get_layer_keymap, get_all_layer_keymap",
    )
    parser.add_argument(
        "--hid-verbose",
        type=int,
        choices=[0, 1],
        default=2,
        help="脚本 HID 日志详细度：0=元信息（默认），1=包含 payload 转储",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    global HID_VERBOSE
    HID_VERBOSE = int(args.hid_verbose)
    dev = hid.device()
    try:
        # 列举匹配的 HID 设备，帮助诊断 PID/路径 是否正确
        devs = hid.enumerate(args.vid, args.pid)

        # 如果用户通过 --interface 指定了接口号，先筛选仅匹配该接口的枚举结果
        if args.interface is not None:
            filtered = [d for d in devs if d.get("interface_number") == args.interface]
            print(f"按 interface={args.interface} 筛选枚举设备，匹配到 {len(filtered)} 个")
            if filtered:
                devs = filtered

        # 尝试按枚举的 path 逐个打开（优先跳过由系统作为键盘占用的接口）
        opened = False
        for i, d in enumerate(devs):
            path = d.get("path")
            # 跳过明显被系统键盘占用的路径提示（带 "\\\\KBD" 的路径）
            if path and b"\\KBD" in path:
                print(f"  跳过被系统占用的接口 [{i}] {path!r}")
                continue
            try:
                print(f"  尝试打开枚举设备 [{i}] path={path!r} ...")
                dev.open_path(path)
                # 非阻塞试读一次以验证设备是否可读（若被系统占用会抛 OSError）
                dev.set_nonblocking(True)
                try:
                    _ = dev.read(1)
                except OSError as exc:
                    print(f"    打开后读取失败: {exc}")
                    try:
                        dev.close()
                    except Exception:
                        pass
                    continue
                dev.set_nonblocking(False)
                print(f"已打开 HID 设备 path={path!r} (index {i})")
                opened = True
                break
            except OSError as exc:
                print(f"    open_path 失败: {exc}")
                try:
                    dev.close()
                except Exception:
                    pass
                continue

        if not opened:
            print("尝试使用 VID/PID 直接打开（回退）...")
            dev.open(args.vid, args.pid)
            dev.set_nonblocking(False)
            print(f"已打开 HID 设备 VID=0x{args.vid:04X}, PID=0x{args.pid:04X}")

        crc_mode = args.crc_mode
        if crc_mode == "auto":
            crc_mode = auto_detect_crc_mode(dev, args.timeout_ms)
        else:
            print(f"使用指定 CRC 模式: {crc_mode}")

        # 解析 --tests 参数
        tests_arg = args.tests.strip()
        tests_map = {
            "single_ok": test_single_ok,
            "crc_error_nack": test_crc_error_nack,
            "segment_retry_recovery": test_segment_retry_recovery,
            "single_invalid_seq": test_single_invalid_seq,
            "start_invalid_seq": test_start_invalid_seq,
            "device_send_receive": test_device_send_receive,
            "get_layer_keymap": test_get_layer_keymap,
            "get_all_layer_keymap": test_get_all_layer_keymap,
        }

        if tests_arg.lower() == "all":
            selected_tests = list(tests_map.items())
        else:
            names = [s.strip() for s in tests_arg.split(",") if s.strip()]
            invalid = [n for n in names if n not in tests_map]
            if invalid:
                print(f"未知测试项: {invalid}. 可用测试: {list(tests_map.keys())}")
                return 3
            selected_tests = [(n, tests_map[n]) for n in names]

        # 运行选中的测试
        passed = 0
        for name, fn in selected_tests:
            flush_input(dev)
            time.sleep(0.02)
            try:
                fn(dev, args.timeout_ms, crc_mode)
                print(f"[通过] {name}")
                passed += 1
            except Exception as exc:
                print(f"[失败] {name}: {exc}")

        print(f"\n结果: {passed}/{len(selected_tests)} 项通过")
        return 0 if passed == len(selected_tests) else 1
    except OSError as exc:
        print(f"打开设备失败: {exc}")
        traceback.print_exc()
        return 2
    finally:
        try:
            dev.close()
        except Exception:
            pass


if __name__ == "__main__":
    sys.exit(main())