mod config;
mod app;
mod tui;

fn main() -> Result<(), std::io::Error> {
    if let Err(err) = tui::run() {
        println!("{:?}", err);
    }
    Ok(())
}
