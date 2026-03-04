"""Main Textual application for mruls."""

from __future__ import annotations

import os
from enum import Enum
from typing import Optional

from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.containers import Grid, Horizontal, Vertical
from textual.screen import ModalScreen
from textual.widgets import Button, DataTable, Footer, Header, Label, Static

from mruls.slurm import (
    Job,
    cancel_job,
    get_job_output_paths,
    get_jobs,
    read_output_file,
)


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


class OutputViewerModal(ModalScreen[None]):
    """A modal screen for viewing job output."""

    BINDINGS = [
        Binding("escape", "dismiss", "Close"),
        Binding("o", "dismiss", "Close"),
        Binding("tab", "toggle_output", "Toggle stdout/stderr"),
    ]

    CSS = """
    OutputViewerModal {
        align: center middle;
    }

    OutputViewerModal > Vertical {
        width: 90%;
        height: 90%;
        border: thick $primary;
        background: $surface;
    }

    OutputViewerModal .output-header {
        dock: top;
        height: 3;
        padding: 1;
        background: $primary;
        text-style: bold;
    }

    OutputViewerModal .output-content {
        height: 1fr;
        padding: 1;
        overflow-y: auto;
    }

    OutputViewerModal .output-footer {
        dock: bottom;
        height: 1;
        padding: 0 1;
        background: $surface-darken-1;
    }
    """

    def __init__(
        self,
        job_id: str,
        job_name: str,
        stdout_content: str,
        stderr_content: str,
        show_stderr: bool = False,
    ) -> None:
        """Initialize the output viewer."""
        super().__init__()
        self.job_id = job_id
        self.job_name = job_name
        self.stdout_content = stdout_content
        self.stderr_content = stderr_content
        self.showing_stderr = show_stderr

    def compose(self) -> ComposeResult:
        """Compose the viewer UI."""
        output_type = "stderr" if self.showing_stderr else "stdout"
        content = self.stderr_content if self.showing_stderr else self.stdout_content
        yield Vertical(
            Static(
                f"Job {self.job_id} ({self.job_name}) - {output_type}",
                classes="output-header",
            ),
            Static(content, classes="output-content"),
            Static(
                "[Tab] Toggle stdout/stderr | [Esc/o] Close", classes="output-footer"
            ),
        )

    def action_toggle_output(self) -> None:
        """Toggle between stdout and stderr."""
        self.showing_stderr = not self.showing_stderr
        output_type = "stderr" if self.showing_stderr else "stdout"
        content = self.stderr_content if self.showing_stderr else self.stdout_content

        header = self.query_one(".output-header", Static)
        header.update(f"Job {self.job_id} ({self.job_name}) - {output_type}")

        content_widget = self.query_one(".output-content", Static)
        content_widget.update(content)


class OutputView(Enum):
    """Enum for output view states."""

    HIDDEN = "hidden"
    STDOUT = "stdout"
    STDERR = "stderr"


class OutputDisplayMode(Enum):
    """Enum for output display modes."""

    BOTTOM = "bottom"
    SIDE = "side"
    MODAL = "modal"


class OutputPanel(Static):
    """A panel widget for displaying job output."""

    CSS = """
    OutputPanel {
        height: 100%;
        border: solid $primary;
        background: $surface;
        overflow-y: auto;
        padding: 0 1;
    }

    OutputPanel .panel-title {
        text-style: bold;
        color: $primary;
        padding: 0;
    }

    OutputPanel .panel-content {
        padding: 0;
    }
    """

    def __init__(
        self, title: str = "", content: str = "", id: Optional[str] = None
    ) -> None:
        """Initialize the output panel."""
        super().__init__(id=id)
        self._title = title
        self._content = content

    def compose(self) -> ComposeResult:
        """Compose the panel UI."""
        yield Static(self._title, classes="panel-title")
        yield Static(self._content, classes="panel-content")

    def update_content(self, title: str, content: str) -> None:
        """Update the panel content."""
        self._title = title
        self._content = content
        try:
            self.query_one(".panel-title", Static).update(title)
            self.query_one(".panel-content", Static).update(content)
        except Exception:
            pass


class MrulsApp(App):
    """A minimal TUI for managing Slurm jobs."""

    TITLE = "mruls"
    CSS = """
    #main-container {
        height: 1fr;
    }

    #main-container.horizontal {
        layout: horizontal;
    }

    #main-container.vertical {
        layout: vertical;
    }

    #table-container {
        height: 1fr;
        width: 1fr;
    }

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

    #output-panel {
        display: none;
    }

    #output-panel.visible {
        display: block;
    }

    #output-panel.bottom {
        height: 15;
        width: 100%;
        dock: bottom;
    }

    #output-panel.side {
        width: 50%;
        height: 100%;
    }
    """

    BINDINGS = [
        Binding("q", "quit", "Quit"),
        Binding("r", "refresh", "Refresh"),
        Binding("c", "cancel_job", "Cancel"),
        Binding("a", "toggle_all_users", "All/Mine"),
        Binding("i", "show_info", "Info"),
        Binding("o", "toggle_output", "Output"),
        Binding("d", "cycle_display_mode", "Display"),
    ]

    def __init__(self, output_display_mode: str = "bottom") -> None:
        """Initialize the app.

        Args:
            output_display_mode: Display mode for output panel.
                                 One of 'bottom', 'side', or 'modal'.
        """
        super().__init__()
        self.show_all_users = False
        self.current_user = os.environ.get("USER", "")
        self.jobs: list[Job] = []
        # Output viewer state
        self.output_view = OutputView.HIDDEN
        self.output_display_mode = OutputDisplayMode(output_display_mode)
        self.current_stdout: str = ""
        self.current_stderr: str = ""
        self.current_output_job_id: Optional[str] = None
        self.current_output_job_name: str = ""

    def compose(self) -> ComposeResult:
        """Compose the UI."""
        yield Header()
        with Horizontal(id="main-container", classes="horizontal"):
            with Vertical(id="table-container"):
                yield DataTable()
            yield OutputPanel(id="output-panel")
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

    async def _load_job_output(self, job_id: str, job_name: str) -> None:
        """Load stdout and stderr for a job."""
        self.current_output_job_id = job_id
        self.current_output_job_name = job_name

        stdout_path, stderr_path = await get_job_output_paths(job_id)

        if stdout_path:
            success, content = await read_output_file(stdout_path, tail_lines=500)
            self.current_stdout = content if success else f"Error: {content}"
        else:
            self.current_stdout = "No stdout file found for this job."

        if stderr_path:
            success, content = await read_output_file(stderr_path, tail_lines=500)
            self.current_stderr = content if success else f"Error: {content}"
        else:
            self.current_stderr = "No stderr file found for this job."

    def _update_output_panel(self) -> None:
        """Update the output panel based on current view state."""
        panel = self.query_one("#output-panel", OutputPanel)

        if self.output_view == OutputView.HIDDEN:
            panel.remove_class("visible", "bottom", "side")
            return

        # Set visibility and position classes
        panel.add_class("visible")
        panel.remove_class("bottom", "side")

        if self.output_display_mode == OutputDisplayMode.BOTTOM:
            panel.add_class("bottom")
        elif self.output_display_mode == OutputDisplayMode.SIDE:
            panel.add_class("side")

        # Set content based on view
        if self.output_view == OutputView.STDOUT:
            title = f"[stdout] Job {self.current_output_job_id} ({self.current_output_job_name})"
            panel.update_content(title, self.current_stdout)
        elif self.output_view == OutputView.STDERR:
            title = f"[stderr] Job {self.current_output_job_id} ({self.current_output_job_name})"
            panel.update_content(title, self.current_stderr)

    async def action_toggle_output(self) -> None:
        """Toggle output view: hidden -> stdout -> stderr -> hidden."""
        table = self.query_one(DataTable)

        if table.cursor_row is None:
            return

        row_key = table.get_row_at(table.cursor_row)
        if not row_key:
            return

        job_id = str(row_key[0])
        job_name = str(row_key[1]) if len(row_key) > 1 else ""

        # If viewing a different job, start fresh with stdout
        if job_id != self.current_output_job_id:
            await self._load_job_output(job_id, job_name)
            self.output_view = OutputView.STDOUT
        else:
            # Cycle through views: HIDDEN -> STDOUT -> STDERR -> HIDDEN
            if self.output_view == OutputView.HIDDEN:
                self.output_view = OutputView.STDOUT
            elif self.output_view == OutputView.STDOUT:
                self.output_view = OutputView.STDERR
            else:
                self.output_view = OutputView.HIDDEN

        # Handle modal display mode separately
        if (
            self.output_display_mode == OutputDisplayMode.MODAL
            and self.output_view != OutputView.HIDDEN
        ):
            # For modal mode, show the modal and reset state
            show_stderr = self.output_view == OutputView.STDERR
            self.output_view = (
                OutputView.HIDDEN
            )  # Reset since modal handles its own state
            await self.push_screen(
                OutputViewerModal(
                    job_id,
                    job_name,
                    self.current_stdout,
                    self.current_stderr,
                    show_stderr,
                )
            )
        else:
            self._update_output_panel()

    def action_cycle_display_mode(self) -> None:
        """Cycle through display modes: bottom -> side -> modal -> bottom."""
        modes = list(OutputDisplayMode)
        current_idx = modes.index(self.output_display_mode)
        next_idx = (current_idx + 1) % len(modes)
        self.output_display_mode = modes[next_idx]

        self.notify(f"Output display mode: {self.output_display_mode.value}")

        # Update panel if currently visible
        if self.output_view != OutputView.HIDDEN:
            self._update_output_panel()
