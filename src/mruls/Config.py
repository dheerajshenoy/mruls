from __future__ import annotations
from dataclasses import dataclass
from pathlib import Path
import tomllib  # built-in from Python 3.11+, else use `tomli`

DEFAULT_CONFIG_PATH = Path.home() / ".config" / "mruls" / "config.toml"


@dataclass
class Config:
    refresh_interval: int = 5
    output_display_mode: str = "bottom"
    show_all_users: bool = False
    tail_lines: int = 500

    @classmethod
    def load(cls, path: Path = DEFAULT_CONFIG_PATH) -> Config:
        if not path.exists():
            return cls()  # return defaults if no config file

        with open(path, "rb") as f:
            data = tomllib.load(f)

        return cls(
            refresh_interval=data.get("refresh_interval", 30),
            output_display_mode=data.get("output_display_mode", "bottom"),
            show_all_users=data.get("show_all_users", False),
            tail_lines=data.get("tail_lines", 500),
        )
