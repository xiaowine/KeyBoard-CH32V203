#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import argparse
import struct
import sys
import time
from typing import Dict, List, Optional, Tuple

import hid


FRAME_SIZE = 64
CRC_INPUT_SIZE = 60
MAX_DATA_PAYLOAD = 56
POLY = 0x04C11DB7

FRAME_TYPE_ERROR = 0
FRAME_TYPE_START = 1
FRAME_TYPE_DATA = 2
FRAME_TYPE_ACK = 3
FRAME_TYPE_NACK = 4


def stm32_crc32_words_le(data60: bytes) -> int:
    if len(data60) != CRC_INPUT_SIZE:
        raise ValueError(f"CRC 输入长度必须为 {CRC_INPUT_SIZE} 字节")

    crc = 0xFFFFFFFF
    for i in range(0, CRC_INPUT_SIZE, 4):
        word = struct.unpack_from("<I", data60, i)[0]
        crc ^= word
        for _ in range(32):
            if crc & 0x80000000:
                crc = ((crc << 1) ^ POLY) & 0xFFFFFFFF
            else:
                crc = (crc << 1) & 0xFFFFFFFF
    return crc


def build_frame(
    seq: int,
    frame_type: int,
    payload_type: int = 0,
    payload_data: bytes = b"",
    force_bad_crc: bool = False,
) -> bytes:
    payload_data = payload_data[:MAX_DATA_PAYLOAD]
    payload_len = len(payload_data)

    frame = bytearray(FRAME_SIZE)
    frame[0] = seq & 0xFF
    frame[1] = frame_type & 0xFF
    frame[2] = payload_len & 0xFF
    frame[3] = payload_type & 0xFF
    frame[4 : 4 + payload_len] = payload_data

    crc = stm32_crc32_words_le(bytes(frame[:CRC_INPUT_SIZE]))
    if force_bad_crc:
        crc ^= 0x00000001
    struct.pack_into("<I", frame, CRC_INPUT_SIZE, crc)
    return bytes(frame)


def parse_frame(buf: bytes) -> Optional[Dict[str, int]]:
    if len(buf) != FRAME_SIZE:
        return None
    return {
        "seq": buf[0],
        "type": buf[1],
        "payload_len": buf[2],
        "payload_type": buf[3],
        "crc": struct.unpack_from("<I", buf, CRC_INPUT_SIZE)[0],
    }


def type_name(frame_type: int) -> str:
    return {
        FRAME_TYPE_ERROR: "ERROR",
        FRAME_TYPE_START: "START",
        FRAME_TYPE_DATA: "DATA",
        FRAME_TYPE_ACK: "ACK",
        FRAME_TYPE_NACK: "NACK",
    }.get(frame_type, f"UNKNOWN({frame_type})")


def chunk_payload(payload: bytes, chunk_size: int) -> List[bytes]:
    if chunk_size <= 0:
        raise ValueError("chunk_size 必须大于 0")
    return [payload[i : i + chunk_size] for i in range(0, len(payload), chunk_size)]


def pick_custom_device(vid: int, pid: int) -> Optional[dict]:
    devs = hid.enumerate(vid, pid)
    if not devs:
        return None
    for d in devs:
        if d.get("interface_number") == 2:
            return d
    for d in devs:
        if d.get("usage_page") == 0xFF00:
            return d
    return devs[0]


def open_device(vid: int, pid: int, path: Optional[str]) -> hid.device:
    h = hid.device()
    if path:
        h.open_path(path.encode("utf-8"))
        return h
    info = pick_custom_device(vid, pid)
    if info is None:
        raise RuntimeError(f"未找到设备：VID=0x{vid:04X}, PID=0x{pid:04X}")
    h.open_path(info["path"])
    return h


def write_frame(h: hid.device, frame64: bytes) -> None:
    wrote = h.write(b"\x00" + frame64)
    if wrote <= 0:
        raise RuntimeError("HID 写入失败")


def send_ack_for_peer(h: hid.device, peer_seq: int) -> None:
    write_frame(h, build_frame(seq=peer_seq, frame_type=FRAME_TYPE_ACK))


def read_frame(h: hid.device, timeout_ms: int = 100) -> Optional[bytes]:
    raw = h.read(FRAME_SIZE, timeout_ms)
    if not raw:
        return None
    buf = bytes(raw)
    if len(buf) == FRAME_SIZE + 1 and buf[0] == 0x00:
        buf = buf[1:]
    if len(buf) != FRAME_SIZE:
        return None
    return buf


def flush_in(h: hid.device, ms: int = 200) -> None:
    end_t = time.time() + ms / 1000.0
    while time.time() < end_t:
        _ = read_frame(h, timeout_ms=20)


def wait_response_and_auto_ack(
    h: hid.device,
    timeout_sec: float,
    expect_types: Tuple[int, ...] = (FRAME_TYPE_ACK, FRAME_TYPE_NACK, FRAME_TYPE_ERROR),
) -> Tuple[Optional[Dict[str, int]], List[Dict[str, int]]]:
    t0 = time.time()
    end_t = t0 + timeout_sec
    rx_all: List[Dict[str, int]] = []

    while time.time() < end_t:
        buf = read_frame(h, timeout_ms=80)
        if buf is None:
            continue
        parsed = parse_frame(buf)
        if parsed is None:
            continue
        parsed["t_ms"] = int((time.time() - t0) * 1000)
        rx_all.append(parsed)
        print(
            f"[{parsed['t_ms']:>4} ms] 收到 type={type_name(parsed['type'])}, "
            f"seq={parsed['seq']}, payload_len={parsed['payload_len']}"
        )
        send_ack_for_peer(h, parsed["seq"])
        if parsed["type"] in expect_types:
            return parsed, rx_all
    return None, rx_all


def print_summary(received: List[Dict[str, int]], title: str) -> None:
    ack_cnt = sum(1 for x in received if x["type"] == FRAME_TYPE_ACK)
    nack_cnt = sum(1 for x in received if x["type"] == FRAME_TYPE_NACK)
    err_cnt = sum(1 for x in received if x["type"] == FRAME_TYPE_ERROR)
    print(f"\n{title} 统计：")
    print(f"  总接收帧数={len(received)}")
    print(f"  ACK={ack_cnt}, NACK={nack_cnt}, ERROR={err_cnt}")


def send_start_expect_ack(h: hid.device, total_len: int, observe_sec: float, received_all: List[Dict[str, int]]) -> bool:
    write_frame(h, build_frame(seq=0, frame_type=FRAME_TYPE_START, payload_data=struct.pack("<H", total_len)))
    resp, rxs = wait_response_and_auto_ack(h, observe_sec)
    received_all.extend(rxs)
    return resp is not None and resp["type"] == FRAME_TYPE_ACK


def send_data_expect_type(
    h: hid.device,
    seq: int,
    payload: bytes,
    observe_sec: float,
    expected: int,
    received_all: List[Dict[str, int]],
    bad_crc: bool = False,
) -> bool:
    write_frame(h, build_frame(seq=seq, frame_type=FRAME_TYPE_DATA, payload_data=payload, force_bad_crc=bad_crc))
    resp, rxs = wait_response_and_auto_ack(h, observe_sec)
    received_all.extend(rxs)
    return resp is not None and resp["type"] == expected


def run_normal_flow_test(h: hid.device, observe_sec: float, multi_data: bool) -> bool:
    name = "正常接收测试（多DATA）" if multi_data else "正常接收测试（单DATA）"
    print(f"\n================ {name} ================")

    payload = b"Hello-Keyboard-HID-MULTI-DATA-CHECK"
    chunks = chunk_payload(payload, 8) if multi_data else [payload]
    received_all: List[Dict[str, int]] = []

    if not send_start_expect_ack(h, len(payload), observe_sec, received_all):
        print_summary(received_all, name)
        print("  结论：START 阶段失败。")
        return False

    seq = 1
    for part in chunks:
        if not send_data_expect_type(h, seq, part, observe_sec, FRAME_TYPE_ACK, received_all):
            print_summary(received_all, name)
            print(f"  结论：DATA(seq={seq}) 阶段失败。")
            return False
        seq += 1

    print_summary(received_all, name)
    print("  结论：完整接收流程通过。")
    return True


def run_bad_crc_flow_test(h: hid.device, observe_sec: float, multi_data: bool) -> bool:
    name = "错误CRC测试（多DATA）" if multi_data else "错误CRC测试（单DATA）"
    print(f"\n================ {name} ================")

    payload = b"CRC-FAULT-RECOVER-MULTI-DATA-CHECK"
    chunks = chunk_payload(payload, 9) if multi_data else [payload]
    received_all: List[Dict[str, int]] = []

    if not send_start_expect_ack(h, len(payload), observe_sec, received_all):
        print_summary(received_all, name)
        print("  结论：START 阶段失败。")
        return False

    if not send_data_expect_type(h, 1, chunks[0], observe_sec, FRAME_TYPE_NACK, received_all, bad_crc=True):
        print_summary(received_all, name)
        print("  结论：错 CRC 未得到 NACK。")
        return False
    if not send_data_expect_type(h, 1, chunks[0], observe_sec, FRAME_TYPE_ACK, received_all):
        print_summary(received_all, name)
        print("  结论：错 CRC 后重发恢复失败。")
        return False

    seq = 2
    for part in chunks[1:]:
        if not send_data_expect_type(h, seq, part, observe_sec, FRAME_TYPE_ACK, received_all):
            print_summary(received_all, name)
            print(f"  结论：后续 DATA(seq={seq}) 失败。")
            return False
        seq += 1

    print_summary(received_all, name)
    print("  结论：容错恢复流程通过。")
    return True


def run_session_restart_test(h: hid.device, observe_sec: float, multi_data: bool) -> bool:
    name = "单一会话管理测试（多DATA）" if multi_data else "单一会话管理测试（单DATA）"
    print(f"\n================ {name} ================")

    old_payload = b"OLD-SESSION-PAYLOAD-WILL-BE-DROPPED"
    new_payload = b"NEW-SESSION-TAKES-OVER"
    old_chunks = chunk_payload(old_payload, 10) if multi_data else [old_payload]
    new_chunks = chunk_payload(new_payload, 7) if multi_data else [new_payload]
    received_all: List[Dict[str, int]] = []

    if not send_start_expect_ack(h, len(old_payload), observe_sec, received_all):
        print_summary(received_all, name)
        print("  结论：旧会话 START 失败。")
        return False

    cut = max(1, len(old_chunks) // 2)
    seq = 1
    for part in old_chunks[:cut]:
        if not send_data_expect_type(h, seq, part, observe_sec, FRAME_TYPE_ACK, received_all):
            print_summary(received_all, name)
            print(f"  结论：旧会话 DATA(seq={seq}) 失败。")
            return False
        seq += 1

    if not send_start_expect_ack(h, len(new_payload), observe_sec, received_all):
        print_summary(received_all, name)
        print("  结论：新会话 START 失败。")
        return False

    seq = 1
    for part in new_chunks:
        if not send_data_expect_type(h, seq, part, observe_sec, FRAME_TYPE_ACK, received_all):
            print_summary(received_all, name)
            print(f"  结论：新会话 DATA(seq={seq}) 失败。")
            return False
        seq += 1

    print_summary(received_all, name)
    print("  结论：单一会话切换管理正常。")
    return True


def run_interrupted_old_data_should_fail_test(h: hid.device, observe_sec: float) -> bool:
    name = "打断后旧数据续发失败测试"
    print(f"\n================ {name} ================")
    print("流程：旧START->旧半包DATA->新START->新DATA完成->再发旧剩余DATA，期望 ERROR。")

    old_payload = b"OLD-STREAM-SHOULD-BE-INVALID-AFTER-NEW-START"
    new_payload = b"NEW-VALID-SESSION"
    old_part1 = old_payload[:12]
    old_part2 = old_payload[12:]
    received_all: List[Dict[str, int]] = []

    if not send_start_expect_ack(h, len(old_payload), observe_sec, received_all):
        print_summary(received_all, name)
        print("  结论：旧会话 START 失败。")
        return False
    if not send_data_expect_type(h, 1, old_part1, observe_sec, FRAME_TYPE_ACK, received_all):
        print_summary(received_all, name)
        print("  结论：旧会话首片 DATA 失败。")
        return False

    if not send_start_expect_ack(h, len(new_payload), observe_sec, received_all):
        print_summary(received_all, name)
        print("  结论：新会话 START 失败。")
        return False
    if not send_data_expect_type(h, 1, new_payload, observe_sec, FRAME_TYPE_ACK, received_all):
        print_summary(received_all, name)
        print("  结论：新会话 DATA 失败。")
        return False

    if not send_data_expect_type(h, 2, old_part2, observe_sec, FRAME_TYPE_ERROR, received_all):
        print_summary(received_all, name)
        print("  结论：旧数据续发未被拒绝（未收到 ERROR）。")
        return False

    print_summary(received_all, name)
    print("  结论：通过（打断后旧数据续发被正确拒绝）。")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description="键盘固件 HID 通讯完整流程测试（7项）")
    parser.add_argument("--vid", type=lambda x: int(x, 0), default=0x1A86, help="USB VID（默认：0x1A86）")
    parser.add_argument("--pid", type=lambda x: int(x, 0), default=0x2004, help="USB PID（默认：0x2004）")
    parser.add_argument("--path", type=str, default=None, help="通过 HID 路径打开设备")
    parser.add_argument("--observe-sec", type=float, default=1.2, help="单步等待响应超时（秒）")
    args = parser.parse_args()

    try:
        h = open_device(args.vid, args.pid, args.path)
        h.set_nonblocking(0)
        results: List[Tuple[str, bool]] = []
        try:
            flush_in(h, ms=250)
            suites = [
                ("正常接收测试（单DATA）", lambda: run_normal_flow_test(h, args.observe_sec, multi_data=False)),
                ("正常接收测试（多DATA）", lambda: run_normal_flow_test(h, args.observe_sec, multi_data=True)),
                ("错误CRC测试（单DATA）", lambda: run_bad_crc_flow_test(h, args.observe_sec, multi_data=False)),
                ("错误CRC测试（多DATA）", lambda: run_bad_crc_flow_test(h, args.observe_sec, multi_data=True)),
                ("单一会话管理测试（单DATA）", lambda: run_session_restart_test(h, args.observe_sec, multi_data=False)),
                ("单一会话管理测试（多DATA）", lambda: run_session_restart_test(h, args.observe_sec, multi_data=True)),
                ("打断后旧数据续发失败测试", lambda: run_interrupted_old_data_should_fail_test(h, args.observe_sec)),
            ]

            for name, fn in suites:
                ok = fn()
                results.append((name, ok))
                flush_in(h, ms=120)
        finally:
            h.close()

        total = len(results)
        passed = sum(1 for _, ok in results if ok)
        failed = total - passed
        print("\n================ 最终测试总结 ================")
        for name, ok in results:
            print(f"  {name}: {'通过' if ok else '失败'}")
        print(f"  总计: {total} 项，通过: {passed} 项，失败: {failed} 项")
        return 0 if failed == 0 else 2

    except KeyboardInterrupt:
        return 130
    except Exception as e:
        print(f"错误：{e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
