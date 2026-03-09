"""Entry point for mruls."""

import argparse
from mruls.app import MrulsApp
from pathlib import Path


def main() -> None:
    """Run the mruls TUI."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", type=Path, default=None)
    args = parser.parse_args()

    app = MrulsApp(config_path=args.config)
    app.run()


if __name__ == "__main__":
    main()
