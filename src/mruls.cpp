#include "mruls.hpp"

#include "argparse.hpp"
#include "toml.hpp"
#include "utils.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cstdio>
#include <memory>
#include <sstream>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

mruls::mruls(const argparse::ArgumentParser &parser)
    : m_screen(ftxui::ScreenInteractive::Fullscreen())
{
    readArgs(parser);

    if (execCommand("which squeue") == "")
    {
        std::cerr << "Error: 'squeue' command not found in PATH" << std::endl;
        std::exit(1);
    }

    m_raw_output = execCommand(m_config.slurm.squeue_cmd);
    initUI();

    // Background refresh thread
    m_refresh_thread = std::thread([this]
    {
        std::string output;

        while (m_running)
        {
            if (m_view_type == ViewType::JOB_LIST)
            {
                output = execCommand(m_config.slurm.squeue_cmd);

                if (!output.empty())
                {
                    std::lock_guard lock(m_mutex);
                    if (!m_running)
                        return;
                    m_raw_output = std::move(output);
                    m_dirty      = true;
                }

                if (m_running)
                    m_screen.PostEvent(ftxui::Event::Custom);

                refresh(); // wait with interval
            }
            else if (m_view_type == ViewType::JOB_OUTPUT)
            {
                std::string path;
                {
                    std::lock_guard lock(m_mutex);
                    path = m_output_path;
                }

                if (path.empty())
                {
                    std::unique_lock lock(m_mutex);
                    m_cv.wait_for(lock, std::chrono::milliseconds(100),
                                  [this] { return !m_running.load(); });
                    continue;
                }

                output
                    = readOutputFileTail(path, m_config.job_output.max_lines);
                if (!output.empty())
                {
                    std::lock_guard lock(m_mutex);
                    if (!m_running)
                        return;
                    m_raw_output = std::move(output);
                    m_dirty      = true;
                }

                m_screen.PostEvent(ftxui::Event::Custom);

                waitForFileChange();
            }
            else
            {
                refresh(); // JOB_DETAIL still uses interval
            }
        }
    });
}

mruls::~mruls()
{
    m_running = false;
    m_cv.notify_all();
    teardownInotify();

    if (m_refresh_thread.joinable())
        m_refresh_thread.join();
    if (m_detail_thread.joinable())
        m_detail_thread.join();
}

void
mruls::refresh() noexcept
{
    std::chrono::duration<float> time;

    switch (m_view_type)
    {
        case ViewType::JOB_LIST:
            time = std::chrono::duration<float>(
                m_config.job_list.refresh_interval);
            break;

        case ViewType::JOB_DETAIL:
            time = std::chrono::duration<float>(
                m_config.job_detail.refresh_interval);
            break;

        case ViewType::JOB_OUTPUT:
            break;

        default:
            return;
    }

    std::unique_lock lock(m_mutex);
    m_cv.wait_for(lock, time, [this] { return !m_running.load(); });
    if (!m_running)
        return;
}

void
mruls::readArgs(const argparse::ArgumentParser &parser)
{
    if (parser.is_used("config"))
    {
        if (auto config = parser.get<std::string>("config"); !config.empty())
            m_config_file_path = std::move(config);
    }

    if (parser.is_used("user"))
    {
        if (auto user = parser.get<std::string>("user"); !user.empty())
            m_config.slurm.username = std::move(user);
    }
}

void
mruls::initDefaultConfig()
{
    // No configuration for now, but this is where defaults would be set
}

void
mruls::initConfig()
{
    if (m_config_file_path.empty())
    {
        initDefaultConfig();
        return;
    }
    else
    {
        parseConfig();
    }
}

void
mruls::parseConfig() noexcept
{
    toml::table toml;
    try
    {
        toml = toml::parse_file(m_config_file_path);
    }
    catch (const toml::parse_error &e)
    {
        std::cerr << "Error parsing config file: " << e.what() << std::endl;
        std::exit(1);
    }

    if (auto job_list = toml["job_list"])
    {
        if (auto refresh = job_list["refresh_interval"].value<float>())
            m_config.job_list.refresh_interval = *refresh;
    }

    if (auto job_detail = toml["job_detail"])
    {
        if (auto refresh = job_detail["refresh_interval"].value<float>())
            m_config.job_detail.refresh_interval = *refresh;
    }

    if (auto job_output = toml["job_output"])
    {
        if (auto show_line_numbers
            = job_output["show_line_numbers"].value<bool>())
            m_config.job_output.show_line_numbers = *show_line_numbers;

        if (auto max_lines = job_output["max_lines"].value<int>())
            m_config.job_output.max_lines = *max_lines;

        if (auto auto_scroll = job_output["auto_scroll"].value<bool>())
        {
            m_config.job_output.auto_scroll = *auto_scroll;
            m_auto_scrolling                = *auto_scroll;
        }
    }

    if (auto slurm = toml["slurm"])
    {
        if (auto username = slurm["username"].value<std::string>())
            m_config.slurm.username = std::move(*username);
        if (auto squeue_cmd = slurm["squeue_cmd"].value<std::string>())
            m_config.slurm.squeue_cmd = std::move(*squeue_cmd);
    }
}

void
mruls::loop()
{
    m_screen.Loop(m_main_view);
}

void
mruls::initUI()
{
    auto renderer = ftxui::Renderer([this]
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

    m_main_view = CatchEvent(renderer,
                             [this](ftxui::Event e) { return handleEvent(e); });
}

// ============================================================================
// Event Handling
// ============================================================================

bool
mruls::handleEvent(ftxui::Event event)
{
    // ESC - go back from detail/output views
    if (event == ftxui::Event::Escape)
    {
        if (m_view_type != ViewType::JOB_LIST)
        {
            goBack();
            return true;
        }
        return false;
    }

    // Keyboard navigation
    if (event == ftxui::Event::ArrowDown
        || event == ftxui::Event::Character("j"))
    {
        if (m_key_buffer.empty())
        {
            navDown();
            return true;
        }
    }

    if (event == ftxui::Event::ArrowUp || event == ftxui::Event::Character("k"))
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
        if (event.mouse().button == ftxui::Mouse::WheelDown)
        {
            navDown();
            return true;
        }
        if (event.mouse().button == ftxui::Mouse::WheelUp)
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
    if (event == ftxui::Event::Return && m_view_type == ViewType::JOB_LIST)
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

        if (m_config.job_output.auto_scroll)
            m_auto_scrolling = false;
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
    m_screen.PostEvent(ftxui::Event::Custom);
}

void
mruls::navDown()
{
    if (m_view_type == ViewType::JOB_OUTPUT)
    {
        std::lock_guard lock(m_mutex);
        m_scroll_y++;
        if (m_config.job_output.auto_scroll)
            m_auto_scrolling = false;
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
    m_screen.PostEvent(ftxui::Event::Custom);
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
        if (m_config.job_output.auto_scroll)
            m_auto_scrolling = false;
    }
    else
    {
        m_selected = 1;
    }
    m_screen.PostEvent(ftxui::Event::Custom);
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

        if (m_config.job_output.auto_scroll)
            m_auto_scrolling = true;
    }
    else
    {
        m_selected = std::max(1, getRowCount() - 1);
    }
    m_screen.PostEvent(ftxui::Event::Custom);
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
    auto output = execCommand(m_config.slurm.squeue_cmd);
    {
        std::lock_guard lock(m_mutex);
        m_raw_output = std::move(output);
        m_dirty      = true;
    }

    m_cv.notify_all();
    m_screen.PostEvent(ftxui::Event::Custom);
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
        m_screen.PostEvent(ftxui::Event::Custom);
    });
}

void
mruls::refreshJobList()
{
    auto output = execCommand(m_config.slurm.squeue_cmd);
    {
        std::lock_guard lock(m_mutex);
        m_raw_output = std::move(output);
        m_dirty      = true;
    }
    m_screen.PostEvent(ftxui::Event::Custom);
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
    auto raw  = execCommand("scontrol show job " + job_id);
    auto rows = parseKeyValue(raw);

    std::string stdout_path, stderr_path;
    for (const auto &[k, v] : rows)
    {
        if (k == "StdOut")
            stdout_path = v;
        if (k == "StdErr")
            stderr_path = v;
    }

    const auto &path
        = (m_output_type == OutputType::STDOUT) ? stdout_path : stderr_path;
    if (path.empty())
        return;

    // do initial read BEFORE setting view type so first render has content
    auto initial = readOutputFileTail(path, m_config.job_output.max_lines);

    {
        std::lock_guard lock(m_mutex);
        m_current_job_id = job_id;
        m_stdout_path    = std::move(stdout_path);
        m_stderr_path    = std::move(stderr_path);
        m_output_path = (m_output_type == OutputType::STDOUT) ? m_stdout_path
                                                              : m_stderr_path;
        m_raw_output  = std::move(initial);
        m_view_type   = ViewType::JOB_OUTPUT;
        m_scroll_y    = INT_MAX;
    }

    setupInotify(m_output_path);

    m_cv.notify_all();
    m_screen.PostEvent(ftxui::Event::Custom);
}

void
mruls::toggleOutputType()
{
    m_output_type = (m_output_type == OutputType::STDOUT) ? OutputType::STDERR
                                                          : OutputType::STDOUT;

    std::string new_path;
    {
        std::lock_guard lock(m_mutex);
        new_path = (m_output_type == OutputType::STDOUT) ? m_stdout_path
                                                         : m_stderr_path;
        if (new_path.empty())
            return;
        m_output_path = new_path;
    }

    // read before updating m_raw_output
    auto initial = readOutputFileTail(new_path, m_config.job_output.max_lines);

    {
        std::lock_guard lock(m_mutex);
        m_raw_output = std::move(initial); // ← pre-populate
        m_scroll_y   = INT_MAX;
    }

    setupInotify(new_path);
    m_screen.PostEvent(ftxui::Event::Custom);
    m_cv.notify_all();
}

// ============================================================================
// Rendering
// ============================================================================

ftxui::Element
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
            return ftxui::text("No jobs") | ftxui::center;
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
        return ftxui::text("No jobs") | ftxui::center;

    int nrows  = static_cast<int>(m_job_rows.size());
    m_selected = std::clamp(m_selected, 1, std::max(1, nrows - 1));

    ftxui::Elements rows;
    rows.reserve(nrows + 1);

    for (int i = 0; i < nrows; ++i)
    {
        ftxui::Elements cells;
        cells.reserve(m_col_widths.size());

        // Index column
        auto idx = (i == 0) ? "#" : std::to_string(i);
        cells.push_back(
            ftxui::text(idx)
            | size(ftxui::WIDTH, ftxui::EQUAL, m_col_widths[0] + 2));

        // Data columns
        for (size_t c = 0; c < m_job_rows[i].size(); ++c)
        {
            auto cell = (c == m_job_rows[i].size() - 1)
                            ? ftxui::text(m_job_rows[i][c]) | ftxui::flex_grow
                            : ftxui::text(m_job_rows[i][c])
                                  | size(ftxui::WIDTH, ftxui::EQUAL,
                                         m_col_widths[c + 1] + 2);
            cells.push_back(std::move(cell));
        }

        ftxui::Element row = hbox(std::move(cells));

        if (i == 0)
            row = row | ftxui::bold;
        else if (i == m_selected)
            row = row | bgcolor(ftxui::Color::Blue) | color(ftxui::Color::White)
                  | ftxui::bold;

        rows.push_back(std::move(row));

        if (i == 0)
            rows.push_back(ftxui::separator());
    }

    return ftxui::vbox({
        ftxui::text("MRULS") | ftxui::center | ftxui::bold
            | color(ftxui::Color::Blue),
        vbox(std::move(rows)) | ftxui::flex,
    });
}

ftxui::Element
mruls::renderDetail()
{
    using namespace ftxui;

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

ftxui::Element
mruls::renderOutput()
{
    using namespace ftxui;
    std::lock_guard lock(m_mutex);

    std::vector<std::string> lines;
    std::string current;
    for (char c : m_raw_output)
    {
        if (c == '\n' || c == '\r')
        {
            if (!current.empty())
            {
                lines.push_back(std::move(current));
                current.clear();
            }
        }
        else
        {
            current += c;
        }
    }
    if (!current.empty())
        lines.push_back(std::move(current));

    int total_lines = static_cast<int>(lines.size());
    int visible_h   = m_screen.dimy() - FOOTER_HEIGHT;
    int max_scroll  = std::max(0, total_lines - visible_h);

    if (m_config.job_output.auto_scroll)
    {

        if (m_auto_scrolling)
        {
            m_scroll_y = max_scroll;
        }
        else
        {
            m_scroll_y = std::clamp(m_scroll_y, 0, max_scroll);
            if (m_scroll_y >= max_scroll && total_lines > 0)
            {
                m_auto_scrolling = true;
            }
        }
    }
    else
    {
        m_scroll_y = std::clamp(m_scroll_y, 0, max_scroll);
    }

    Elements elems;
    // Width needed to fit the largest line number
    const int lnum_width = std::to_string(total_lines).size();
    elems.reserve(visible_h);

    for (int i = m_scroll_y; i < std::min(total_lines, m_scroll_y + visible_h);
         ++i)
    {
        Element line = text(lines[i]) | flex_grow;

        if (m_config.job_output.show_line_numbers)
            line = hbox({
                text(std::to_string(i + 1)) | size(WIDTH, EQUAL, lnum_width)
                    | color(Color::GrayDark) | align_right,
                text(" │ ") | color(Color::GrayDark),
                std::move(line),
            });

        elems.push_back(std::move(line));
    }

    const bool is_stdout = (m_output_type == OutputType::STDOUT);
    const auto label     = is_stdout ? " STDOUT " : " STDERR ";
    const auto bg_color  = is_stdout ? Color::Green : Color::Red;

    auto header = hbox({
        text(label) | bold | bgcolor(bg_color) | color(Color::Black),
        filler(),
        text(m_output_path) | dim,
        text("  [ESC] Back ") | inverted,
    });

    if (m_raw_output.empty())
        return vbox({header, separator()});

    return vbox({
        header,
        separator(),
        vbox(std::move(elems)) | flex,
        text(" j/k: scroll | gg/G: start/end | e: toggle stdout/stderr ") | dim,
    });
}

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

std::string
mruls::readOutputFileTail(const std::string &path, int max_lines) noexcept
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        return {};

    const std::streamoff file_size = f.tellg();
    if (file_size == 0)
        return {};

    const std::streamoff CHUNK_SIZE = 4096;
    int lines_found                 = 0;
    std::streamoff current_pos      = file_size;
    std::string buffer;
    buffer.reserve(CHUNK_SIZE);
    bool start_found               = false;
    std::streamoff final_start_pos = 0;

    while (current_pos > 0 && !start_found)
    {
        std::streamoff read_size = std::min(current_pos, CHUNK_SIZE);
        current_pos -= read_size;

        f.seekg(current_pos);
        buffer.resize(static_cast<size_t>(read_size));
        f.read(buffer.data(), read_size);

        for (auto i = static_cast<ssize_t>(read_size - 1); i >= 0; --i)
        {
            if (buffer[i] == '\n')
            {
                ++lines_found;
                if (lines_found > max_lines)
                {
                    final_start_pos = current_pos + i + 1;
                    start_found     = true;
                    break;
                }
            }
        }
    }

    f.seekg(start_found ? final_start_pos : 0);

    std::string result;
    std::string current_line;
    char c;

    while (f.get(c))
    {
        if (c == '\n')
        {
            if (!current_line.empty())
            {
                result += current_line;
                result += '\n';
            }
            current_line.clear();
        }
        else if (c == '\r')
        {
            // overwrite current line (tqdm behavior)
            current_line.clear();
        }
        else
        {
            current_line += c;
        }
    }

    // flush last line
    if (!current_line.empty())
    {
        result += current_line;
        result += '\n';
    }

    return result;
}

void
mruls::setupInotify(const std::string &path) noexcept
{
    teardownInotify(); // clean up any existing watch

    m_inotify_fd = inotify_init1(IN_NONBLOCK);
    if (m_inotify_fd == -1)
        return;

    m_inotify_wd = inotify_add_watch(m_inotify_fd, path.c_str(),
                                     IN_MODIFY | IN_CLOSE_WRITE);
    if (m_inotify_wd == -1)
    {
        close(m_inotify_fd);
        m_inotify_fd = -1;
        return;
    }

    m_watched_path = path;
}

void
mruls::teardownInotify() noexcept
{
    if (m_inotify_fd != -1)
    {
        if (m_inotify_wd != -1)
        {
            inotify_rm_watch(m_inotify_fd, m_inotify_wd);
            m_inotify_wd = -1;
        }
        close(m_inotify_fd);
        m_inotify_fd = -1;
    }
    m_watched_path.clear();
}

void
mruls::waitForFileChange() noexcept
{
    std::string path;
    {
        std::lock_guard lock(m_mutex);
        path = m_output_path;
    }

    if (path.empty())
        return;

    // 1. Initial metadata for the polling fallback
    struct stat st;
    off_t last_size = 0;
    if (stat(path.c_str(), &st) == 0)
        last_size = st.st_size;

    // 2. Main wait loop
    while (m_running && m_view_type == ViewType::JOB_OUTPUT)
    {
        // --- Strategy A: inotify (Fast for local FS) ---
        if (m_inotify_fd != -1)
        {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(m_inotify_fd, &fds);

            // Short timeout so we can check m_running and stat periodically
            struct timeval tv{0, 250000}; // 250ms

            int ret = select(m_inotify_fd + 1, &fds, nullptr, nullptr, &tv);

            if (ret > 0 && FD_ISSET(m_inotify_fd, &fds))
            {
                // Drain inotify events to clear the queue
                char buf[4096]
                    __attribute__((aligned(__alignof__(struct inotify_event))));
                const struct inotify_event *event;

                ssize_t len = read(m_inotify_fd, buf, sizeof(buf));
                if (len > 0)
                    return; // Local change detected!
            }
        }
        else
        {
            // If inotify failed to initialize, sleep manually to prevent CPU
            // pegging
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }

        // --- Strategy B: stat() Polling (Reliable for NFS/Lustre/Clusters) ---
        // Network file systems often don't trigger inotify events across nodes.
        // We manually check if the file size has grown.
        if (stat(path.c_str(), &st) == 0)
        {
            if (st.st_size != last_size)
            {
                off_t new_size = st.st_size;

                // wait briefly to let writer finish (tqdm writes in bursts)
                std::this_thread::sleep_for(std::chrono::milliseconds(50));

                struct stat st2;
                if (stat(path.c_str(), &st2) == 0 && st2.st_size == new_size)
                {
                    return; // stable -> safe to read
                }

                last_size = new_size;
            }
        }

        // Check if the output path changed while we were waiting (e.g., toggled
        // StdErr)
        {
            std::lock_guard lock(m_mutex);
            if (m_output_path != path)
                return;
        }
    }
}

using namespace ftxui;

// ── simple modal renderer ─────────────────────────────────────────────
static Element
renderModal(const std::string &title, const std::string &message)
{
    return dbox({
        // dim background
        vbox({}) | flex | bgcolor(Color::Black) | dim,

        // centered dialog box
        vbox({
            hbox({
                text(" " + title + " ") | bold | bgcolor(Color::Blue)
                    | color(Color::White),
                filler(),
            }),
            separator(),
            text("  " + message + "  ") | flex,
            separator(),
            hbox({
                filler(),
                text(" [ESC] Close ") | inverted,
                text("  "),
            }),
        }) | border
            | size(WIDTH, EQUAL, 50) | size(HEIGHT, EQUAL, 10)
            | clear_under // clears background behind the box
            | center,     // centers in terminal
    });
}

void
mruls::cancelJob() noexcept
{
    m_showing_modal = true;
    m_modal         = utils::renderModal(
        "Cancel Job", "Are you sure you want to cancel this job? [y/N]");
}
