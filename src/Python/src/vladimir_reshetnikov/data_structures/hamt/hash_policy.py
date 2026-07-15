"""Hash and equivalence policies for the persistent hash collections."""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
from typing import Protocol, TypeVar, cast

K = TypeVar("K", contravariant=True)
T = TypeVar("T")


class HashPolicy(Protocol[K]):
    """Coherent process-local hash and equivalence operations."""

    def hash(self, key: K) -> int:
        """Return a hash code for *key*."""

    def equivalent(self, left: K, right: K) -> bool:
        """Return whether two keys belong to the same equivalence class."""


def _signed_int32(value: int) -> int:
    bits = value & 0xFFFF_FFFF
    return bits - 0x1_0000_0000 if bits >= 0x8000_0000 else bits


def _identity_hash(value: object) -> int:
    # ``id`` is stable for the lifetime retained by a collection. Mixing its low bits avoids
    # wasting the first CHAMP levels on the alignment shared by CPython objects.
    bits = id(value) & 0xFFFF_FFFF
    bits = ((bits ^ (bits >> 16)) * 0x7FEB_352D) & 0xFFFF_FFFF
    bits = ((bits ^ (bits >> 15)) * 0x846C_A68B) & 0xFFFF_FFFF
    return _signed_int32(bits ^ (bits >> 16))


def same_value(left: T, right: T) -> bool:
    """Use Python value equality with a reference-identical fast path."""

    if left is right:
        return True
    try:
        result = left == right
    except (TypeError, ValueError):
        return False
    return result if isinstance(result, bool) else False


def default_hash(value: object) -> int:
    """Return a stable process-local 32-bit hash.

    Hashable values use Python's native hash contract. Unhashable values receive an identity hash
    and are consequently equivalent only to the identical object under the default policy.
    """

    try:
        return _signed_int32(hash(value))
    except TypeError:
        return _identity_hash(value)


def _default_equivalent(left: object, right: object) -> bool:
    if left is right:
        return True
    try:
        hash(left)
        hash(right)
    except TypeError:
        return False
    return same_value(left, right)


@dataclass(frozen=True, slots=True)
class _FunctionHashPolicy(HashPolicy[K]):
    hash_function: Callable[[K], int]
    equivalent_function: Callable[[K, K], bool]

    def hash(self, key: K) -> int:
        return _signed_int32(self.hash_function(key))

    def equivalent(self, left: K, right: K) -> bool:
        return bool(self.equivalent_function(left, right))


_DEFAULT_POLICY: HashPolicy[object] = _FunctionHashPolicy(default_hash, _default_equivalent)


def default_hash_policy() -> HashPolicy[T]:
    """Return the shared Python-native hash-policy object."""

    return cast("HashPolicy[T]", _DEFAULT_POLICY)


def create_hash_policy(
    hash_function: Callable[[T], int],
    equivalent_function: Callable[[T, T], bool],
) -> HashPolicy[T]:
    """Create a policy from explicit coherent hash and equivalence functions."""

    return _FunctionHashPolicy(hash_function, equivalent_function)


__all__ = [
    "HashPolicy",
    "create_hash_policy",
    "default_hash",
    "default_hash_policy",
    "same_value",
]
