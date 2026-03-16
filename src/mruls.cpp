#include "mruls.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cstdio>
#include <memory>
#include <sstream>

using namespace ftxui;

// ============================================================================
// Static helpers
// ============================================================================

std::vector<std::string>
mruls::splitLine(const std::string &line)
{
    std::vector<std::string> tokens;
    std::istringstream iss(line);
    std::string tok;
    while (iss >> tok)
        tokens.push_back(std::move(tok));
    return tokens;
}

std::vector<std::vector<std::string>>
mruls::parseOutput(const std::string &output)
{
    std::vector<std::vector<std::string>> rows;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line))
    {
        if (!line.empty())
            rows.push_back(splitLine(line));
    }
    return rows;
}

std::vector<std::pair<std::string, std::string>>
mruls::parseKeyValue(const std::string &raw)
{
    std::vector<std::pair<std::string, std::string>> result;
    std::istringstream ss(raw);
    std::string token;
    while (ss >> token)
    {
        if (auto pos = token.find('='); pos != std::string::npos)
            result.emplace_back(token.substr(0, pos), token.substr(pos + 1));
    }
    return result;
}

std::string
mruls::execCommand(const std::string &cmd)
{
    std::array<char, 256> buffer;
    std::string result;

    std::unique_ptr<FILE, int (*)(FILE *)> pipe(popen(cmd.c_str(), "r"),
                                                pclose);
    if (!pipe)
        return {};

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()))
        result += buffer.data();

    return result;
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

mruls::mruls() : m_screen(ScreenInteractive::Fullscreen())
{
    m_raw_output = execCommand(SQUEUE_CMD);
    initUI();

    // Background refresh thread
    m_refresh_thread = std::thread([this]
    {
        while (m_running)
        {
            {
                std::unique_lock lock(m_mutex);
                m_cv.wait_for(lock, std::chrono::seconds(REFRESH_INTERVAL),
                              [this] { return !m_running.load(); });
                if (!m_running)
                    return;
            }

            std::string output;
            if (m_view_type == ViewType::JOB_LIST)
            {
                output = execCommand(SQUEUE_CMD);
            }
            else if (m_view_type == ViewType::JOB_OUTPUT)
            {
                std::string path;
                {
                    std::lock_guard lock(m_mutex);
                    path = m_output_path;
                }
                if (!path.empty())
                    output = execCommand("tail -n 200 '" + path + "'");
            }

            if (!output.empty())
            {
                std::lock_guard lock(m_mutex);
                if (!m_running)
                    return;
                m_raw_output = std::move(output);
                m_dirty      = true;
            }

            if (m_running)
                m_screen.PostEvent(Event::Custom);
        }
    });
}

mruls::~mruls()
{
    m_running = false;
    m_cv.notify_all();

    if (m_refresh_thread.joinable())
        m_refresh_thread.join();
    if (m_detail_thread.joinable())
        m_detail_thread.join();
}

void
mruls::loop()
{
    m_screen.Loop(m_main_view);
}

// ============================================================================
// UI Initialization
// ============================================================================

void
mruls::initUI()
{
    auto renderer = Renderer([this]
    {
        switch (m_view_type)
        {
            case ViewType::JOB_DETAIL:
                return renderDetail();
            case ViewType::JOB_OUTPUT:
                return renderOutput();
            default:
                return renderJobList();
        }
    });

    m_main_view
        = CatchEvent(renderer, [this](Event e) { return handleEvent(e); });
}

// ============================================================================
// Event Handling
// ============================================================================

bool
mruls::handleEvent(Event event)
{
    // ESC - go back from detail/output views
    if (event == Event::Escape)
    {
        if (m_view_type != ViewType::JOB_LIST)
        {
            goBack();
            return true;
        }
        return false;
    }

    // Keyboard navigation
    if (event == Event::ArrowDown || event == Event::Character("j"))
    {
        if (m_key_buffer.empty())
        {
            navDown();
            return true;
        }
    }

    if (event == Event::ArrowUp || event == Event::Character("k"))
    {
        if (m_key_buffer.empty())
        {
            navUp();
            return true;
        }
    }

    // Mouse wheel
    if (event.is_mouse())
    {
        if (event.mouse().button == Mouse::WheelDown)
        {
            navDown();
            return true;
        }
        if (event.mouse().button == Mouse::WheelUp)
        {
            navUp();
            return true;
        }
    }

    // Character commands
    if (event.is_character())
    {
        const auto &ch = event.character();

        // Key sequences (gg, G)
        if (handleKeySequence(ch))
            return true;

        // Job list specific commands
        if (m_view_type == ViewType::JOB_LIST)
        {
            if (ch == "q")
            {
                quit();
                return true;
            }
            if (ch == "r")
            {
                refreshJobList();
                return true;
            }
            if (ch == "i")
            {
                selectJob();
                return true;
            }
        }

        // Output view specific commands
        if (m_view_type == ViewType::JOB_OUTPUT)
        {
            if (ch == "e")
            {
                toggleOutputType();
                return true;
            }
        }
    }

    // Enter - view job output (job list only)
    if (event == Event::Return && m_view_type == ViewType::JOB_LIST)
    {
        int count = getRowCount();
        if (m_selected > 0 && m_selected < count)
        {
            std::string job_id;
            {
                std::lock_guard lock(m_mutex);
                job_id = m_job_rows[m_selected][0];
            }
            loadJobOutput(job_id);
        }
        return true;
    }

    return false;
}

bool
mruls::handleKeySequence(const std::string &ch)
{
    m_key_buffer += ch;

    if (m_key_buffer == "g")
        return true; // Wait for next key

    if (m_key_buffer == "gg")
    {
        m_key_buffer.clear();
        navBegin();
        return true;
    }

    if (m_key_buffer == "G")
    {
        m_key_buffer.clear();
        navEnd();
        return true;
    }

    m_key_buffer.clear();
    return false;
}

// ============================================================================
// Navigation
// ============================================================================

void
mruls::navUp()
{
    if (m_view_type == ViewType::JOB_OUTPUT)
    {
        std::lock_guard lock(m_mutex);
        m_scroll_y = std::max(0, m_scroll_y - 1);
    }
    else if (m_view_type == ViewType::JOB_DETAIL)
    {
        std::lock_guard lock(m_mutex);
        m_detail_selected = std::max(0, m_detail_selected - 1);
        if (m_detail_selected < m_scroll_y)
            m_scroll_y = m_detail_selected;
    }
    else
    {
        m_selected = std::max(1, m_selected - 1);
    }
    m_screen.PostEvent(Event::Custom);
}

void
mruls::navDown()
{
    if (m_view_type == ViewType::JOB_OUTPUT)
    {
        std::lock_guard lock(m_mutex);
        m_scroll_y++;
    }
    else if (m_view_type == ViewType::JOB_DETAIL)
    {
        std::lock_guard lock(m_mutex);
        int total         = static_cast<int>(m_detail_rows.size());
        int visible       = m_screen.dimy() - FOOTER_HEIGHT;
        m_detail_selected = std::min(m_detail_selected + 1, total - 1);
        if (m_detail_selected >= m_scroll_y + visible)
            m_scroll_y = m_detail_selected - visible + 1;
    }
    else
    {
        int max    = getRowCount() - 1;
        m_selected = std::min(m_selected + 1, std::max(1, max));
    }
    m_screen.PostEvent(Event::Custom);
}

void
mruls::navBegin()
{
    if (m_view_type == ViewType::JOB_DETAIL)
    {
        std::lock_guard lock(m_mutex);
        m_detail_selected = 0;
        m_scroll_y        = 0;
    }
    else if (m_view_type == ViewType::JOB_OUTPUT)
    {
        std::lock_guard lock(m_mutex);
        m_scroll_y = 0;
    }
    else
    {
        m_selected = 1;
    }
    m_screen.PostEvent(Event::Custom);
}

void
mruls::navEnd()
{
    if (m_view_type == ViewType::JOB_DETAIL)
    {
        std::lock_guard lock(m_mutex);
        int total         = static_cast<int>(m_detail_rows.size());
        int visible       = m_screen.dimy() - FOOTER_HEIGHT;
        m_detail_selected = std::max(0, total - 1);
        m_scroll_y        = std::max(0, total - visible);
    }
    else if (m_view_type == ViewType::JOB_OUTPUT)
    {
        // Scroll to end handled in render
        std::lock_guard lock(m_mutex);
        m_scroll_y = INT_MAX; // Will be clamped in render
    }
    else
    {
        m_selected = std::max(1, getRowCount() - 1);
    }
    m_screen.PostEvent(Event::Custom);
}

void
mruls::selectJob()
{
    int count = getRowCount();
    if (m_selected > 0 && m_selected < count)
    {
        std::string job_id;
        {
            std::lock_guard lock(m_mutex);
            job_id = m_job_rows[m_selected][0];
        }
        m_view_type       = ViewType::JOB_DETAIL;
        m_detail_selected = 0;
        m_scroll_y        = 0;
        fetchJobDetail(job_id);
    }
}

void
mruls::goBack()
{
    m_key_buffer.clear();
    m_view_type = ViewType::JOB_LIST;

    // Refresh job list data since m_raw_output may contain output view data
    auto output = execCommand(SQUEUE_CMD);
    {
        std::lock_guard lock(m_mutex);
        m_raw_output = std::move(output);
        m_dirty      = true;
    }

    m_cv.notify_all();
    m_screen.PostEvent(Event::Custom);
}

void
mruls::quit()
{
    m_screen.ExitLoopClosure()();
}

int
mruls::getRowCount() const
{
    std::lock_guard lock(m_mutex);
    return static_cast<int>(m_job_rows.size());
}

// ============================================================================
// Data Fetching
// ============================================================================

void
mruls::fetchJobDetail(const std::string &job_id)
{
    // Wait for any pending detail fetch to complete
    if (m_detail_thread.joinable())
        m_detail_thread.join();

    m_detail_pending = true;

    m_detail_thread = std::thread([this, job_id]
    {
        auto result = execCommand("scontrol show job " + job_id);
        auto parsed = parseKeyValue(
            result.empty() ? "Error=No details found for Job " + job_id
                           : result);

        {
            std::lock_guard lock(m_mutex);
            m_detail_rows     = std::move(parsed);
            m_detail_selected = 0;
            m_scroll_y        = 0;
        }

        m_detail_pending = false;
        m_screen.PostEvent(Event::Custom);
    });
}

void
mruls::refreshJobList()
{
    auto output = execCommand(SQUEUE_CMD);
    {
        std::lock_guard lock(m_mutex);
        m_raw_output = std::move(output);
        m_dirty      = true;
    }
    m_screen.PostEvent(Event::Custom);
}

std::string
mruls::getJobOutputPath(const std::string &job_id, OutputType type)
{
    auto raw        = execCommand("scontrol show job " + job_id);
    auto rows       = parseKeyValue(raw);
    const char *key = (type == OutputType::STDOUT) ? "StdOut" : "StdErr";

    for (const auto &[k, v] : rows)
    {
        if (k == key)
            return v;
    }
    return {};
}

void
mruls::loadJobOutput(const std::string &job_id)
{
    auto path = getJobOutputPath(job_id, m_output_type);
    if (!path.empty())
    {
        std::lock_guard lock(m_mutex);
        m_current_job_id = job_id;
        m_output_path    = std::move(path);
        m_raw_output     = execCommand("tail -n 200 '" + m_output_path + "'");
        m_view_type      = ViewType::JOB_OUTPUT;
        m_scroll_y       = 0;
    }
}

void
mruls::toggleOutputType()
{
    m_output_type = (m_output_type == OutputType::STDOUT) ? OutputType::STDERR
                                                          : OutputType::STDOUT;

    std::string job_id;
    {
        std::lock_guard lock(m_mutex);
        job_id = m_current_job_id;
    }

    auto path = getJobOutputPath(job_id, m_output_type);
    if (!path.empty())
    {
        std::lock_guard lock(m_mutex);
        m_output_path = std::move(path);
        m_raw_output  = execCommand("tail -n 200 '" + m_output_path + "'");
        m_scroll_y    = 0;
    }

    m_screen.PostEvent(Event::Custom);
}

// ============================================================================
// Rendering
// ============================================================================

Element
mruls::renderJobList()
{
    std::lock_guard lock(m_mutex);

    if (m_dirty)
    {
        m_job_rows = parseOutput(m_raw_output);
        m_dirty    = false;

        if (m_job_rows.empty())
        {
            m_col_widths.clear();
            return text("No jobs") | center;
        }

        // Calculate column widths
        size_t ncols = m_job_rows[0].size() + 1; // +1 for index
        m_col_widths.assign(ncols, 0);

        for (size_t r = 0; r < m_job_rows.size(); ++r)
        {
            // Index column width
            auto idx_str = (r == 0) ? "#" : std::to_string(r);
            m_col_widths[0]
                = std::max(m_col_widths[0], static_cast<int>(idx_str.size()));

            // Data columns
            for (size_t c = 0; c < m_job_rows[r].size(); ++c)
                m_col_widths[c + 1]
                    = std::max(m_col_widths[c + 1],
                               static_cast<int>(m_job_rows[r][c].size()));
        }
    }

    if (m_job_rows.empty())
        return text("No jobs") | center;

    int nrows  = static_cast<int>(m_job_rows.size());
    m_selected = std::clamp(m_selected, 1, std::max(1, nrows - 1));

    Elements rows;
    rows.reserve(nrows + 1);

    for (int i = 0; i < nrows; ++i)
    {
        Elements cells;
        cells.reserve(m_col_widths.size());

        // Index column
        auto idx = (i == 0) ? "#" : std::to_string(i);
        cells.push_back(text(idx) | size(WIDTH, EQUAL, m_col_widths[0] + 2));

        // Data columns
        for (size_t c = 0; c < m_job_rows[i].size(); ++c)
        {
            auto cell = (c == m_job_rows[i].size() - 1)
                            ? text(m_job_rows[i][c]) | flex_grow
                            : text(m_job_rows[i][c])
                                  | size(WIDTH, EQUAL, m_col_widths[c + 1] + 2);
            cells.push_back(std::move(cell));
        }

        Element row = hbox(std::move(cells));

        if (i == 0)
            row = row | bold;
        else if (i == m_selected)
            row = row | bgcolor(Color::Blue) | color(Color::White) | bold;

        rows.push_back(std::move(row));

        if (i == 0)
            rows.push_back(separator());
    }

    return vbox({
        text("MRULS") | center | bold | color(Color::Blue),
        vbox(std::move(rows)) | flex,
    });
}

Element
mruls::renderDetail()
{
    std::lock_guard lock(m_mutex);

    if (m_detail_rows.empty())
    {
        if (m_detail_pending)
            return text("Loading...") | center;
        return text("No details") | center;
    }

    int total   = static_cast<int>(m_detail_rows.size());
    int visible = m_screen.dimy() - FOOTER_HEIGHT;

    m_detail_selected = std::clamp(m_detail_selected, 0, total - 1);
    m_scroll_y        = std::clamp(m_scroll_y, 0, std::max(0, total - visible));

    Elements elems;
    elems.reserve(visible);

    for (int i = m_scroll_y; i < std::min(total, m_scroll_y + visible); ++i)
    {
        const auto &[key, val] = m_detail_rows[i];
        Element line           = hbox({
            text(key) | size(WIDTH, EQUAL, 25) | color(Color::Cyan),
            text(val) | flex_grow,
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
        text(" j/k/arrows: scroll | gg/G: start/end ") | dim,
    });
}

Element
mruls::renderOutput()
{
    std::lock_guard lock(m_mutex);

    std::vector<std::string> lines;
    std::istringstream iss(m_raw_output);
    std::string line;
    while (std::getline(iss, line))
        lines.push_back(std::move(line));

    int total   = static_cast<int>(lines.size());
    int visible = m_screen.dimy() - FOOTER_HEIGHT;

    m_scroll_y = std::clamp(m_scroll_y, 0, std::max(0, total - visible));

    Elements elems;
    elems.reserve(visible);

    for (int i = m_scroll_y; i < std::min(total, m_scroll_y + visible); ++i)
        elems.push_back(text(lines[i]));

    const bool is_stdout = (m_output_type == OutputType::STDOUT);
    const auto label     = is_stdout ? " STDOUT " : " STDERR ";
    const auto bg_color  = is_stdout ? Color::Green : Color::Red;

    return vbox({
        hbox({
            text(label) | bold | bgcolor(bg_color) | color(Color::Black),
            filler(),
            text(m_output_path) | dim,
            text("  [ESC] Back ") | inverted,
        }),
        separator(),
        vbox(std::move(elems)) | flex,
        text(" j/k: scroll | gg/G: start/end | e: toggle stdout/stderr ") | dim,
    });
}
