"""Authenticated encryption boundary for Codex task detail replies."""

import base64
import binascii
import json
import re

from voice_input_crypto import TASK_DETAIL_DOMAIN, decrypt, encrypt


TASK_DETAIL_TYPE = "task_detail"
TASK_DETAIL_VERSION = 1
MAX_TASK_REVISION = 0x7FFFFFFF
MAX_UINT32 = 0xFFFFFFFF
_ROUTE_PATTERN = re.compile(r"^[0-9a-f]{16}$")
_NONCE_PATTERN = re.compile(r"^[0-9a-f]{16}$")


def _validate_uint32(value, name, allow_zero=True):
    minimum = 0 if allow_zero else 1
    if (
        isinstance(value, bool)
        or not isinstance(value, int)
        or not minimum <= value <= MAX_UINT32
    ):
        raise ValueError("{} is outside its valid range".format(name))


def _validate_metadata(
    key_epoch,
    exchange_nonce,
    sequence,
    route,
    revision,
    index,
    count,
):
    _validate_uint32(key_epoch, "key epoch", allow_zero=False)
    if (
        not isinstance(exchange_nonce, bytes)
        or len(exchange_nonce) != 8
        or not any(exchange_nonce)
    ):
        raise ValueError("exchange nonce must be 8 nonzero bytes")
    _validate_uint32(sequence, "sequence")
    if not isinstance(route, str) or _ROUTE_PATTERN.fullmatch(route) is None:
        raise ValueError("route must be 16 lowercase hex characters")
    if (
        isinstance(revision, bool)
        or not isinstance(revision, int)
        or not 0 <= revision <= MAX_TASK_REVISION
    ):
        raise ValueError("revision must be 0..2147483647")
    _validate_uint32(count, "count", allow_zero=False)
    _validate_uint32(index, "index")
    if index >= count:
        raise ValueError("index must be less than count")


def canonical_task_detail_aad(
    *,
    message_type,
    version,
    key_epoch,
    exchange_nonce,
    sequence,
    route,
    revision,
    index,
    count,
):
    if message_type != TASK_DETAIL_TYPE:
        raise ValueError("task detail type is invalid")
    if version != TASK_DETAIL_VERSION:
        raise ValueError("task detail version is invalid")
    _validate_metadata(
        key_epoch,
        exchange_nonce,
        sequence,
        route,
        revision,
        index,
        count,
    )
    return json.dumps(
        {
            "type": message_type,
            "version": version,
            "route": route,
            "revision": revision,
            "index": index,
            "count": count,
            "epoch": key_epoch,
            "nonce": exchange_nonce.hex(),
            "sequence": sequence,
        },
        ensure_ascii=True,
        separators=(",", ":"),
    ).encode("ascii")


def encrypt_task_detail(
    key,
    *,
    key_epoch,
    exchange_nonce,
    sequence,
    route,
    revision,
    index,
    count,
    text,
):
    if not isinstance(text, str):
        raise ValueError("task detail text must be a string")
    try:
        plaintext = text.encode("utf-8", errors="strict")
    except UnicodeEncodeError as error:
        raise ValueError("task detail text must be valid UTF-8") from error
    aad = canonical_task_detail_aad(
        message_type=TASK_DETAIL_TYPE,
        version=TASK_DETAIL_VERSION,
        key_epoch=key_epoch,
        exchange_nonce=exchange_nonce,
        sequence=sequence,
        route=route,
        revision=revision,
        index=index,
        count=count,
    )
    ciphertext = encrypt(
        key,
        TASK_DETAIL_DOMAIN,
        exchange_nonce,
        sequence,
        plaintext,
        aad,
    )
    return {
        "type": TASK_DETAIL_TYPE,
        "route": route,
        "revision": revision,
        "index": index,
        "count": count,
        "epoch": key_epoch,
        "nonce": exchange_nonce.hex(),
        "sequence": sequence,
        "ciphertext": base64.b64encode(ciphertext).decode("ascii"),
    }


def decrypt_task_detail(key, envelope):
    if not isinstance(envelope, dict) or envelope.get("type") != TASK_DETAIL_TYPE:
        raise ValueError("task detail envelope is invalid")
    try:
        route = envelope["route"]
        revision = envelope["revision"]
        index = envelope["index"]
        count = envelope["count"]
        key_epoch = envelope["epoch"]
        nonce_hex = envelope["nonce"]
        sequence = envelope["sequence"]
        ciphertext_text = envelope["ciphertext"]
    except KeyError as error:
        raise ValueError("task detail envelope is incomplete") from error
    if not isinstance(nonce_hex, str) or _NONCE_PATTERN.fullmatch(nonce_hex) is None:
        raise ValueError("task detail nonce is invalid")
    exchange_nonce = bytes.fromhex(nonce_hex)
    if not isinstance(ciphertext_text, str):
        raise ValueError("task detail ciphertext is invalid")
    try:
        ciphertext = base64.b64decode(ciphertext_text, validate=True)
    except (binascii.Error, ValueError) as error:
        raise ValueError("task detail ciphertext is invalid") from error
    aad = canonical_task_detail_aad(
        message_type=TASK_DETAIL_TYPE,
        version=TASK_DETAIL_VERSION,
        key_epoch=key_epoch,
        exchange_nonce=exchange_nonce,
        sequence=sequence,
        route=route,
        revision=revision,
        index=index,
        count=count,
    )
    plaintext = decrypt(
        key,
        TASK_DETAIL_DOMAIN,
        exchange_nonce,
        sequence,
        ciphertext,
        aad,
    )
    try:
        return plaintext.decode("utf-8", errors="strict")
    except UnicodeDecodeError as error:
        raise ValueError("task detail plaintext is not valid UTF-8") from error
