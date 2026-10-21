#pragma once

#include <gtk/gtk.h>
#include <string>
#include <functional>

class LocalPackageDialog {
public:
    static bool canHandle(const std::string &path);
    static void show(GtkWindow *parent, const std::string &filePath, std::function<void(bool success)> onFinished = nullptr);
};
