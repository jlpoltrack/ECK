"""Open a pyOCD session that attaches to a running chip (generic cortex_m target, no halt or reset)."""


def attach(uid, **options):
    """Session on the probe with unique ID uid, already opened; options go to pyOCD unchanged."""
    from pyocd.core.helpers import ConnectHelper
    s = ConnectHelper.session_with_chosen_probe(unique_id=uid, target_override="cortex_m",
                                                connect_mode="attach", options=options)
    s.open()
    return s
