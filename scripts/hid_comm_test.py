#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import argparse
import re
import struct
import sys
import time
from pathlib import Path
from typing import Callable, Dict, List, Optional, Tuple

import hid


FRAME_SIZE = 64
CRC_INPUT_SIZE = 60
MAX_DATA_PAYLOAD = 56
POLY = 0x04C11DB7
FW_PROCESS_INTERVAL_MS = 2
_BASE_INTERVAL_MS = 5
_TIME_SCALE = FW_PROCESS_INTERVAL_MS / _BASE_INTERVAL_MS


def _scale_ms(value_ms: int, minimum_ms: int) -> int:
    return max(minimum_ms, int(round(value_ms * _TIME_SCALE)))


READ_TIMEOUT_MS = _scale_ms(80, 12)
FLUSH_READ_TIMEOUT_MS = _scale_ms(20, 6)
DEFAULT_OBSERVE_SEC = max(0.6, 1.2 * _TIME_SCALE)
DEFAULT_START_FLUSH_MS = _scale_ms(250, 80)
DEFAULT_CASE_FLUSH_MS = _scale_ms(120, 40)
DEFAULT_STEP_FLUSH_MS = _scale_ms(40, 12)
STRESS_QUERY_REQUEST_LEN = MAX_DATA_PAYLOAD + 24

_TYPE_DEFAULTS = {
    "FRAME_TYPE_ERROR": 0,
    "FRAME_TYPE_START": 1,
    "FRAME_TYPE_DATA": 2,
    "FRAME_TYPE_ACK": 3,
    "FRAME_TYPE_NACK": 4,
    "DATA_TYPE_GET_KEY": 0,
    "DATA_TYPE_SET_LAYER": 1,
    "DATA_TYPE_GET_LAYER_KEYMAP": 2,
    "DATA_TYPE_SET_LAYER_KEYMAP": 3,
    "DATA_TYPE_GET_ALL_LAYER_KEYMAP": 4,
}


def _parse_c_int_literal(literal: str) -> Optional[int]:
    s = literal.strip()
    if not s:
        return None

    # Strip common C integer suffixes: U/L/UL/ULL/LU...
    s = re.sub(r"[uUlL]+$", "", s)
    if not s:
        return None

    try:
        if s.startswith(("0b", "0B")):
            return int(s[2:], 2)
        if s.startswith(("0x", "0X")):
            return int(s[2:], 16)
        if len(s) > 1 and s.startswith("0") and re.fullmatch(r"0[0-7]+", s):
            return int(s, 8)
        return int(s, 10)
    except ValueError:
        return None


def load_type_values_from_header() -> Dict[str, int]:
    header_path = Path(__file__).resolve().parent.parent / "KeyBoard" / "inc" / "comm_controller.h"
    values = dict(_TYPE_DEFAULTS)
    try:
        text = header_path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return values

    for name, literal in re.findall(r"\b((?:FRAME|DATA)_TYPE_[A-Z0-9_]+)\s*=\s*([0-9A-Za-z]+)", text):
        if name in values:
            parsed = _parse_c_int_literal(literal)
            if parsed is not None:
                values[name] = parsed
    return values


_TYPE_VALUES = load_type_values_from_header()
FRAME_TYPE_ERROR = _TYPE_VALUES["FRAME_TYPE_ERROR"]
FRAME_TYPE_START = _TYPE_VALUES["FRAME_TYPE_START"]
FRAME_TYPE_DATA = _TYPE_VALUES["FRAME_TYPE_DATA"]
FRAME_TYPE_ACK = _TYPE_VALUES["FRAME_TYPE_ACK"]
FRAME_TYPE_NACK = _TYPE_VALUES["FRAME_TYPE_NACK"]
DATA_TYPE_SET_LAYER_KEYMAP = _TYPE_VALUES["DATA_TYPE_SET_LAYER_KEYMAP"]
DATA_TYPE_GET_KEY = _TYPE_VALUES["DATA_TYPE_GET_KEY"]
DATA_TYPE_GET_LAYER_KEYMAP = _TYPE_VALUES["DATA_TYPE_GET_LAYER_KEYMAP"]
DATA_TYPE_GET_ALL_LAYER_KEYMAP = _TYPE_VALUES["DATA_TYPE_GET_ALL_LAYER_KEYMAP"]
DATA_TYPE_SET_LAYER = _TYPE_VALUES["DATA_TYPE_SET_LAYER"]
TRANSPORT_TEST_PAYLOAD_TYPE = DATA_TYPE_SET_LAYER
GET_KEY_EXPECTED_REPLY_LEN = 3


def _read_define_u32(text: str, name: str) -> Optional[int]:
    m = re.search(rf"#define\s+{name}\s+\(?\s*(\d+)\s*\)?", text)
    if m:
        return int(m.group(1))
    return None


def load_keymap_reply_sizes() -> Tuple[int, int]:
    root = Path(__file__).resolve().parent.parent
    key_h = root / "KeyBoard" / "inc" / "key.h"
    keymap_h = root / "KeyBoard" / "inc" / "keymap.h"
    keymap_loader_h = root / "KeyBoard" / "inc" / "keymap_loader.h"

    hc165_count = 3
    key_total_keys = 24
    max_code = 3
    keymap_layers = 10

    try:
        key_text = key_h.read_text(encoding="utf-8", errors="ignore")
        keymap_text = keymap_h.read_text(encoding="utf-8", errors="ignore")
        keymap_loader_text = keymap_loader_h.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        layer_size_fallback = (1 + 2 * max_code + 1) * key_total_keys
        return layer_size_fallback, layer_size_fallback * keymap_layers

    hc_val = _read_define_u32(key_text, "HC165_COUNT")
    if hc_val is not None:
        hc165_count = hc_val

    key_total_val = _read_define_u32(key_text, "KEY_TOTAL_KEYS")
    if key_total_val is not None:
        key_total_keys = key_total_val
    else:
        m_total = re.search(r"#define\s+KEY_TOTAL_KEYS\s+\(\s*HC165_COUNT\s*\*\s*(\d+)\s*\)", key_text)
        if m_total:
            key_total_keys = hc165_count * int(m_total.group(1))

    max_code_val = _read_define_u32(keymap_text, "MAX_CODE")
    if max_code_val is not None:
        max_code = max_code_val

    layers_val = _read_define_u32(keymap_loader_text, "KEYMAP_LAYERS")
    if layers_val is not None:
        keymap_layers = layers_val
    else:
        m_layers = re.search(r"#define\s+KEYMAP_LAYERS\s+\(\(\s*(\d+)\s*-\s*(\d+)\s*\)\s*/", keymap_loader_text)
        if m_layers:
            flash_total = int(m_layers.group(1))
            flash_header = int(m_layers.group(2))
            layer_size_temp = (1 + 2 * max_code + 1) * key_total_keys
            if layer_size_temp > 0:
                keymap_layers = (flash_total - flash_header) // layer_size_temp

    key_mapping_size = 1 + 2 * max_code + 1
    layer_size = key_mapping_size * key_total_keys
    all_size = layer_size * keymap_layers
    return layer_size, all_size


LAYER_KEYMAP_REPLY_LEN, ALL_LAYER_KEYMAP_REPLY_LEN = load_keymap_reply_sizes()


def stm32_crc32_words_le(data60: bytes) -> int:
    if len(data60) != CRC_INPUT_SIZE:
        raise ValueError(f"CRC input length must be {CRC_INPUT_SIZE} bytes")

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
        raise ValueError("chunk_size \u5fc5\u987b\u5927\u4e8e 0")
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
        raise RuntimeError(f"\u672a\u627e\u5230\u8bbe\u5907\uff1aVID=0x{vid:04X}, PID=0x{pid:04X}")
    h.open_path(info["path"])
    return h


def write_frame(h: hid.device, frame64: bytes) -> None:
    wrote = h.write(b"\x00" + frame64)
    if wrote <= 0:
        raise RuntimeError("HID \u5199\u5165\u5931\u8d25")


def send_ack_for_peer(h: hid.device, peer_seq: int) -> None:
    write_frame(h, build_frame(seq=peer_seq, frame_type=FRAME_TYPE_ACK))


def read_frame(h: hid.device, timeout_ms: int = READ_TIMEOUT_MS) -> Optional[bytes]:
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
        buf = read_frame(h, timeout_ms=FLUSH_READ_TIMEOUT_MS)
        if buf is None:
            continue
        parsed = parse_frame(buf)
        if parsed is None:
            continue
        # Flushing still needs protocol ACK to avoid peer-side resend overflow.
        send_ack_for_peer(h, parsed["seq"])


def wait_response_and_auto_ack(
    h: hid.device,
    timeout_sec: float,
    expect_types: Tuple[int, ...] = (FRAME_TYPE_ACK, FRAME_TYPE_NACK, FRAME_TYPE_ERROR),
    verbose: bool = True,
) -> Tuple[Optional[Dict[str, int]], List[Dict[str, int]]]:
    t0 = time.time()
    end_t = t0 + timeout_sec
    rx_all: List[Dict[str, int]] = []

    while time.time() < end_t:
        buf = read_frame(h, timeout_ms=READ_TIMEOUT_MS)
        if buf is None:
            continue
        parsed = parse_frame(buf)
        if parsed is None:
            continue
        parsed["t_ms"] = int((time.time() - t0) * 1000)
        rx_all.append(parsed)
        if verbose:
            print(
                f"[{parsed['t_ms']:>4} ms] \u6536\u5230 type={type_name(parsed['type'])}, "
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
    print(f"\n{title} summary:")
    print(f"  total frames: {len(received)}")
    print(f"  ACK={ack_cnt}, NACK={nack_cnt}, ERROR={err_cnt}")


def fill_diag(diag: Optional[Dict[str, object]], reason: str, received: List[Dict[str, int]]) -> None:
    if diag is None:
        return
    diag["reason"] = reason
    diag["recent_frames"] = received[-10:]


def format_recent_frames(frames: List[Dict[str, int]]) -> str:
    return " | ".join(
        f"{type_name(x['type'])}(seq={x['seq']},len={x['payload_len']},ptype={x['payload_type']},t={x.get('t_ms', -1)}ms)"
        for x in frames
    )


def send_start_expect_ack(
    h: hid.device,
    total_len: int,
    observe_sec: float,
    received_all: List[Dict[str, int]],
    payload_type: int = TRANSPORT_TEST_PAYLOAD_TYPE,
) -> bool:
    write_frame(
        h,
        build_frame(
            seq=0,
            frame_type=FRAME_TYPE_START,
            payload_type=payload_type,
            payload_data=struct.pack("<H", total_len),
        ),
    )
    resp, rxs = wait_response_and_auto_ack(h, observe_sec, expect_types=(FRAME_TYPE_ACK,))
    received_all.extend(rxs)
    if resp is not None:
        # Drain possible delayed retransmissions so next stage won't consume stale ACK.
        flush_in(h, ms=DEFAULT_STEP_FLUSH_MS)
    return resp is not None


def send_data_expect_type(
    h: hid.device,
    seq: int,
    payload: bytes,
    observe_sec: float,
    expected: int,
    received_all: List[Dict[str, int]],
    payload_type: int = TRANSPORT_TEST_PAYLOAD_TYPE,
    bad_crc: bool = False,
) -> bool:
    write_frame(
        h,
        build_frame(
            seq=seq,
            frame_type=FRAME_TYPE_DATA,
            payload_type=payload_type,
            payload_data=payload,
            force_bad_crc=bad_crc,
        ),
    )
    resp, rxs = wait_response_and_auto_ack(h, observe_sec, expect_types=(expected,))
    received_all.extend(rxs)
    if resp is not None:
        # Keep link in sync between adjacent steps.
        flush_in(h, ms=DEFAULT_STEP_FLUSH_MS)
    return resp is not None


def run_normal_flow_test(h: hid.device, observe_sec: float, multi_data: bool) -> bool:
    name = "normal receive test (multi DATA)" if multi_data else "normal receive test (single DATA)"
    print(f"\n================ {name} ================")

    payload = b"Hello-Keyboard-HID-MULTI-DATA-CHECK"
    chunks = chunk_payload(payload, 8) if multi_data else [payload]
    received_all: List[Dict[str, int]] = []

    if not send_start_expect_ack(h, len(payload), observe_sec, received_all):
        print_summary(received_all, name)
        print("  result: START stage failed.")
        return False

    seq = 1
    for part in chunks:
        if not send_data_expect_type(h, seq, part, observe_sec, FRAME_TYPE_ACK, received_all):
            print_summary(received_all, name)
            print(f"  result: DATA(seq={seq}) stage failed.")
            return False
        seq += 1

    print_summary(received_all, name)
    print("  result: complete receive flow passed.")
    return True


def run_bad_crc_flow_test(h: hid.device, observe_sec: float, multi_data: bool) -> bool:
    name = "bad CRC test (multi DATA)" if multi_data else "bad CRC test (single DATA)"
    print(f"\n================ {name} ================")

    payload = b"CRC-FAULT-RECOVER-MULTI-DATA-CHECK"
    chunks = chunk_payload(payload, 9) if multi_data else [payload]
    received_all: List[Dict[str, int]] = []

    if not send_start_expect_ack(h, len(payload), observe_sec, received_all):
        print_summary(received_all, name)
        print("  result: START stage failed.")
        return False

    if not send_data_expect_type(h, 1, chunks[0], observe_sec, FRAME_TYPE_NACK, received_all, bad_crc=True):
        print_summary(received_all, name)
        print("  result: bad CRC did not trigger NACK.")
        return False
    if not send_data_expect_type(h, 1, chunks[0], observe_sec, FRAME_TYPE_ACK, received_all):
        print_summary(received_all, name)
        print("  result: resend recovery after bad CRC failed.")
        return False

    seq = 2
    for part in chunks[1:]:
        if not send_data_expect_type(h, seq, part, observe_sec, FRAME_TYPE_ACK, received_all):
            print_summary(received_all, name)
            print(f"  result: follow-up DATA(seq={seq}) failed.")
            return False
        seq += 1

    print_summary(received_all, name)
    print("  result: fault-tolerant recovery flow passed.")
    return True


def run_session_restart_test(h: hid.device, observe_sec: float, multi_data: bool) -> bool:
    name = "single-session management test (multi DATA)" if multi_data else "single-session management test (single DATA)"
    print(f"\n================ {name} ================")

    old_payload = b"OLD-SESSION-PAYLOAD-WILL-BE-DROPPED"
    new_payload = b"NEW-SESSION-TAKES-OVER"
    old_chunks = chunk_payload(old_payload, 10) if multi_data else [old_payload]
    new_chunks = chunk_payload(new_payload, 7) if multi_data else [new_payload]
    received_all: List[Dict[str, int]] = []

    if not send_start_expect_ack(h, len(old_payload), observe_sec, received_all):
        print_summary(received_all, name)
        print("  result: old session START failed.")
        return False

    cut = max(1, len(old_chunks) // 2)
    seq = 1
    for part in old_chunks[:cut]:
        if not send_data_expect_type(h, seq, part, observe_sec, FRAME_TYPE_ACK, received_all):
            print_summary(received_all, name)
            print(f"  result: old session DATA(seq={seq}) failed.")
            return False
        seq += 1

    if not send_start_expect_ack(h, len(new_payload), observe_sec, received_all):
        print_summary(received_all, name)
        print("  result: new session START failed.")
        return False

    seq = 1
    for part in new_chunks:
        if not send_data_expect_type(h, seq, part, observe_sec, FRAME_TYPE_ACK, received_all):
            print_summary(received_all, name)
            print(f"  result: new session DATA(seq={seq}) failed.")
            return False
        seq += 1

    print_summary(received_all, name)
    print("  result: single-session switching works.")
    return True


def run_interrupted_old_data_should_fail_test(h: hid.device, observe_sec: float) -> bool:
    name = "old-data-after-interrupt should fail"
    print(f"\n================ {name} ================")
    print("flow: old START -> old half DATA -> new START -> new DATA complete -> old remaining DATA, expect ERROR")

    old_payload = b"OLD-STREAM-SHOULD-BE-INVALID-AFTER-NEW-START"
    new_payload = b"NEW-VALID-SESSION"
    old_part1 = old_payload[:12]
    old_part2 = old_payload[12:]
    received_all: List[Dict[str, int]] = []

    if not send_start_expect_ack(h, len(old_payload), observe_sec, received_all):
        print_summary(received_all, name)
        print("  result: old session START failed.")
        return False
    if not send_data_expect_type(h, 1, old_part1, observe_sec, FRAME_TYPE_ACK, received_all):
        print_summary(received_all, name)
        print("  result: first old-session DATA failed.")
        return False

    if not send_start_expect_ack(h, len(new_payload), observe_sec, received_all):
        print_summary(received_all, name)
        print("  result: new session START failed.")
        return False
    if not send_data_expect_type(h, 1, new_payload, observe_sec, FRAME_TYPE_ACK, received_all):
        print_summary(received_all, name)
        print("  result: new session DATA failed.")
        return False

    if not send_data_expect_type(h, 2, old_part2, observe_sec, FRAME_TYPE_ERROR, received_all):
        print_summary(received_all, name)
        print("  result: old data continuation was not rejected (ERROR not received).")
        return False

    print_summary(received_all, name)
    print("  result: passed (old data continuation correctly rejected after interrupt).")
    return True


def run_get_key_test(
    h: hid.device,
    observe_sec: float,
    verbose: bool = True,
    diag: Optional[Dict[str, object]] = None,
) -> bool:
    name = "DATA_TYPE_GET_KEY test"
    if verbose:
        print(f"\n================ {name} ================")

    received_all: List[Dict[str, int]] = []
    got_control_ack = False
    got_reply_start = False
    got_reply_data = False
    reply_total_len: Optional[int] = None
    reply_payload = bytearray()
    seen_data_seq: set[int] = set()

    req = build_frame(
        seq=0,
        frame_type=FRAME_TYPE_START,
        payload_type=DATA_TYPE_GET_KEY,
        payload_data=struct.pack("<H", 0),
    )
    write_frame(h, req)

    t0 = time.time()
    end_t = t0 + max(2.0, observe_sec * 3.0)

    while time.time() < end_t:
        buf = read_frame(h, timeout_ms=READ_TIMEOUT_MS)
        if buf is None:
            continue

        parsed = parse_frame(buf)
        if parsed is None:
            continue

        parsed["t_ms"] = int((time.time() - t0) * 1000)
        received_all.append(parsed)
        if verbose:
            print(
                f"[{parsed['t_ms']:>4} ms] \u6536\u5230 type={type_name(parsed['type'])}, "
                f"seq={parsed['seq']}, payload_len={parsed['payload_len']}, payload_type={parsed['payload_type']}"
            )

        send_ack_for_peer(h, parsed["seq"])

        payload_len = parsed["payload_len"]
        payload_type = parsed["payload_type"]
        payload_data = buf[4 : 4 + payload_len]

        if parsed["type"] == FRAME_TYPE_ACK:
            got_control_ack = True
            continue

        if parsed["type"] == FRAME_TYPE_START:
            if payload_type != DATA_TYPE_GET_KEY:
                fill_diag(diag, f"reply START payload_type mismatch: {payload_type}", received_all)
                if verbose:
                    print(f"  FAIL: reply START payload_type != GET_KEY, actual={payload_type}")
                    print_summary(received_all, name)
                return False
            if payload_len < 2:
                fill_diag(diag, f"reply START payload_len invalid: {payload_len}", received_all)
                if verbose:
                    print(f"  FAIL: reply START payload_len invalid, actual={payload_len}")
                    print_summary(received_all, name)
                return False

            reply_total_len = struct.unpack_from("<H", payload_data, 0)[0]
            got_reply_start = True
            continue

        if parsed["type"] == FRAME_TYPE_DATA:
            if payload_type != DATA_TYPE_GET_KEY:
                fill_diag(diag, f"reply DATA payload_type mismatch: {payload_type}", received_all)
                if verbose:
                    print(f"  FAIL: reply DATA payload_type != GET_KEY, actual={payload_type}")
                    print_summary(received_all, name)
                return False

            if parsed["seq"] not in seen_data_seq:
                seen_data_seq.add(parsed["seq"])
                reply_payload.extend(payload_data)
            if reply_total_len is not None and len(reply_payload) >= reply_total_len:
                got_reply_data = True
                break

    ok = (
        got_control_ack
        and got_reply_start
        and got_reply_data
        and reply_total_len == GET_KEY_EXPECTED_REPLY_LEN
        and len(reply_payload) == GET_KEY_EXPECTED_REPLY_LEN
    )

    if verbose:
        print_summary(received_all, name)
        if ok:
            print(f"  PASS: GET_KEY reply {len(reply_payload)} bytes: {reply_payload.hex(' ')}")
        else:
            print(
                "  FAIL: "
                f"ack={got_control_ack}, start={got_reply_start}, data={got_reply_data}, "
                f"reply_total_len={reply_total_len}, data_len={len(reply_payload)}"
            )
    if not ok:
        fill_diag(
            diag,
            "ack/start/data/len check failed: "
            f"ack={got_control_ack}, start={got_reply_start}, data={got_reply_data}, "
            f"reply_total_len={reply_total_len}, data_len={len(reply_payload)}",
            received_all,
        )
    return ok


def run_zero_len_request_expect_reply(
    h: hid.device,
    observe_sec: float,
    payload_type_req: int,
    name: str,
    expected_reply_len: Optional[int] = None,
    verbose: bool = True,
    diag: Optional[Dict[str, object]] = None,
) -> bool:
    if verbose:
        print(f"\n================ {name} ================")

    received_all: List[Dict[str, int]] = []
    got_control_ack = False
    got_reply_start = False
    got_reply_data = False
    reply_total_len: Optional[int] = None
    reply_payload = bytearray()
    seen_data_seq: set[int] = set()

    req = build_frame(
        seq=0,
        frame_type=FRAME_TYPE_START,
        payload_type=payload_type_req,
        payload_data=struct.pack("<H", 0),
    )
    write_frame(h, req)

    t0 = time.time()
    end_t = t0 + max(2.0, observe_sec * 4.0)

    while time.time() < end_t:
        buf = read_frame(h, timeout_ms=READ_TIMEOUT_MS)
        if buf is None:
            continue

        parsed = parse_frame(buf)
        if parsed is None:
            continue

        parsed["t_ms"] = int((time.time() - t0) * 1000)
        received_all.append(parsed)
        if verbose:
            print(
                f"[{parsed['t_ms']:>4} ms] \u6536\u5230 type={type_name(parsed['type'])}, "
                f"seq={parsed['seq']}, payload_len={parsed['payload_len']}, payload_type={parsed['payload_type']}"
            )

        send_ack_for_peer(h, parsed["seq"])

        payload_len = parsed["payload_len"]
        payload_type = parsed["payload_type"]
        payload_data = buf[4 : 4 + payload_len]

        if parsed["type"] == FRAME_TYPE_ACK:
            got_control_ack = True
            continue

        if parsed["type"] == FRAME_TYPE_START:
            if payload_type != payload_type_req:
                fill_diag(diag, f"reply START payload_type mismatch: {payload_type}", received_all)
                if verbose:
                    print(f"  FAIL: reply START payload_type mismatch, actual={payload_type}")
                    print_summary(received_all, name)
                return False
            if payload_len < 2:
                fill_diag(diag, f"reply START payload_len invalid: {payload_len}", received_all)
                if verbose:
                    print(f"  FAIL: reply START payload_len invalid, actual={payload_len}")
                    print_summary(received_all, name)
                return False
            reply_total_len = struct.unpack_from("<H", payload_data, 0)[0]
            got_reply_start = True
            if reply_total_len == 0:
                got_reply_data = True
                break
            continue

        if parsed["type"] == FRAME_TYPE_DATA:
            if payload_type != payload_type_req:
                fill_diag(diag, f"reply DATA payload_type mismatch: {payload_type}", received_all)
                if verbose:
                    print(f"  FAIL: reply DATA payload_type mismatch, actual={payload_type}")
                    print_summary(received_all, name)
                return False

            if parsed["seq"] not in seen_data_seq:
                seen_data_seq.add(parsed["seq"])
                reply_payload.extend(payload_data)
            if reply_total_len is not None and len(reply_payload) >= reply_total_len:
                got_reply_data = True
                break

    ok = (
        got_control_ack
        and got_reply_start
        and got_reply_data
        and reply_total_len is not None
        and len(reply_payload) == reply_total_len
    )
    if expected_reply_len is not None:
        ok = ok and (reply_total_len == expected_reply_len)

    if verbose:
        print_summary(received_all, name)
        if ok:
            print(f"  PASS: reply_len={reply_total_len}, data_len={len(reply_payload)}")
        else:
            print(
                "  FAIL: "
                f"ack={got_control_ack}, start={got_reply_start}, data={got_reply_data}, "
                f"reply_total_len={reply_total_len}, data_len={len(reply_payload)}, "
                f"expected_reply_len={expected_reply_len}"
            )
    if not ok:
        fill_diag(
            diag,
            "ack/start/data/len check failed: "
            f"ack={got_control_ack}, start={got_reply_start}, data={got_reply_data}, "
            f"reply_total_len={reply_total_len}, data_len={len(reply_payload)}, "
            f"expected_reply_len={expected_reply_len}",
            received_all,
        )
    return ok


def run_zero_len_request_expect_ack_only(
    h: hid.device,
    observe_sec: float,
    payload_type_req: int,
    name: str,
    verbose: bool = True,
    diag: Optional[Dict[str, object]] = None,
) -> bool:
    if verbose:
        print(f"\n================ {name} ================")

    received_all: List[Dict[str, int]] = []
    got_control_ack = False
    saw_reply_frame = False

    req = build_frame(
        seq=0,
        frame_type=FRAME_TYPE_START,
        payload_type=payload_type_req,
        payload_data=struct.pack("<H", 0),
    )
    write_frame(h, req)

    t0 = time.time()
    end_t = t0 + max(1.5, observe_sec * 2.0)

    while time.time() < end_t:
        buf = read_frame(h, timeout_ms=READ_TIMEOUT_MS)
        if buf is None:
            continue

        parsed = parse_frame(buf)
        if parsed is None:
            continue

        parsed["t_ms"] = int((time.time() - t0) * 1000)
        received_all.append(parsed)
        if verbose:
            print(
                f"[{parsed['t_ms']:>4} ms] \u6536\u5230 type={type_name(parsed['type'])}, "
                f"seq={parsed['seq']}, payload_len={parsed['payload_len']}, payload_type={parsed['payload_type']}"
            )

        send_ack_for_peer(h, parsed["seq"])

        if parsed["type"] == FRAME_TYPE_ACK:
            got_control_ack = True
            # Fast path: pass condition reached, no need to wait until timeout.
            if not saw_reply_frame:
                break
            continue

        if parsed["type"] in (FRAME_TYPE_START, FRAME_TYPE_DATA):
            saw_reply_frame = True
            # Already violates ack-only contract, no need to keep waiting.
            if got_control_ack:
                break

    ok = got_control_ack and (not saw_reply_frame)
    if verbose:
        print_summary(received_all, name)
        if ok:
            print("  PASS: ACK received and no business reply frame.")
        else:
            print(f"  FAIL: ack={got_control_ack}, saw_reply_frame={saw_reply_frame}")
    if not ok:
        fill_diag(
            diag,
            f"ack-only check failed: ack={got_control_ack}, saw_reply_frame={saw_reply_frame}",
            received_all,
        )
    return ok


def _build_stress_query_request_payload(payload_type: int, length: int = STRESS_QUERY_REQUEST_LEN) -> bytes:
    if length <= 0:
        return b""
    # Deterministic pattern: easier to debug across host/firmware logs.
    return bytes(((payload_type + i) & 0xFF) for i in range(length))


def run_chunked_query_request_expect_reply(
    h: hid.device,
    observe_sec: float,
    payload_type_req: int,
    name: str,
    expected_reply_len: Optional[int] = None,
    request_payload: bytes = b"",
    verbose: bool = True,
    diag: Optional[Dict[str, object]] = None,
) -> bool:
    if verbose:
        print(f"\n================ {name} ================")

    received_all: List[Dict[str, int]] = []
    got_start_ack = False
    got_reply_start = False
    got_reply_data = False
    reply_total_len: Optional[int] = None
    reply_payload = bytearray()
    seen_data_seq: set[int] = set()

    req_len = len(request_payload)
    req_start = build_frame(
        seq=0,
        frame_type=FRAME_TYPE_START,
        payload_type=payload_type_req,
        payload_data=struct.pack("<H", req_len),
    )
    write_frame(h, req_start)

    # START for request session should be ACKed by firmware.
    resp, rxs = wait_response_and_auto_ack(h, observe_sec, expect_types=(FRAME_TYPE_ACK,), verbose=verbose)
    received_all.extend(rxs)
    if resp is None:
        if verbose:
            print_summary(received_all, name)
            print("  FAIL: request START was not ACKed.")
        fill_diag(diag, "request START was not ACKed", received_all)
        return False
    got_start_ack = True

    if req_len > 0:
        chunks = chunk_payload(request_payload, MAX_DATA_PAYLOAD)
        for idx, part in enumerate(chunks):
            seq = idx + 1
            is_last = idx == (len(chunks) - 1)
            write_frame(
                h,
                build_frame(
                    seq=seq,
                    frame_type=FRAME_TYPE_DATA,
                    payload_type=payload_type_req,
                    payload_data=part,
                ),
            )

            if not is_last:
                resp, rxs = wait_response_and_auto_ack(h, observe_sec, expect_types=(FRAME_TYPE_ACK,), verbose=verbose)
                received_all.extend(rxs)
                if resp is None:
                    if verbose:
                        print_summary(received_all, name)
                        print(f"  FAIL: request DATA(seq={seq}) did not receive expected ACK.")
                    fill_diag(diag, f"request DATA(seq={seq}) did not receive expected ACK", received_all)
                    return False
                continue

            # Last query DATA should transition directly to reply START.
            t0 = time.time()
            end_t = t0 + observe_sec
            got_last_data_start = False
            while time.time() < end_t:
                buf = read_frame(h, timeout_ms=READ_TIMEOUT_MS)
                if buf is None:
                    continue
                parsed = parse_frame(buf)
                if parsed is None:
                    continue
                parsed["t_ms"] = int((time.time() - t0) * 1000)
                received_all.append(parsed)
                if verbose:
                    print(
                        f"[{parsed['t_ms']:>4} ms] received type={type_name(parsed['type'])}, "
                        f"seq={parsed['seq']}, payload_len={parsed['payload_len']}, payload_type={parsed['payload_type']}"
                    )
                send_ack_for_peer(h, parsed["seq"])

                if parsed["type"] != FRAME_TYPE_START:
                    continue
                if parsed["payload_type"] != payload_type_req:
                    if verbose:
                        print_summary(received_all, name)
                        print(f"  FAIL: reply START payload_type mismatch, actual={parsed['payload_type']}")
                    fill_diag(diag, f"reply START payload_type mismatch: {parsed['payload_type']}", received_all)
                    return False
                if parsed["payload_len"] < 2:
                    if verbose:
                        print_summary(received_all, name)
                        print(f"  FAIL: reply START payload_len invalid, actual={parsed['payload_len']}")
                    fill_diag(diag, f"reply START payload_len invalid: {parsed['payload_len']}", received_all)
                    return False
                reply_total_len = struct.unpack_from("<H", buf, 4)[0]
                got_reply_start = True
                got_last_data_start = True
                break

            if not got_last_data_start:
                if verbose:
                    print_summary(received_all, name)
                    print(f"  FAIL: request DATA(seq={seq}) did not receive expected START.")
                fill_diag(diag, f"request DATA(seq={seq}) did not receive expected START", received_all)
                return False
    else:
        # Keep compatibility for zero-length request use.
        t0 = time.time()
        end_t = t0 + observe_sec
        got_zero_req_start = False
        while time.time() < end_t:
            buf = read_frame(h, timeout_ms=READ_TIMEOUT_MS)
            if buf is None:
                continue
            parsed = parse_frame(buf)
            if parsed is None:
                continue
            parsed["t_ms"] = int((time.time() - t0) * 1000)
            received_all.append(parsed)
            if verbose:
                print(
                    f"[{parsed['t_ms']:>4} ms] received type={type_name(parsed['type'])}, "
                    f"seq={parsed['seq']}, payload_len={parsed['payload_len']}, payload_type={parsed['payload_type']}"
                )
            send_ack_for_peer(h, parsed["seq"])

            if parsed["type"] != FRAME_TYPE_START:
                continue
            if parsed["payload_type"] != payload_type_req:
                if verbose:
                    print_summary(received_all, name)
                    print(f"  FAIL: reply START payload_type mismatch, actual={parsed['payload_type']}")
                fill_diag(diag, f"reply START payload_type mismatch: {parsed['payload_type']}", received_all)
                return False
            if parsed["payload_len"] < 2:
                if verbose:
                    print_summary(received_all, name)
                    print(f"  FAIL: reply START payload_len invalid, actual={parsed['payload_len']}")
                fill_diag(diag, f"reply START payload_len invalid: {parsed['payload_len']}", received_all)
                return False
            reply_total_len = struct.unpack_from("<H", buf, 4)[0]
            got_reply_start = True
            got_zero_req_start = True
            break

        if not got_zero_req_start:
            if verbose:
                print_summary(received_all, name)
                print("  FAIL: did not receive reply START after zero-length request.")
            fill_diag(diag, "did not receive reply START after zero-length request", received_all)
            return False

    if reply_total_len is None:
        if verbose:
            print_summary(received_all, name)
            print("  FAIL: reply_total_len is unknown.")
        fill_diag(diag, "reply_total_len is unknown", received_all)
        return False

    if reply_total_len == 0:
        got_reply_data = True
    else:
        t0 = time.time()
        end_t = t0 + max(2.0, observe_sec * 4.0)
        while time.time() < end_t:
            buf = read_frame(h, timeout_ms=READ_TIMEOUT_MS)
            if buf is None:
                continue
            parsed = parse_frame(buf)
            if parsed is None:
                continue
            parsed["t_ms"] = int((time.time() - t0) * 1000)
            received_all.append(parsed)
            if verbose:
                print(
                    f"[{parsed['t_ms']:>4} ms] received type={type_name(parsed['type'])}, "
                    f"seq={parsed['seq']}, payload_len={parsed['payload_len']}, payload_type={parsed['payload_type']}"
                )
            send_ack_for_peer(h, parsed["seq"])

            if parsed["type"] != FRAME_TYPE_DATA:
                continue
            if parsed["payload_type"] != payload_type_req:
                if verbose:
                    print_summary(received_all, name)
                    print(f"  FAIL: reply DATA payload_type mismatch, actual={parsed['payload_type']}")
                fill_diag(diag, f"reply DATA payload_type mismatch: {parsed['payload_type']}", received_all)
                return False
            if parsed["seq"] in seen_data_seq:
                continue
            seen_data_seq.add(parsed["seq"])
            payload_len = parsed["payload_len"]
            reply_payload.extend(buf[4 : 4 + payload_len])
            if len(reply_payload) >= reply_total_len:
                got_reply_data = True
                break

    ok = got_start_ack and got_reply_start and got_reply_data and len(reply_payload) == reply_total_len
    if expected_reply_len is not None:
        ok = ok and (reply_total_len == expected_reply_len)

    if verbose:
        print_summary(received_all, name)
        if ok:
            print(
                "  PASS: "
                f"request_len={req_len}, reply_len={reply_total_len}, data_len={len(reply_payload)}"
            )
        else:
            print(
                "  FAIL: "
                f"start_ack={got_start_ack}, reply_start={got_reply_start}, reply_data={got_reply_data}, "
                f"request_len={req_len}, reply_total_len={reply_total_len}, data_len={len(reply_payload)}, "
                f"expected_reply_len={expected_reply_len}"
            )
    if not ok:
        fill_diag(
            diag,
            "chunked query flow check failed: "
            f"start_ack={got_start_ack}, reply_start={got_reply_start}, reply_data={got_reply_data}, "
            f"request_len={req_len}, reply_total_len={reply_total_len}, data_len={len(reply_payload)}, "
            f"expected_reply_len={expected_reply_len}",
            received_all,
        )
    return ok


def run_continuous_stress_test(
    h: hid.device,
    observe_sec: float,
    rounds: int,
    stop_on_fail: bool = False,
    progress_every: int = 20,
) -> bool:
    name = f"continuous stress test ({rounds} rounds)"
    print(f"\n================ {name} ================")

    if rounds <= 0:
        print("  SKIP: rounds <= 0")
        return True

    get_key_req_payload = _build_stress_query_request_payload(DATA_TYPE_GET_KEY)
    get_layer_req_payload = _build_stress_query_request_payload(DATA_TYPE_GET_LAYER_KEYMAP)
    get_all_req_payload = _build_stress_query_request_payload(DATA_TYPE_GET_ALL_LAYER_KEYMAP)

    steps: List[Tuple[str, Callable[[Dict[str, object]], bool]]] = [
        (
            "GET_KEY",
            lambda d: run_chunked_query_request_expect_reply(
                h,
                observe_sec,
                DATA_TYPE_GET_KEY,
                "DATA_TYPE_GET_KEY chunked request test",
                expected_reply_len=GET_KEY_EXPECTED_REPLY_LEN,
                request_payload=get_key_req_payload,
                verbose=False,
                diag=d,
            ),
        ),
        (
            "GET_LAYER_KEYMAP",
            lambda d: run_chunked_query_request_expect_reply(
                h,
                observe_sec,
                DATA_TYPE_GET_LAYER_KEYMAP,
                "DATA_TYPE_GET_LAYER_KEYMAP chunked request test",
                expected_reply_len=LAYER_KEYMAP_REPLY_LEN,
                request_payload=get_layer_req_payload,
                verbose=False,
                diag=d,
            ),
        ),
        (
            "GET_ALL_LAYER_KEYMAP",
            lambda d: run_chunked_query_request_expect_reply(
                h,
                observe_sec,
                DATA_TYPE_GET_ALL_LAYER_KEYMAP,
                "DATA_TYPE_GET_ALL_LAYER_KEYMAP chunked request test",
                expected_reply_len=ALL_LAYER_KEYMAP_REPLY_LEN,
                request_payload=get_all_req_payload,
                verbose=False,
                diag=d,
            ),
        ),
        (
            "SET_LAYER",
            lambda d: run_zero_len_request_expect_ack_only(
                h,
                observe_sec,
                DATA_TYPE_SET_LAYER,
                "DATA_TYPE_SET_LAYER test",
                verbose=False,
                diag=d,
            ),
        ),
        (
            "SET_LAYER_KEYMAP",
            lambda d: run_zero_len_request_expect_ack_only(
                h,
                observe_sec,
                DATA_TYPE_SET_LAYER_KEYMAP,
                "DATA_TYPE_SET_LAYER_KEYMAP test",
                verbose=False,
                diag=d,
            ),
        ),
    ]

    failures: List[Tuple[int, str, str]] = []
    t0 = time.time()
    progress_every = max(1, progress_every)
    ran = 0

    for idx in range(rounds):
        ran = idx + 1
        step_name, fn = steps[idx % len(steps)]
        diag: Dict[str, object] = {}
        ok = fn(diag)
        if not ok:
            reason = str(diag.get("reason", "unknown"))
            failures.append((idx + 1, step_name, reason))
            print(f"  FAIL round={idx + 1}, case={step_name}, reason={reason}")
            recent = diag.get("recent_frames")
            if isinstance(recent, list) and recent:
                print(f"    recent rx: {format_recent_frames(recent)}")
            if stop_on_fail:
                print("  stop_on_fail enabled, test terminated early.")
                break
        elif (idx + 1) % progress_every == 0:
            print(f"  progress: {idx + 1}/{rounds} rounds, failures={len(failures)}")

    elapsed = time.time() - t0
    passed = ran - len(failures)

    print(f"\n{name} summary:")
    print(f"  rounds ran: {ran}/{rounds}")
    print(f"  passed: {passed}, failed: {len(failures)}")
    print(f"  elapsed: {elapsed:.2f}s")
    if failures:
        preview = ", ".join(f"#{r}:{case}" for r, case, _ in failures[:8])
        if len(failures) > 8:
            preview += f", ... (+{len(failures) - 8} more)"
        print(f"  failed rounds: {preview}")
        return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description="Keyboard firmware HID communication flow tests")
    parser.add_argument("--vid", type=lambda x: int(x, 0), default=0x1A86, help="USB VID (default: 0x1A86)")
    parser.add_argument("--pid", type=lambda x: int(x, 0), default=0x2004, help="USB PID (default: 0x2004)")
    parser.add_argument("--path", type=str, default=None, help="Open device via HID path")
    parser.add_argument(
        "--observe-sec",
        type=float,
        default=DEFAULT_OBSERVE_SEC,
        help=f"Per-step response timeout in seconds (default: {DEFAULT_OBSERVE_SEC:.2f})",
    )
    parser.add_argument(
        "--stress-rounds",
        type=int,
        default=0,
        help="Enable continuous stress test with N rounds (default: 0, disabled)",
    )
    parser.add_argument(
        "--stress-stop-on-fail",
        action="store_true",
        help="Stop stress test immediately on first failure",
    )
    parser.add_argument(
        "--stress-progress-every",
        type=int,
        default=20,
        help="Print stress progress every N rounds (default: 20)",
    )
    args = parser.parse_args()

    try:
        h = open_device(args.vid, args.pid, args.path)
        h.set_nonblocking(0)
        results: List[Tuple[str, bool]] = []
        try:
            flush_in(h, ms=DEFAULT_START_FLUSH_MS)
            if args.stress_rounds > 0:
                suites = [
                    (
                        f"continuous stress test ({args.stress_rounds} rounds)",
                        lambda: run_continuous_stress_test(
                            h,
                            args.observe_sec,
                            rounds=args.stress_rounds,
                            stop_on_fail=args.stress_stop_on_fail,
                            progress_every=args.stress_progress_every,
                        ),
                    )
                ]
            else:
                suites = [
                    ("normal receive test (single DATA)", lambda: run_normal_flow_test(h, args.observe_sec, multi_data=False)),
                    ("normal receive test (multi DATA)", lambda: run_normal_flow_test(h, args.observe_sec, multi_data=True)),
                    ("bad CRC test (single DATA)", lambda: run_bad_crc_flow_test(h, args.observe_sec, multi_data=False)),
                    ("bad CRC test (multi DATA)", lambda: run_bad_crc_flow_test(h, args.observe_sec, multi_data=True)),
                    ("single-session test (single DATA)", lambda: run_session_restart_test(h, args.observe_sec, multi_data=False)),
                    ("single-session test (multi DATA)", lambda: run_session_restart_test(h, args.observe_sec, multi_data=True)),
                    ("interrupt old-data continuation should fail", lambda: run_interrupted_old_data_should_fail_test(h, args.observe_sec)),
                    ("DATA_TYPE_GET_KEY test", lambda: run_get_key_test(h, args.observe_sec)),
                    (
                        "DATA_TYPE_GET_LAYER_KEYMAP test",
                        lambda: run_zero_len_request_expect_reply(
                            h,
                            args.observe_sec,
                            DATA_TYPE_GET_LAYER_KEYMAP,
                            "DATA_TYPE_GET_LAYER_KEYMAP test",
                            expected_reply_len=LAYER_KEYMAP_REPLY_LEN,
                        ),
                    ),
                    (
                        "DATA_TYPE_GET_ALL_LAYER_KEYMAP test",
                        lambda: run_zero_len_request_expect_reply(
                            h,
                            args.observe_sec,
                            DATA_TYPE_GET_ALL_LAYER_KEYMAP,
                            "DATA_TYPE_GET_ALL_LAYER_KEYMAP test",
                            expected_reply_len=ALL_LAYER_KEYMAP_REPLY_LEN,
                        ),
                    ),
                    (
                        "DATA_TYPE_SET_LAYER test",
                        lambda: run_zero_len_request_expect_ack_only(
                            h,
                            args.observe_sec,
                            DATA_TYPE_SET_LAYER,
                            "DATA_TYPE_SET_LAYER test",
                        ),
                    ),
                    (
                        "DATA_TYPE_SET_LAYER_KEYMAP test",
                        lambda: run_zero_len_request_expect_ack_only(
                            h,
                            args.observe_sec,
                            DATA_TYPE_SET_LAYER_KEYMAP,
                            "DATA_TYPE_SET_LAYER_KEYMAP test",
                        ),
                    ),
                ]

            for name, fn in suites:
                ok = fn()
                results.append((name, ok))
                flush_in(h, ms=DEFAULT_CASE_FLUSH_MS)
        finally:
            h.close()

        total = len(results)
        passed = sum(1 for _, ok in results if ok)
        failed = total - passed
        print("\n================ Final Test Summary ================")
        for name, ok in results:
            print(f"  {name}: {'PASS' if ok else 'FAIL'}")
        print(f"  Total: {total}, Passed: {passed}, Failed: {failed}")
        return 0 if failed == 0 else 2

    except KeyboardInterrupt:
        return 130
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
