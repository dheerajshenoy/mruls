from textual.screen import ModalScreen
from textual.app import ComposeResult
from textual.containers import Grid
from textual.widgets import Button, Label


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
