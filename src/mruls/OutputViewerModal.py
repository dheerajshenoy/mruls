from textual.screen import ModalScreen
from textual.binding import Binding
from textual.app import ComposeResult
from textual.containers import Vertical
from textual.widgets import Static

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

