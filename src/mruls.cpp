#include "mruls.hpp"

static constexpr const int FOOTER_HEIGHT = 4;

static constexpr const char *SQUEUE_CMD
    = "squeue -o '%.8i %.9P %.8j %.8u %.2t %.10M %.6D %R'";

static std::vector<std::string>
splitLine(const std::string &line)
{
    std::vector<std::string> tokens;
    std::istringstream iss(line);
    std::string tok;
    while (iss >> tok)
        tokens.push_back(tok);
    return tokens;
}

static std::vector<std::vector<std::string>>
parseOutput(const std::string &output)
{
    std::vector<std::vector<std::string>> rows;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line))
    {
        if (line.empty())
            continue;
        rows.push_back(splitLine(line));
    }
    return rows;
}

static std::string
exec(const char *cmd)
{
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe)
        return "Error running command";
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
        result += buffer.data();
    return result;
}

// Parse "KEY=VALUE" tokens from scontrol output — called once per fetch
std::vector<std::pair<std::string, std::string>>
mruls::parseJobDetail(const std::string &raw)
{
    std::vector<std::pair<std::string, std::string>> lines;
    std::stringstream ss(raw);
    std::string segment;
    while (ss >> segment)
    {
        size_t pos = segment.find('=');
        if (pos != std::string::npos)
            lines.push_back({segment.substr(0, pos), segment.substr(pos + 1)});
    }
    return lines;
}

void
mruls::fetchJobDetail(std::string job_id)
{
    m_detail_cancel.store(true);
    if (m_detail_thread.joinable())
        m_detail_thread.detach();
    m_detail_cancel.store(false);
    m_detail_thread = std::thread([this, job_id]()
    {
        std::string cmd    = "scontrol show job " + job_id;
        std::string result = exec(cmd.c_str());

        auto parsed = parseJobDetail(
            result.empty() ? "Error=No details found for Job ID " + job_id
                           : result);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_detail_rows     = std::move(parsed);
            m_detail_selected = 0;
            m_detail_scroll   = 0;
        }

        m_screen.PostEvent(ftxui::Event::Custom);
    });
}

mruls::mruls() : m_screen(ftxui::ScreenInteractive::Fullscreen())
{
    m_running = true;
    m_output  = exec(SQUEUE_CMD);

    initUI();

    m_job_thread = std::thread([this]
    {
        while (m_running)
        {
            {
                std::unique_lock<std::mutex> lk(m_mutex);
                m_cv.wait_for(lk, std::chrono::seconds(5),
                              [this] { return !m_running.load(); });
                if (!m_running)
                    break;
            }

            if (m_view_type == ViewType::JOB_LIST)
            {
                auto output = exec(SQUEUE_CMD);
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (!m_running)
                        break;
                    m_output       = std::move(output);
                    m_output_dirty = true;
                }
            }

            else if (m_view_type == ViewType::JOB_OUTPUT)
            {
                auto job_output = exec();
            }

            if (m_running)
                m_screen.PostEvent(ftxui::Event::Custom);
        }
    });
}

mruls::~mruls()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_running = false;
    }
    m_cv.notify_all();

    if (m_job_thread.joinable())
        m_job_thread.join();

    if (m_detail_thread.joinable())
        m_detail_thread.join();
}
ftxui::Element
mruls::renderTable(const std::string &output)
{
    using namespace ftxui;

    if (m_output_dirty)
    {
        auto rows = parseOutput(output);

        if (rows.empty())
        {
            m_display_rows.clear();
            m_col_widths.clear();
            m_output_dirty = false;
            return text("No jobs") | center;
        }

        // Save raw rows for job_select (no index column yet)
        m_current_rows = rows;

        int nrows    = (int)rows.size();
        size_t ncols = rows[0].size() + 1; // +1 for index column

        // Clamp selection before we need it
        m_selected_row
            = (nrows <= 1) ? 0 : std::clamp(m_selected_row, 1, nrows - 1);

        // Insert index column
        rows[0].insert(rows[0].begin(), "#");
        for (int i = 1; i < nrows; ++i)
            rows[i].insert(rows[i].begin(), std::to_string(i));

        // Ensure all rows have the same width, then compute col widths
        m_col_widths.assign(ncols, 0);
        for (auto &row : rows)
        {
            row.resize(ncols, "");
            for (size_t c = 0; c < ncols; ++c)
                m_col_widths[c] = std::max(m_col_widths[c], (int)row[c].size());
        }

        m_display_rows = std::move(rows);
        m_output_dirty = false;
    }

    if (m_display_rows.empty())
        return text("No jobs") | center;

    int nrows    = (int)m_display_rows.size();
    size_t ncols = m_col_widths.size();

    // Re-clamp in case selection changed since last parse
    m_selected_row
        = (nrows <= 1) ? 0 : std::clamp(m_selected_row, 1, nrows - 1);

    Elements grid_rows;
    grid_rows.reserve(nrows + 1); // +1 for separator

    for (int i = 0; i < nrows; ++i)
    {
        Elements cells;
        cells.reserve(ncols);

        for (size_t c = 0; c < ncols; ++c)
        {
            Element cell = (c == ncols - 1)
                               ? text(m_display_rows[i][c]) | flex_grow
                               : text(m_display_rows[i][c])
                                     | size(WIDTH, EQUAL, m_col_widths[c] + 2);
            cells.push_back(std::move(cell));
        }

        Element row_elem = hbox(std::move(cells));

        if (i == 0)
            row_elem = row_elem | bold;
        else if (i == m_selected_row)
            row_elem
                = row_elem | bgcolor(Color::Blue) | color(Color::White) | bold;

        grid_rows.push_back(std::move(row_elem));

        if (i == 0)
            grid_rows.push_back(separator());
    }

    return vbox(std::move(grid_rows)) | flex;
}

ftxui::Element
mruls::renderDetail()
{
    using namespace ftxui;

    // Copy under lock
    std::vector<std::pair<std::string, std::string>> rows;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        rows = m_detail_rows;
    }

    int total   = (int)rows.size();
    int visible = m_screen.dimy() - FOOTER_HEIGHT;

    m_detail_selected
        = std::clamp(m_detail_selected, 0, std::max(0, total - 1));
    m_detail_scroll
        = std::clamp(m_detail_scroll, 0, std::max(0, total - visible));

    Elements elems;
    for (int i = m_detail_scroll;
         i < std::min(total, m_detail_scroll + visible); ++i)
    {
        const auto &[key, val] = rows[i];
        Element line           = hbox({
            text(key) | size(WIDTH, EQUAL, 25) | color(Color::Cyan),
            text(val) | color(Color::White) | flex_grow,
        });
        if (i == m_detail_selected)
            line = line | bgcolor(Color::Blue) | color(Color::White);
        elems.push_back(std::move(line));
    }

    return vbox({
        hbox({
            text(" JOB INSPECTOR ") | bold | bgcolor(Color::Blue)
                | color(Color::White),
            filler(),
            text(std::to_string(m_detail_selected + 1) + "/"
                 + std::to_string(total))
                | dim,
            text("  [ESC] Back ") | inverted,
        }),
        separator(),
        vbox(std::move(elems)) | flex,
        hbox({text(" ↑↓ / j k / wheel to scroll ") | dim | italic}),
    });
}

void
mruls::loop()
{
    m_screen.Loop(m_main_view);
}

void
mruls::initUI()
{
    using namespace ftxui;

    auto renderer = Renderer([this]() -> Element
    {
        if (m_view_type == ViewType::JOB_DETAIL)
            return renderDetail();

        std::string output;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            output = m_output;
        }
        return vbox({
            text("MRULS") | center | bold | color(Color::Blue),
            renderTable(output),
        });
    });

    auto focusable = Container::Stacked({renderer});

    m_main_view = CatchEvent(focusable, [this](Event event) -> bool
    {
        if (m_view_type == ViewType::JOB_DETAIL)
        {
            if (event == Event::Escape)
            {
                m_key_buf.clear();
                m_view_type = ViewType::JOB_LIST;
                m_screen.PostEvent(ftxui::Event::Custom);
                return true;
            }

            // Handle sequences BEFORE single-char bindings
            // so 'g' can be buffered and 'j'/'k' don't steal it
            if (event.is_character())
            {
                const std::string ch = event.character();

                // 'j' and 'k' only act as movement if not completing a sequence
                if (ch == "j" && m_key_buf.empty())
                {
                    detail_next_line();
                    return true;
                }
                if (ch == "k" && m_key_buf.empty())
                {
                    detail_prev_line();
                    return true;
                }

                // All other chars go through sequence handler
                if (handle_key_sequence(ch))
                    return true;
            }

            if (event == Event::ArrowDown)
            {
                detail_next_line();
                return true;
            }
            if (event == Event::ArrowUp)
            {
                detail_prev_line();
                return true;
            }

            if (event.is_mouse())
            {
                if (event.mouse().button == Mouse::WheelUp)
                {
                    detail_prev_line();
                    return true;
                }
                if (event.mouse().button == Mouse::WheelDown)
                {
                    detail_next_line();
                    return true;
                }
            }

            return false;
        }

        // --- Table view ---
        if (event.is_character())
        {
            const std::string &ch = event.character();

            if (ch == "r")
            {
                refresh_job_list();
                return true;
            }

            if (ch == "q")
            {
                quit();
                return true;
            }

            if (ch == "i")
            {
                job_select();
                return true;
            }

            if (ch == "j" && m_key_buf.empty())
            {
                job_next_line();
                return true;
            }

            if (ch == "k" && m_key_buf.empty())
            {
                job_prev_line();
                return true;
            }

            if (handle_key_sequence(ch))
                return true;
        }

        if (event == Event::ArrowDown)
        {
            job_next_line();
            return true;
        }

        if (event == Event::ArrowUp)
        {
            job_prev_line();
            return true;
        }

        if (event == Event::Return)
        {
            m_view_type = ViewType::JOB_OUTPUT;
            return true;
        }

        return false;
    });
}

void
mruls::detail_beginning()
{
    m_detail_selected = 0;
    m_detail_scroll   = 0;
    m_screen.PostEvent(ftxui::Event::Custom);
}

void
mruls::detail_end()
{
    int total         = (int)m_detail_rows.size();
    int visible       = m_screen.dimy() - FOOTER_HEIGHT;
    m_detail_selected = std::max(0, total - 1);
    m_detail_scroll   = std::max(0, total - visible);
    m_screen.PostEvent(ftxui::Event::Custom);
}

void
mruls::detail_next_line()
{
    int visible = m_screen.dimy() - FOOTER_HEIGHT;
    int total   = m_detail_rows.size();

    m_detail_selected = std::min(m_detail_selected + 1, total - 1);
    if (m_detail_selected >= m_detail_scroll + visible)
        m_detail_scroll = m_detail_selected - visible + 1;
    m_screen.PostEvent(ftxui::Event::Custom);
}

void
mruls::detail_prev_line()
{
    m_detail_selected = std::max(0, m_detail_selected - 1);
    if (m_detail_selected < m_detail_scroll)
        m_detail_scroll = m_detail_selected;
    m_screen.PostEvent(ftxui::Event::Custom);
}

void
mruls::refresh_job_list()
{
    auto output = exec(SQUEUE_CMD);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_output       = std::move(output);
        m_output_dirty = true;
    }
    m_screen.PostEvent(ftxui::Event::Custom);
}

void
mruls::quit()
{
    m_screen.ExitLoopClosure()();
}

void
mruls::job_prev_line()
{
    m_selected_row = std::max(1, m_selected_row - 1);
    m_screen.PostEvent(ftxui::Event::Custom);
}

void
mruls::job_next_line()
{
    m_selected_row++;
    m_screen.PostEvent(ftxui::Event::Custom);
}

void
mruls::job_select()
{
    if (!m_current_rows.empty() && m_selected_row < (int)m_current_rows.size())
    {
        m_view_type        = ViewType::JOB_DETAIL;
        m_detail_scroll    = 0;
        m_detail_selected  = 0;
        std::string job_id = m_current_rows[m_selected_row][0];
        fetchJobDetail(job_id);
    }
}

void
mruls::job_beginning()
{
    m_selected_row = 1; // 0 is header
    m_screen.PostEvent(ftxui::Event::Custom);
}

void
mruls::job_end()
{
    m_selected_row = m_current_rows.size() - 1;
    m_screen.PostEvent(ftxui::Event::Custom);
}

// Returns true and executes action if buffer matches, clears buffer.
// Returns false and clears buffer if no prefix match possible.
// Returns true (waiting) if buffer is a valid prefix.
bool
mruls::handle_key_sequence(const std::string &ch)
{
    m_key_buf += ch;

    // Detail view sequences
    if (m_view_type == ViewType::JOB_DETAIL)
    {
        if (m_key_buf == "g")
            return true; // wait for second key

        if (m_key_buf == "gg")
        {
            m_key_buf.clear();
            detail_beginning();
            return true;
        }

        if (m_key_buf == "G")
        {
            m_key_buf.clear();
            detail_end();
            return true;
        }
    }
    else
    {
        if (m_key_buf == "g")
            return true; // wait for second key

        if (m_key_buf == "gg")
        {
            m_key_buf.clear();
            job_beginning();
            return true;
        }

        if (m_key_buf == "G")
        {
            m_key_buf.clear();
            job_end();
            return true;
        }
    }

    // No match — discard
    m_key_buf.clear();
    return false;
}
