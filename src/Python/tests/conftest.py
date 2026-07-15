"""Shared deterministic property-test settings."""

from hypothesis import HealthCheck, settings

settings.register_profile(
    "data-structures",
    deadline=1000,
    suppress_health_check=(HealthCheck.too_slow,),
)
settings.load_profile("data-structures")
