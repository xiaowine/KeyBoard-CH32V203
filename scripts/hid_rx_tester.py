
#!/usr/bin/env python3
"""
KeyBoard 固件 HID 接收状态机测试脚本。

依赖：
    pip install hidapi

默认目标 VID/PID 来自 USB 描述符：
    VID = 0x1A86
    PID = 0xFE00
"""

from __future__ import annotations

import argparse
import sys
import time
import zlib
from dataclasses import dataclass
from typing import Optional

import hid

FRAME_TYPE_SINGLE = 0b000
FRAME_TYPE_START = 0b001
FRAME_TYPE_DATA = 0b010
FRAME_TYPE_END = 0b011
FRAME_TYPE_ACK = 0b100
FRAME_TYPE_NACK = 0b101
FRAME_TYPE_BUSY = 0b110
FRAME_TYPE_OF = 0b111

DATA_TYPE_KEY = 0

FRAME_SIZE = 32
PAYLOAD_SIZE = 24
CRC_INPUT_SIZE = 28


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
    return parse_frame(frame)


def expect_ctrl(dev: hid.device, exp_type: int, exp_seq: int, exp_data_type: int, timeout_ms: int) -> None:
    rsp = read_frame(dev, timeout_ms)
    if rsp.frame_type != exp_type or rsp.seq != exp_seq or rsp.data_type != exp_data_type:
        raise AssertionError(
            f"响应不符合预期: type={rsp.frame_type} seq={rsp.seq} data_type={rsp.data_type}, "
            f"期望 type={exp_type} seq={exp_seq} data_type={exp_data_type}"
        )


def test_single_ok(dev: hid.device, timeout_ms: int, crc_mode: str) -> None:
    seq = 1
    payload = b"hello-hid-rx"
    frame = build_frame(FRAME_TYPE_SINGLE, DATA_TYPE_KEY, seq, payload, crc_mode=crc_mode)
    write_frame(dev, frame)
    expect_ctrl(dev, FRAME_TYPE_ACK, seq, DATA_TYPE_KEY, timeout_ms)


def test_crc_error_nack(dev: hid.device, timeout_ms: int, crc_mode: str) -> None:
    seq = 2
    payload = b"crc-bad"
    frame = build_frame(FRAME_TYPE_SINGLE, DATA_TYPE_KEY, seq, payload, crc_mode=crc_mode, bad_crc=True)
    write_frame(dev, frame)
    expect_ctrl(dev, FRAME_TYPE_NACK, seq, DATA_TYPE_KEY, timeout_ms)


def test_segment_retry_recovery(dev: hid.device, timeout_ms: int, crc_mode: str) -> None:
    seq_start = 10
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


def auto_detect_crc_mode(dev: hid.device, timeout_ms: int) -> str:
    """自动探测能让设备返回 ACK 的 CRC 配置。"""
    probe_payload = b"crc-probe"
    for idx, mode in enumerate(CRC_PROFILES.keys()):
        seq = (0x60 + idx) & 0xFF
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
    parser.add_argument("--pid", type=lambda x: int(x, 0), default=0xFE00, help="USB PID（默认: 0xFE00）")
    parser.add_argument("--timeout-ms", type=int, default=500, help="读取超时（毫秒，默认: 500）")
    parser.add_argument(
        "--crc-mode",
        type=str,
        default="auto",
        choices=["auto", *CRC_PROFILES.keys()],
        help="CRC 计算模式（默认: auto 自动探测）",
    )
    parser.add_argument(
        "--tests",
        type=str,
        default="all",
        help="要运行的测试，逗号分隔，或 'all'（默认: all）。可用: single_ok, crc_error_nack, segment_retry_recovery",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    dev = hid.device()
    try:
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
        return 2
    finally:
        try:
            dev.close()
        except Exception:
            pass


if __name__ == "__main__":
    sys.exit(main())