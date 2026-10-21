#pragma once

#include <gtk/gtk.h>
#include <string>

class FloatingTerminal {
public:
    explicit FloatingTerminal(GtkWindow *parentWindow);
    ~FloatingTerminal() = default;

    void show();
    void hide();
    void toggle(bool showWindow);
    void toggleVisibility();
    bool isVisible() const;

    void appendLog(const std::string &line, bool isStderr);
    void clearLog();

private:
    GtkWidget *m_window = nullptr;
    GtkWidget *m_textView = nullptr;
    GtkTextBuffer *m_buffer = nullptr;
    GtkTextMark *m_endMark = nullptr;
};
