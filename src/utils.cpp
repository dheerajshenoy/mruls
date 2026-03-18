#include "utils.hpp"

namespace utils {
    Element renderModal(const std::string &title,
            const std::string &message)
    {
        return dbox({
                // dim background
                vbox({}) | flex | bgcolor(Color::Black) | dim,

                // centered dialog box
                vbox({
                        hbox({
                                text(" " + title + " ")
                                | bold | bgcolor(Color::Blue) | color(Color::White),
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
                        })
                | border
                    | size(WIDTH,  EQUAL, 50)
                    | size(HEIGHT, EQUAL, 10)
                    | clear_under        // clears background behind the box
                    | center,            // centers in terminal
        });
    }
};
