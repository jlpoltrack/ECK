"""Where the firmware repo and its nRF52 app are: the parent of tools/, or HHKB_FW_DIR."""
import os
from pathlib import Path

REPO = Path(os.environ.get("HHKB_FW_DIR", Path(__file__).resolve().parents[2]))
NRF_APP = REPO / "nrf52"
