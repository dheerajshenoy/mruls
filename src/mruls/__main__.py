"""Entry point for mruls."""

from mruls.app import MrulsApp


def main() -> None:
    """Run the mruls TUI."""
    app = MrulsApp()
    app.run()


if __name__ == "__main__":
    main()
