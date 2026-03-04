"""Entry point for slurp."""

from slurp.app import SlurpApp


def main() -> None:
    """Run the slurp TUI."""
    app = SlurpApp()
    app.run()


if __name__ == "__main__":
    main()
