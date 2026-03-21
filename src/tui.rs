use crate::app::Dialog;
use crate::app::{App, View};

// Ratatui imports
use ratatui::crossterm::{
    cursor::{Hide, Show},
    event::{self, Event, KeyCode},
    execute,
    terminal::{EnterAlternateScreen, LeaveAlternateScreen, disable_raw_mode, enable_raw_mode},
};
use ratatui::layout::Constraint;
use ratatui::layout::{Alignment, Rect};
use ratatui::style::{Color, Modifier, Style};
use ratatui::widgets::{Block, Borders, Cell, Row, Table};
use ratatui::widgets::{Clear, Paragraph};
use ratatui::{Terminal, backend::CrosstermBackend};

// Standard library imports
use std::io;
use std::process::Command;
use std::time::{Duration, Instant};

const APP_NAME: &str = "MRULS";

pub fn run() -> Result<(), io::Error> {
    enable_raw_mode()?;
    let mut stdout = io::stdout();
    execute!(stdout, EnterAlternateScreen, Hide)?;
    let backend = CrosstermBackend::new(stdout);
    let mut terminal = Terminal::new(backend)?;
    terminal.clear()?;

    let original_hook = std::panic::take_hook();
    std::panic::set_hook(Box::new(move |panic_info| {
        disable_raw_mode().unwrap();
        execute!(io::stdout(), LeaveAlternateScreen, Show).unwrap();
        original_hook(panic_info);
    }));

    let res = event_loop(&mut terminal);

    disable_raw_mode()?;
    execute!(terminal.backend_mut(), Show, LeaveAlternateScreen)?;
    terminal.show_cursor()?;

    if let Err(ref e) = res {
        eprintln!("Error: {}", e);
    }

    res
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

fn render_dialog(f: &mut ratatui::Frame, title: &str, message: &str) {
    // center a small box on screen
    let area = centered_rect(40, 6, f.area());

    let block = Block::default()
        .title(title)
        .borders(Borders::ALL)
        .style(Style::default().bg(Color::DarkGray));

    let text = Paragraph::new(message)
        .block(block)
        .alignment(Alignment::Center);

    f.render_widget(Clear, area); // clear background beneath dialog
    f.render_widget(text, area);
}

fn centered_rect(width: u16, height: u16, area: Rect) -> Rect {
    let x = area.x + (area.width.saturating_sub(width)) / 2;
    let y = area.y + (area.height.saturating_sub(height)) / 2;
    Rect::new(x, y, width.min(area.width), height.min(area.height))
}

fn render_lines<'a>(
    output_rows: &[String],
    block: Block<'a>,
    selected: usize,
    show_line_numbers: bool,
) -> (Table<'a>, usize) {
    let rows = output_rows
        .iter()
        .enumerate()
        .map(|(i, line)| {
            let mut cells: Vec<Cell> = Vec::new();
            if show_line_numbers {
                let rel = (i as isize - selected as isize).abs();
                let line_num = if i == selected {
                    Cell::from(format!("{:>3} ", i + 1)).style(Style::default().fg(Color::Yellow))
                } else {
                    Cell::from(format!("{:>3} ", rel)).style(Style::default().fg(Color::DarkGray))
                };
                cells.push(line_num);
            }
            cells.push(Cell::from(line.clone()));
            Row::new(cells)
        })
        .collect::<Vec<Row>>();

    let rows_count = rows.len();

    let mut widths = Vec::new();
    if show_line_numbers {
        widths.push(Constraint::Length(5));
    }
    widths.push(Constraint::Fill(1));

    (
        Table::new(rows, widths)
            .block(block)
            .row_highlight_style(Style::default().bg(Color::Blue)),
        rows_count,
    )
}

fn render_job_table<'a>(
    output_rows: &[String],
    block: Block<'a>,
    selected: usize,
    show_line_numbers: bool,
) -> (Table<'a>, usize) {
    // determine column widths from all rows (not just header)
    let col_widths: Vec<usize> = output_rows.iter().fold(Vec::new(), |mut widths, line| {
        for (i, word) in line.split_whitespace().enumerate() {
            if i >= widths.len() {
                widths.push(word.len());
            } else {
                widths[i] = widths[i].max(word.len());
            }
        }
        widths
    });

    let header = output_rows.first().map(|line| {
        let mut cells: Vec<Cell> = Vec::new();
        if show_line_numbers {
            cells.push(Cell::from("   "));
        }
        cells.extend(line.split_whitespace().map(|s| {
            Cell::from(s.to_string()).style(
                Style::default()
                    .add_modifier(Modifier::BOLD)
                    .fg(Color::Yellow),
            )
        }));
        Row::new(cells).style(Style::default().bg(Color::DarkGray))
    });

    let rows = output_rows
        .iter()
        .skip(1)
        .enumerate()
        .map(|(i, line)| {
            let mut cells: Vec<Cell> = Vec::new();
            if show_line_numbers {
                let rel = (i as isize - selected as isize).abs();
                let line_num = if i == selected {
                    Cell::from(format!("{:>3} ", i + 1)).style(Style::default().fg(Color::Yellow))
                } else {
                    Cell::from(format!("{:>3} ", rel)).style(Style::default().fg(Color::DarkGray))
                };
                cells.push(line_num);
            }
            cells.extend(line.split_whitespace().map(|s| Cell::from(s.to_string())));
            Row::new(cells)
        })
        .collect::<Vec<Row>>();

    let rows_count = rows.len();

    // build constraints from measured column widths + 2 padding each
    let mut widths: Vec<Constraint> = Vec::new();
    if show_line_numbers {
        widths.push(Constraint::Length(5));
    }
    widths.extend(
        col_widths
            .iter()
            .map(|&w| Constraint::Length((w + 2) as u16)),
    );

    let (mut table, _) = (
        Table::new(rows, widths)
            .block(block)
            .column_spacing(1)
            .row_highlight_style(Style::default().bg(Color::Blue)),
        rows_count,
    );

    if let Some(h) = header {
        table = table.header(h);
    }

    (table, rows_count)
}

// ---------------------------------------------------------------------------
// Cancelling jobs
// ---------------------------------------------------------------------------

fn cancel_job(job_id: &str) -> Result<(), String> {
    let output = Command::new("scancel")
        .arg(job_id)
        .output()
        .map_err(|e| e.to_string())?;

    if output.status.success() {
        Ok(())
    } else {
        Err(String::from_utf8_lossy(&output.stderr).trim().to_string())
    }
}

// ---------------------------------------------------------------------------
// Fetching
// ---------------------------------------------------------------------------

fn fetch_jobs(app: &mut App) {
    let output = Command::new("squeue")
        .arg("-o")
        .arg("%i %u %t %M %D %R")
        .output()
        .expect("Failed to execute squeue command");

    if output.status.success() {
        let stdout = String::from_utf8_lossy(&output.stdout).to_string();
        app.output_rows = stdout.lines().map(|s| s.to_string()).collect();
    } else {
        eprintln!(
            "Error fetching jobs: {}",
            String::from_utf8_lossy(&output.stderr)
        );
    }
}

fn fetch_job_details(app: &mut App) {
    let job_id = match &app.selected_job_id {
        Some(id) => id.clone(),
        None => return,
    };

    let output = Command::new("scontrol")
        .arg("show")
        .arg("job")
        .arg(&job_id)
        .output()
        .expect("Failed to execute scontrol command");

    if output.status.success() {
        let stdout = String::from_utf8_lossy(&output.stdout).to_string();
        app.output_rows = stdout
            .split_whitespace()
            .filter(|s| s.contains('='))
            .map(|s| {
                let (k, v) = s.split_once('=').unwrap_or((s, ""));
                format!("{:<30} {}", k, v)
            })
            .collect();
    } else {
        app.output_rows = vec![format!(
            "Error: {}",
            String::from_utf8_lossy(&output.stderr).trim()
        )];
    }
}

// ---------------------------------------------------------------------------
// Event loop
// ---------------------------------------------------------------------------

fn event_loop(terminal: &mut Terminal<CrosstermBackend<std::io::Stdout>>) -> Result<(), io::Error> {
    let mut app = App::new();
    let tick_rate = Duration::from_secs(app.config.refresh_interval);
    let mut last_tick = Instant::now();

    fetch_jobs(&mut app);

    loop {
        let selected = app.table_state.selected().unwrap_or(0);
        let show_line_numbers = app.config.show_line_numbers;

        // build table outside draw closure
        let (table, num_rows) = match app.view {
            View::JobList => render_job_table(
                &app.output_rows,
                Block::default().title(APP_NAME).borders(Borders::ALL),
                selected,
                show_line_numbers,
            ),
            View::JobDetails => render_lines(
                &app.output_rows,
                Block::default().title("Job Details").borders(Borders::ALL),
                selected,
                show_line_numbers,
            ),
            View::JobOutput => render_lines(
                &app.output_rows,
                Block::default().title("Job Output").borders(Borders::ALL),
                selected,
                show_line_numbers,
            ),
        };
        app.num_rows = num_rows;

        let dialog = app.dialog.clone();

        // single draw call — main view + dialog overlay
        terminal.draw(|f| {
            f.render_stateful_widget(table, f.area(), &mut app.table_state);

            match &dialog {
                Dialog::ConfirmQuit => {
                    render_dialog(
                        f,
                        " Quit ",
                        "Are you sure you want to quit?\n\n[y] Yes    [n] No",
                    );
                }

                Dialog::ConfirmCancel { job_id } => {
                    let msg = format!("Cancel job {}?\n\n[y] Yes    [n] No", job_id);
                    render_dialog(f, " Cancel Job ", &msg);
                }

                _ => {}
            }
        })?;

        let timeout = tick_rate.saturating_sub(last_tick.elapsed());
        if event::poll(timeout)? {
            if let Event::Key(key) = event::read()? {
                if handle_key_event(&mut app, key.code) {
                    break;
                }
                match app.view {
                    View::JobList => fetch_jobs(&mut app),
                    View::JobDetails => fetch_job_details(&mut app),
                    View::JobOutput => {}
                }
            }
        }

        if last_tick.elapsed() >= tick_rate {
            match app.view {
                View::JobList => fetch_jobs(&mut app),
                View::JobDetails => fetch_job_details(&mut app),
                View::JobOutput => {}
            }
            last_tick = Instant::now();
        }
    }

    Ok(())
}

// ---------------------------------------------------------------------------
// Key handling
// ---------------------------------------------------------------------------

fn handle_key_event(app: &mut App, key: KeyCode) -> bool {
    match &app.dialog {
        Dialog::ConfirmQuit => match key {
            KeyCode::Char('y') => return app.quit(),
            KeyCode::Char('n') | KeyCode::Esc => {
                app.dialog = Dialog::None;
                return false;
            }
            _ => return false,
        },

        Dialog::ConfirmCancel { job_id } => {
            let job_id = job_id.clone(); // clone to release borrow on app
            match key {
                KeyCode::Char('y') | KeyCode::Enter => {
                    match cancel_job(&job_id) {
                        Ok(_) => {
                            app.dialog = Dialog::None;
                            fetch_jobs(app); // refresh list immediately
                        }
                        Err(e) => {
                            eprintln!("Failed to cancel job {}: {}", job_id, e);
                            app.dialog = Dialog::None;
                        }
                    }
                }
                KeyCode::Char('n') | KeyCode::Esc => {
                    app.dialog = Dialog::None;
                }
                _ => {}
            }
            return false;
        }

        _ => {}
    }
    // accumulate numeric prefix e.g. "10" in "10j"
    if let KeyCode::Char(c) = key {
        if c.is_ascii_digit() && app.pending_key.is_none() {
            app.count_buffer.push(c);
            return false;
        }
    }

    // handle pending key combos e.g. "gg"
    if let Some(pending) = app.pending_key.take() {
        match (pending, key) {
            ('g', KeyCode::Char('g')) => {
                app.first_row();
                app.count_buffer.clear();
            }
            _ => {
                app.count_buffer.clear();
            }
        }
        return false;
    }

    let count = app.get_count();

    match app.view {
        View::JobList => match key {
            KeyCode::Char('q') => {
                if app.config.confirm_on_quit {
                    app.dialog = Dialog::ConfirmQuit;
                    return false;
                } else {
                    return app.quit();
                }
            }

            KeyCode::Char('c') => {
                let index = app.table_state.selected().unwrap_or(0);
                let data_index = index + 1; // skip header
                if let Some(line) = app.output_rows.get(data_index) {
                    if let Some(job_id) = line.split_whitespace().next() {
                        app.dialog = Dialog::ConfirmCancel {
                            job_id: job_id.to_string(),
                        };
                    }
                }
            }

            KeyCode::Char('j') | KeyCode::Down => {
                for _ in 0..count {
                    app.next_row();
                }
            }
            KeyCode::Char('k') | KeyCode::Up => {
                for _ in 0..count {
                    app.prev_row();
                }
            }
            KeyCode::Char('g') => {
                app.pending_key = Some('g');
            }
            KeyCode::Char('G') => app.last_row(),
            KeyCode::Char('n') => app.toggle_line_numbers(),
            KeyCode::Char('r') => app.should_refresh = true,
            KeyCode::Enter => app.select_current_row(),
            KeyCode::Esc => app.go_back(),
            _ => {
                app.count_buffer.clear();
            }
        },

        View::JobDetails | View::JobOutput => match key {
            KeyCode::Char('q') => return app.quit(),
            KeyCode::Char('j') | KeyCode::Down => {
                for _ in 0..count {
                    app.next_row();
                }
            }
            KeyCode::Char('k') | KeyCode::Up => {
                for _ in 0..count {
                    app.prev_row();
                }
            }
            KeyCode::Char('g') => {
                app.pending_key = Some('g');
            }
            KeyCode::Char('G') => app.last_row(),
            KeyCode::Char('n') => app.toggle_line_numbers(),
            KeyCode::Esc => app.go_back(),
            _ => {
                app.count_buffer.clear();
            }
        },
    }

    return false;
}
