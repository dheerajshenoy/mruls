use crate::app::{App, View};

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

fn render_job_table<'a>(output_rows: &[String], block: &Block<'a>) -> (Table<'a>, usize) {
    // Returns the table and the number of rows for pagination

    let header = output_rows
        .first()
        .map(|line| {
            let cells: Vec<ratatui::text::Text> = line
                .split_whitespace()
                .map(|s| ratatui::text::Text::raw(s.to_string()))
                .collect();
            Row::new(cells).style(Style::default().bg(ratatui::style::Color::DarkGray))
        })
        .unwrap();

    let rows = output_rows
        .iter()
        .skip(1) // Skip the header
        .map(|line| {
            let cells: Vec<Cell> = line
                .split_whitespace()
                .map(|s| Cell::from(s.to_string()))
                .collect();
            Row::new(cells)
        })
        .collect::<Vec<Row>>();

    // Split the first line to determine the number of columns
    let num_columns = output_rows
        .first()
        .map(|s| s.split_whitespace().count())
        .unwrap_or(0);
    let widths = vec![Constraint::Length(20); num_columns];

    let rows_count = rows.len();

    (
        Table::new(rows, widths)
            .header(header)
            .block(block.clone())
            .row_highlight_style(Style::default().bg(ratatui::style::Color::Blue)),
        rows_count,
    )
}

fn handle_key_event(app: &mut App, key: KeyCode) -> bool {
    match key {
        KeyCode::Char('q') => app.quit(),
        KeyCode::Char('j') => {
            app.next_row();
            false
        }
        KeyCode::Char('k') => {
            app.prev_row();
            false
        }

        KeyCode::Enter => {
            app.select_current_row();
            false
        }

        KeyCode::Esc => {
            app.go_back();
            false
        }

        _ => false,
    }
}

fn fetch_jobs(app: &mut App) {
    // let output = Command::new("squeue")
    //     .arg("-o")
    //     .arg("%i %u %t %M %D %R")
    //     .output()
    //     .expect("Failed to execute squeue command");
    let output = Command::new("ls")
        .arg("-lah")
        .output()
        .expect("Failed to execute command");

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
                    render_job_table(&app.output_rows, &Block::default().borders(Borders::ALL));
                app.num_rows = num_rows;
                terminal.draw(|f| {
                    let size = f.area();
                    f.render_stateful_widget(table, size, &mut app.table_state);
                })?;
            }

            View::JobDetails => {
                let (table, num_rows) =
                    render_job_table(&app.output_rows, &Block::default().borders(Borders::ALL));
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
