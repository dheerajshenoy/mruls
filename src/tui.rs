use crate::app::{App, View};
use crate::config::Config;

// Ratatui imports
use ratatui::crossterm::{
    cursor::{Hide, Show},
    event::{self, Event, KeyCode},
    execute,
    terminal::{EnterAlternateScreen, LeaveAlternateScreen, disable_raw_mode, enable_raw_mode},
};
use ratatui::layout::Constraint::{self};
use ratatui::style::Style;
use ratatui::widgets::{Block, Borders, Cell, Row, Table};
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

    // set panic hook to restore terminal before printing panic message
    let original_hook = std::panic::take_hook();
    std::panic::set_hook(Box::new(move |panic_info| {
        // restore terminal
        disable_raw_mode().unwrap();
        execute!(io::stdout(), LeaveAlternateScreen, Show,).unwrap();
        // print the panic message normally
        original_hook(panic_info);
    }));

    let res = event_loop(&mut terminal);

    disable_raw_mode()?;
    execute!(terminal.backend_mut(), Show, LeaveAlternateScreen)?;
    terminal.show_cursor()?;

    if let Err(ref e) = res {
        eprintln!("Error: {}", e);
    }

    return res;
}

fn render_lines<'a>(output_rows: &[String], block: Block<'a>, app: &mut App) -> (Table<'a>, usize) {
    let rows = output_rows
        .iter()
        .enumerate()
        .map(|(i, line)| {
            let mut cells: Vec<Cell> = Vec::new();
            if app.config.show_line_numbers {
                let rel = (i as isize - app.current_row() as isize).abs();
                let line_num = if i == app.current_row() {
                    Cell::from(format!("{:>3} ", i + 1))
                        .style(Style::default().fg(ratatui::style::Color::Yellow))
                } else {
                    Cell::from(format!("{:>3} ", rel))
                        .style(Style::default().fg(ratatui::style::Color::DarkGray))
                };
                cells.push(line_num);
            }
            cells.push(Cell::from(line.clone()));
            Row::new(cells)
        })
        .collect::<Vec<Row>>();

    let rows_count = rows.len();

    let mut widths = Vec::new();
    if app.config.show_line_numbers {
        widths.push(Constraint::Length(5));
    }
    widths.push(Constraint::Fill(1)); // single content column takes remaining space

    (
        Table::new(rows, widths)
            .block(block)
            .row_highlight_style(Style::default().bg(ratatui::style::Color::Blue)),
        rows_count,
    )
}

fn render_job_table<'a>(app: &mut App, block: &Block<'a>) -> (Table<'a>, usize) {
    let header = app.output_rows.first().map(|line| {
        let mut cells: Vec<Cell> = Vec::new();
        if app.config.show_line_numbers {
            cells.push(Cell::from("   "));
        }
        cells.push(
            Cell::from(line.clone())
                .style(Style::default().add_modifier(ratatui::style::Modifier::BOLD)),
        );
        Row::new(cells).style(Style::default().bg(ratatui::style::Color::DarkGray))
    });

    let data_rows = app.output_rows.iter().skip(1).cloned().collect::<Vec<_>>();
    let (mut table, count) = render_lines(&data_rows, block.clone(), app);

    if let Some(h) = header {
        table = table.header(h);
    }

    (table, count)
}

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
        // scontrol outputs key=value pairs separated by whitespace/newlines
        // normalize into one key=value per line for clean display
        app.output_rows = stdout
            .split_whitespace()
            .filter(|s| s.contains('='))
            .map(|s| s.to_string())
            .collect();
    } else {
        app.output_rows = vec![format!(
            "Error: {}",
            String::from_utf8_lossy(&output.stderr).trim()
        )];
    }
}

fn event_loop(terminal: &mut Terminal<CrosstermBackend<std::io::Stdout>>) -> Result<(), io::Error> {
    let mut app = App::new();
    let tick_rate = Duration::from_secs(app.config.refresh_interval);
    let mut last_tick = Instant::now();

    fetch_jobs(&mut app);

    loop {
        match app.view {
            View::JobList => {
                let (table, num_rows) =
                    render_job_table(&mut app, &Block::default().borders(Borders::ALL));
                app.num_rows = num_rows;
                terminal.draw(|f| {
                    let size = f.area();
                    f.render_stateful_widget(table, size, &mut app.table_state);
                })?;
            }

            View::JobDetails => {
                let (table, num_rows) =
                    render_job_table(&mut app, &Block::default().borders(Borders::ALL));
                app.num_rows = num_rows;
                terminal.draw(|f| {
                    let size = f.area();
                    f.render_stateful_widget(table, size, &mut app.table_state);
                })?;
            }

            View::JobOutput => {
                terminal.draw(|f| {
                    let size = f.area();
                    let block = Block::default().title("Job Output").borders(Borders::ALL);
                    f.render_widget(block, size);
                })?;
            }
        }

        let timeout = tick_rate.saturating_sub(last_tick.elapsed());
        if event::poll(timeout)? {
            if let Event::Key(key) = event::read()? {
                if handle_key_event(&mut app, key.code) {
                    break;
                }
            }

            match app.view {
                View::JobList => fetch_jobs(&mut app),
                View::JobDetails => fetch_job_details(&mut app),
                View::JobOutput => {}
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

fn handle_key_event(app: &mut App, key: KeyCode) -> bool {
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
                return false;
            }
            // unknown combo — discard
            _ => {
                app.count_buffer.clear();
                return false;
            }
        }
    }

    let count = app.get_count(); // consumes count_buffer, returns 1 if empty

    match app.view {
        View::JobList => match key {
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
            } // wait for second g
            KeyCode::Char('G') => app.last_row(),
            KeyCode::Enter => app.select_current_row(),
            KeyCode::Char('r') => app.should_refresh = true,
            KeyCode::Esc => app.count_buffer.clear(),
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
            KeyCode::Char('b') | KeyCode::Backspace => app.go_back(),
            KeyCode::Esc => app.count_buffer.clear(),
            _ => {
                app.count_buffer.clear();
            }
        },
    }
    false
}
