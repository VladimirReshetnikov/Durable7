"""Internal deterministic diagnostics for the lazy range-update sequence.

These counters are test instrumentation, not performance measurements.  A session is local to the
current thread and shadows an enclosing session exactly as the C# reference implementation does.
"""

from __future__ import annotations

from dataclasses import dataclass
from threading import local
from types import TracebackType


@dataclass(frozen=True, slots=True)
class _RangeUpdateOperationStatistics:
    node_visits: int = 0
    node_allocations: int = 0
    facade_allocations: int = 0
    rotations: int = 0
    pushes: int = 0
    subtree_applications: int = 0
    element_measure_callbacks: int = 0
    measure_combine_callbacks: int = 0
    identity_test_callbacks: int = 0
    tag_compose_callbacks: int = 0
    element_apply_callbacks: int = 0
    measure_apply_callbacks: int = 0

    @property
    def policy_callbacks(self) -> int:
        return (
            self.element_measure_callbacks
            + self.measure_combine_callbacks
            + self.identity_test_callbacks
            + self.tag_compose_callbacks
            + self.element_apply_callbacks
            + self.measure_apply_callbacks
        )


class _RangeUpdateDiagnosticSession:
    __slots__ = (
        "_disposed",
        "_previous",
        "element_apply_callbacks",
        "element_measure_callbacks",
        "facade_allocations",
        "identity_test_callbacks",
        "measure_apply_callbacks",
        "measure_combine_callbacks",
        "node_allocations",
        "node_visits",
        "pushes",
        "rotations",
        "subtree_applications",
        "tag_compose_callbacks",
    )

    def __init__(self, previous: _RangeUpdateDiagnosticSession | None) -> None:
        self._previous = previous
        self._disposed = False
        self.node_visits = 0
        self.node_allocations = 0
        self.facade_allocations = 0
        self.rotations = 0
        self.pushes = 0
        self.subtree_applications = 0
        self.element_measure_callbacks = 0
        self.measure_combine_callbacks = 0
        self.identity_test_callbacks = 0
        self.tag_compose_callbacks = 0
        self.element_apply_callbacks = 0
        self.measure_apply_callbacks = 0

    @property
    def snapshot(self) -> _RangeUpdateOperationStatistics:
        return _RangeUpdateOperationStatistics(
            node_visits=self.node_visits,
            node_allocations=self.node_allocations,
            facade_allocations=self.facade_allocations,
            rotations=self.rotations,
            pushes=self.pushes,
            subtree_applications=self.subtree_applications,
            element_measure_callbacks=self.element_measure_callbacks,
            measure_combine_callbacks=self.measure_combine_callbacks,
            identity_test_callbacks=self.identity_test_callbacks,
            tag_compose_callbacks=self.tag_compose_callbacks,
            element_apply_callbacks=self.element_apply_callbacks,
            measure_apply_callbacks=self.measure_apply_callbacks,
        )

    def __enter__(self) -> _RangeUpdateDiagnosticSession:
        return self

    def __exit__(
        self,
        exception_type: type[BaseException] | None,
        exception: BaseException | None,
        traceback: TracebackType | None,
    ) -> None:
        del exception_type, exception, traceback
        self.close()

    def close(self) -> None:
        if self._disposed:
            return
        if _current() is not self:
            raise RuntimeError("Range-update diagnostic sessions must close in stack order.")
        _state.current = self._previous
        self._disposed = True


_state = local()


def _current() -> _RangeUpdateDiagnosticSession | None:
    return getattr(_state, "current", None)


def _observe_range_update_operations() -> _RangeUpdateDiagnosticSession:
    session = _RangeUpdateDiagnosticSession(_current())
    _state.current = session
    return session


def _record(attribute: str) -> None:
    current = _current()
    if current is not None:
        setattr(current, attribute, getattr(current, attribute) + 1)


def _record_node_visit() -> None:
    _record("node_visits")


def _record_node_allocation() -> None:
    _record("node_allocations")


def _record_facade_allocation() -> None:
    _record("facade_allocations")


def _record_rotation() -> None:
    _record("rotations")


def _record_push() -> None:
    _record("pushes")


def _record_subtree_application() -> None:
    _record("subtree_applications")


def _record_element_measure_callback() -> None:
    _record("element_measure_callbacks")


def _record_measure_combine_callback() -> None:
    _record("measure_combine_callbacks")


def _record_identity_test_callback() -> None:
    _record("identity_test_callbacks")


def _record_tag_compose_callback() -> None:
    _record("tag_compose_callbacks")


def _record_element_apply_callback() -> None:
    _record("element_apply_callbacks")


def _record_measure_apply_callback() -> None:
    _record("measure_apply_callbacks")


__all__: list[str] = []
