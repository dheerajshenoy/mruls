"""Main Textual application for slurp."""

from __future__ import annotations

import os

from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.containers import Grid
from textual.screen import ModalScreen
from textual.widgets import Button, DataTable, Footer, Header, Label

from slurp.slurm import Job, cancel_job, get_jobs


class ConfirmCancelDialog(ModalScreen[bool]):
    """A modal dialog to confirm job cancellation."""

    CSS = """
    ConfirmCancelDialog {
        align: center middle;
    }

    ConfirmCancelDialog > Grid {
        grid-size: 2;
        grid-gutter: 1 2;
        grid-rows: auto auto;
        padding: 1 2;
        width: 50;
        height: auto;
        border: thick $primary;
        background: $surface;
    }

    ConfirmCancelDialog > Grid > Label {
        column-span: 2;
        content-align: center middle;
        width: 100%;
        margin-bottom: 1;
    }

    ConfirmCancelDialog > Grid > Button {
        width: 100%;
    }
    """

    def __init__(self, job_id: str, job_name: str = "") -> None:
        """Initialize the dialog with job details."""
        super().__init__()
        self.job_id = job_id
        self.job_name = job_name

    def compose(self) -> ComposeResult:
        """Compose the dialog UI."""
        job_desc = f"{self.job_id}"
        if self.job_name:
            job_desc = f"{self.job_id} ({self.job_name})"
        yield Grid(
            Label(f"Cancel job {job_desc}?"),
            Button("Yes", variant="error", id="confirm"),
            Button("No", variant="primary", id="cancel"),
        )

    def on_button_pressed(self, event: Button.Pressed) -> None:
        """Handle button presses."""
        self.dismiss(event.button.id == "confirm")


class SlurpApp(App):
    """A minimal TUI for managing Slurm jobs."""

    TITLE = "slurp"
    CSS = """
    DataTable {
        height: 1fr;
    }

    DataTable > .datatable--cursor {
        background: $accent;
    }

    .job-running {
        color: $success;
    }

    .job-pending {
        color: $warning;
    }

    .job-failed {
        color: $error;
    }
    """

    BINDINGS = [
        Binding("q", "quit", "Quit"),
        Binding("r", "refresh", "Refresh"),
        Binding("c", "cancel_job", "Cancel"),
        Binding("a", "toggle_all_users", "All/Mine"),
        Binding("i", "show_info", "Info"),
    ]

    def __init__(self) -> None:
        """Initialize the app."""
        super().__init__()
        self.show_all_users = False
        self.current_user = os.environ.get("USER", "")
        self.jobs: list[Job] = []

    def compose(self) -> ComposeResult:
        """Compose the UI."""
        yield Header()
        yield DataTable()
        yield Footer()

    async def on_mount(self) -> None:
        """Set up the table when the app mounts."""
        table = self.query_one(DataTable)
        table.cursor_type = "row"
        table.zebra_stripes = True

        # Add columns
        table.add_columns(
            "ID",
            "Name",
            "User",
            "Partition",
            "State",
            "Time",
            "Nodes",
            "Nodelist",
        )

        # Load initial data
        await self.action_refresh()

    async def action_refresh(self) -> None:
        """Refresh the job list."""
        user = None if self.show_all_users else self.current_user
        self.jobs = await get_jobs(user)

        table = self.query_one(DataTable)
        table.clear()

        for job in self.jobs:
            # Color based on state
            state_style = ""
            if job.state == "RUNNING":
                state_style = "[green]"
            elif job.state == "PENDING":
                state_style = "[yellow]"
            elif job.state in ("FAILED", "CANCELLED", "TIMEOUT"):
                state_style = "[red]"

            state_display = f"{state_style}{job.state_symbol} {job.state}"

            table.add_row(
                job.job_id,
                job.name[:20],  # Truncate long names
                job.user,
                job.partition,
                state_display,
                job.time,
                job.nodes,
                job.nodelist[:30],  # Truncate long nodelists
                key=job.job_id,
            )

        # Update title to show filter status
        filter_status = "all users" if self.show_all_users else self.current_user
        self.sub_title = f"[{filter_status}] {len(self.jobs)} jobs"

    async def action_cancel_job(self) -> None:
        """Cancel the selected job after confirmation."""
        table = self.query_one(DataTable)

        if table.cursor_row is None:
            return

        row_key = table.get_row_at(table.cursor_row)
        if not row_key:
            return

        # Get job_id and job_name from the row
        job_id = str(row_key[0])
        job_name = str(row_key[1]) if len(row_key) > 1 else ""

        # Show confirmation dialog
        confirmed = await self.push_screen_wait(ConfirmCancelDialog(job_id, job_name))

        if confirmed:
            success, message = await cancel_job(job_id)
            self.notify(message, severity="information" if success else "error")

            if success:
                await self.action_refresh()

    def action_toggle_all_users(self) -> None:
        """Toggle between showing all users and current user only."""
        self.show_all_users = not self.show_all_users
        self.run_worker(self.action_refresh())

    async def action_show_info(self) -> None:
        """Show detailed job info (placeholder for future modal)."""
        table = self.query_one(DataTable)

        if table.cursor_row is None:
            return

        row_key = table.get_row_at(table.cursor_row)
        if not row_key:
            return

        job_id = str(row_key[0])
        self.notify(f"Job {job_id} selected (info view coming soon)")
