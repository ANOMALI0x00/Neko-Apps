#pragma once

#include <gtk/gtk.h>
#include "Package.h"
#include "Transaction.h"
#include <functional>
#include <string>

class BottomProgressBar {
public:
    explicit BottomProgressBar(std::function<void()> onDismiss = nullptr);
    ~BottomProgressBar() = default;

    GtkWidget* widget() const { return m_container; }

    void updateProgress(const std::string &title, const DownloadProgress &prog);
    void setFinished(bool ok, const std::string &msg);
    void showTestState();
    void hide();

private:
    std::function<void()> m_onDismiss;

    GtkWidget *m_container = nullptr;
    GtkWidget *m_actionBar = nullptr;
    GtkWidget *m_lblTitle = nullptr;
    GtkWidget *m_lblStatus = nullptr;
    GtkWidget *m_progressBar = nullptr;
    GtkWidget *m_btnClose = nullptr;
};
