"""Custom widgets for mruls."""

from __future__ import annotations

from textual.widgets import DataTable


class JobTable(DataTable):
    """A DataTable specialized for displaying Slurm jobs."""

    DEFAULT_CSS = """
    JobTable {
        height: 1fr;
    }

    JobTable > .datatable--cursor {
        background: $accent;
    }
    """

    def __init__(self, **kwargs) -> None:
        """Initialize the job table."""
        super().__init__(**kwargs)
        self.cursor_type = "row"
        self.zebra_stripes = True

    def setup_columns(self) -> None:
        """Set up the standard job columns."""
        self.add_columns(
            "ID",
            "Name",
            "User",
            "Partition",
            "State",
            "Time",
            "Nodes",
            "Nodelist",
        )
