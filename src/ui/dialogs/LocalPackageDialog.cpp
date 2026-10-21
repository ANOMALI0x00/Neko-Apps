#include "LocalPackageDialog.h"
#include "Transaction.h"
#include "Proc.h"
#include "Config.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cctype>

namespace {

struct PackageMeta {
    std::string filePath;
    std::string pkgName;
    std::string version;
    std::string summary;
    std::string homepage;
    std::string license;
    std::string architecture;
    std::string installedSize;
    std::vector<std::string> dependencies;
    bool isTemplate = false;
    bool isInstalled = false;
    std::string installedVersion;
};

static std::string trimString(const std::string &s) {
    size_t first = s.find_first_not_of(" \t\r\n\"'");
    if (first == std::string::npos) return "";
    size_t last = s.find_last_not_of(" \t\r\n\"'");
    return s.substr(first, last - first + 1);
}

static std::string extractXmlValue(const std::string &xml, const std::string &key, const std::string &tag = "string") {
    std::string keyPattern = "<key>" + key + "</key>";
    auto posKey = xml.find(keyPattern);
    if (posKey == std::string::npos) return "";

    std::string openTag = "<" + tag + ">";
    std::string closeTag = "</" + tag + ">";

    auto posOpen = xml.find(openTag, posKey);
    if (posOpen == std::string::npos) return "";
    posOpen += openTag.length();

    auto posClose = xml.find(closeTag, posOpen);
    if (posClose == std::string::npos) return "";

    return trimString(xml.substr(posOpen, posClose - posOpen));
}

static std::vector<std::string> extractXmlArray(const std::string &xml, const std::string &key) {
    std::vector<std::string> result;
    std::string keyPattern = "<key>" + key + "</key>";
    auto posKey = xml.find(keyPattern);
    if (posKey == std::string::npos) return result;

    auto posArrayOpen = xml.find("<array>", posKey);
    auto posArrayClose = xml.find("</array>", posKey);
    if (posArrayOpen == std::string::npos || posArrayClose == std::string::npos || posArrayOpen > posArrayClose) {
        return result;
    }

    size_t cur = posArrayOpen;
    while (cur < posArrayClose) {
        auto o = xml.find("<string>", cur);
        if (o == std::string::npos || o >= posArrayClose) break;
        o += 8;
        auto c = xml.find("</string>", o);
        if (c == std::string::npos || c >= posArrayClose) break;
        std::string val = trimString(xml.substr(o, c - o));
        if (!val.empty()) {
            result.push_back(val);
        }
        cur = c + 9;
    }
    return result;
}

static std::string formatBytes(uint64_t bytes) {
    if (bytes == 0) return "";
    char buf[64];
    if (bytes >= 1024ULL * 1024 * 1024) {
        snprintf(buf, sizeof(buf), "%.1f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024ULL * 1024) {
        snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else if (bytes >= 1024ULL) {
        snprintf(buf, sizeof(buf), "%.0f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
    }
    return std::string(buf);
}

static PackageMeta parsePackageFile(const std::string &path) {
    PackageMeta meta;
    meta.filePath = path;

    std::string fileName = std::filesystem::path(path).filename().string();
    if (fileName == "template" || path.find("/template") != std::string::npos) {
        meta.isTemplate = true;
        std::ifstream file(path);
        if (file.is_open()) {
            std::string line;
            std::string rev = "1";
            while (std::getline(file, line)) {
                line = trimString(line);
                if (line.empty() || line[0] == '#') continue;
                if (line.rfind("pkgname=", 0) == 0) {
                    meta.pkgName = trimString(line.substr(8));
                } else if (line.rfind("version=", 0) == 0) {
                    meta.version = trimString(line.substr(8));
                } else if (line.rfind("revision=", 0) == 0) {
                    rev = trimString(line.substr(9));
                } else if (line.rfind("short_desc=", 0) == 0) {
                    meta.summary = trimString(line.substr(11));
                } else if (line.rfind("homepage=", 0) == 0) {
                    meta.homepage = trimString(line.substr(9));
                } else if (line.rfind("license=", 0) == 0) {
                    meta.license = trimString(line.substr(8));
                } else if (line.rfind("depends=", 0) == 0) {
                    std::string deps = trimString(line.substr(8));
                    std::istringstream diss(deps);
                    std::string d;
                    while (diss >> d) {
                        d = trimString(d);
                        if (!d.empty()) meta.dependencies.push_back(d);
                    }
                }
            }
            if (!meta.version.empty() && !rev.empty()) {
                meta.version += "_" + rev;
            }
        }
        if (meta.pkgName.empty()) {
            meta.pkgName = std::filesystem::path(path).parent_path().filename().string();
        }
        meta.architecture = "Código Fuente (xbps-src)";
    } else {
        // .xbps binary archive
        meta.isTemplate = false;
        std::string plist = Proc::capture("tar", {"-xOf", path, "./props.plist"});
        if (!plist.empty()) {
            meta.pkgName = extractXmlValue(plist, "pkgname");
            meta.version = extractXmlValue(plist, "version");
            meta.summary = extractXmlValue(plist, "short_desc");
            meta.homepage = extractXmlValue(plist, "homepage");
            meta.license = extractXmlValue(plist, "license");
            meta.architecture = extractXmlValue(plist, "architecture");
            std::string szStr = extractXmlValue(plist, "installed_size", "integer");
            if (!szStr.empty()) {
                try {
                    uint64_t b = std::stoull(szStr);
                    meta.installedSize = formatBytes(b);
                } catch (...) {}
            }
            meta.dependencies = extractXmlArray(plist, "run_depends");
        }

        // Fallback: extract name and version from filename if plist parsing had gaps
        if (meta.pkgName.empty()) {
            std::string bname = std::filesystem::path(path).filename().string();
            if (bname.size() > 5 && bname.substr(bname.size() - 5) == ".xbps") {
                bname = bname.substr(0, bname.size() - 5);
            }
            auto lastDot = bname.rfind('.');
            if (lastDot != std::string::npos) {
                meta.architecture = bname.substr(lastDot + 1);
                bname = bname.substr(0, lastDot);
            }
            auto lastDash = bname.rfind('-');
            if (lastDash != std::string::npos) {
                meta.version = bname.substr(lastDash + 1);
                meta.pkgName = bname.substr(0, lastDash);
            } else {
                meta.pkgName = bname;
            }
        }
    }

    // Check if package is already installed on the system
    if (!meta.pkgName.empty()) {
        std::string qOut = Proc::capture("xbps-query", {"-p", "pkgver", meta.pkgName});
        qOut = trimString(qOut);
        if (!qOut.empty() && qOut.find(meta.pkgName) != std::string::npos) {
            meta.isInstalled = true;
            meta.installedVersion = qOut;
        }
    }

    return meta;
}

} // namespace

bool LocalPackageDialog::canHandle(const std::string &path) {
    if (!std::filesystem::exists(path)) return false;
    if (path.size() > 5 && path.substr(path.size() - 5) == ".xbps") return true;
    std::string bname = std::filesystem::path(path).filename().string();
    if (bname == "template") return true;
    return false;
}

void LocalPackageDialog::show(GtkWindow *parent, const std::string &filePath, std::function<void(bool success)> onFinished) {
    if (!canHandle(filePath)) return;

    PackageMeta meta = parsePackageFile(filePath);

    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        meta.isTemplate ? "Instalador de Plantilla - Neko-Apps" : "Instalador de Paquetes Locales - Neko-Apps",
        parent,
        GTK_DIALOG_MODAL,
        "Cancelar", GTK_RESPONSE_CANCEL,
        nullptr
    );
    gtk_window_set_default_size(GTK_WINDOW(dlg), 620, -1);

    GtkWidget *btnCancel = gtk_dialog_get_widget_for_response(GTK_DIALOG(dlg), GTK_RESPONSE_CANCEL);
    if (btnCancel) {
        gtk_style_context_add_class(gtk_widget_get_style_context(btnCancel), "neko-btn-secondary");
    }

    std::string actionLabel = meta.isTemplate ? "Compilar e Instalar" : (meta.isInstalled ? "Reinstalar Paquete" : "Instalar Paquete");
    GtkWidget *btnAction = gtk_dialog_add_button(GTK_DIALOG(dlg), actionLabel.c_str(), GTK_RESPONSE_ACCEPT);
    if (btnAction) {
        gtk_style_context_add_class(gtk_widget_get_style_context(btnAction), "neko-btn-primary");
    }

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(content), 20);

    GtkWidget *rootBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_box_pack_start(GTK_BOX(content), rootBox, TRUE, TRUE, 0);

    // 1. Hero Header Panel
    GtkWidget *heroBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_style_context_add_class(gtk_widget_get_style_context(heroBox), "neko-box-panel");
    gtk_box_pack_start(GTK_BOX(rootBox), heroBox, FALSE, FALSE, 0);

    GtkWidget *icon = gtk_image_new_from_icon_name(
        meta.isTemplate ? "applications-development" : "package-x-generic",
        GTK_ICON_SIZE_DIALOG
    );
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 48);
    gtk_box_pack_start(GTK_BOX(heroBox), icon, FALSE, FALSE, 0);

    GtkWidget *heroText = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(heroBox), heroText, TRUE, TRUE, 0);

    GtkWidget *titleRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *lblTitle = gtk_label_new(meta.pkgName.c_str());
    gtk_widget_set_halign(lblTitle, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblTitle), "neko-hero-title");
    gtk_box_pack_start(GTK_BOX(titleRow), lblTitle, FALSE, FALSE, 0);

    if (!meta.version.empty()) {
        GtkWidget *lblVer = gtk_label_new(("v" + meta.version).c_str());
        gtk_style_context_add_class(gtk_widget_get_style_context(lblVer), "neko-version-badge");
        gtk_box_pack_start(GTK_BOX(titleRow), lblVer, FALSE, FALSE, 0);
    }

    GtkWidget *lblBadge = gtk_label_new(meta.isTemplate ? "Plantilla (xbps-src)" : "Binario XBPS (.xbps)");
    gtk_style_context_add_class(gtk_widget_get_style_context(lblBadge), "neko-filter-pill");
    gtk_box_pack_start(GTK_BOX(titleRow), lblBadge, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(heroText), titleRow, FALSE, FALSE, 0);

    std::string summary = meta.summary.empty() ? (meta.isTemplate ? "Plantilla de compilación local para xbps-src" : "Paquete binario para Void Linux") : meta.summary;
    GtkWidget *lblSummary = gtk_label_new(summary.c_str());
    gtk_widget_set_halign(lblSummary, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(lblSummary), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblSummary), "neko-card-summary");
    gtk_box_pack_start(GTK_BOX(heroText), lblSummary, FALSE, FALSE, 0);

    // 2. Status Banner (Installed / Not Installed)
    GtkWidget *statusBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(statusBox), "neko-hero-badge");
    GtkWidget *statusIcon = gtk_image_new_from_icon_name(
        meta.isInstalled ? "dialog-information-symbolic" : "software-update-available-symbolic",
        GTK_ICON_SIZE_MENU
    );
    gtk_box_pack_start(GTK_BOX(statusBox), statusIcon, FALSE, FALSE, 0);

    std::string statusMsg;
    if (meta.isInstalled) {
        statusMsg = "Ya se encuentra instalado en el sistema: " + meta.installedVersion;
    } else {
        statusMsg = "Paquete no instalado actualmente en tu sistema.";
    }
    GtkWidget *lblStatus = gtk_label_new(statusMsg.c_str());
    gtk_box_pack_start(GTK_BOX(statusBox), lblStatus, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rootBox), statusBox, FALSE, FALSE, 0);

    // 3. Information Grid
    GtkWidget *infoPanel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(infoPanel), "neko-box-panel");
    gtk_box_pack_start(GTK_BOX(rootBox), infoPanel, FALSE, FALSE, 0);

    auto addInfoRow = [infoPanel](const char *key, const std::string &value) {
        if (value.empty()) return;
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        GtkWidget *lblKey = gtk_label_new(key);
        gtk_widget_set_size_request(lblKey, 140, -1);
        gtk_widget_set_halign(lblKey, GTK_ALIGN_START);
        gtk_style_context_add_class(gtk_widget_get_style_context(lblKey), "neko-card-summary");
        gtk_box_pack_start(GTK_BOX(row), lblKey, FALSE, FALSE, 0);

        GtkWidget *lblVal = gtk_label_new(value.c_str());
        gtk_widget_set_halign(lblVal, GTK_ALIGN_START);
        gtk_label_set_line_wrap(GTK_LABEL(lblVal), TRUE);
        gtk_style_context_add_class(gtk_widget_get_style_context(lblVal), "neko-card-title");
        gtk_box_pack_start(GTK_BOX(row), lblVal, TRUE, TRUE, 0);

        gtk_box_pack_start(GTK_BOX(infoPanel), row, FALSE, FALSE, 0);
    };

    addInfoRow("Archivo:", meta.filePath);
    if (!meta.architecture.empty()) addInfoRow("Arquitectura:", meta.architecture);
    if (!meta.installedSize.empty()) addInfoRow("Espacio Requerido:", meta.installedSize);
    if (!meta.license.empty()) addInfoRow("Licencia:", meta.license);
    if (!meta.homepage.empty()) addInfoRow("Sitio Web:", meta.homepage);

    if (!meta.dependencies.empty()) {
        std::string depsStr;
        for (size_t i = 0; i < meta.dependencies.size(); ++i) {
            if (i > 0) depsStr += ", ";
            depsStr += meta.dependencies[i];
            if (i >= 7 && meta.dependencies.size() > 8) {
                depsStr += " (+ " + std::to_string(meta.dependencies.size() - 8) + " más)";
                break;
            }
        }
        addInfoRow("Dependencias:", depsStr);
    }

    struct DialogCtx {
        PackageMeta meta;
        std::function<void(bool success)> onFinished;
    };
    auto *ctx = new DialogCtx{meta, onFinished};

    g_signal_connect_data(dlg, "response", G_CALLBACK(+[](GtkDialog *d, gint response, gpointer data) {
        auto *c = static_cast<DialogCtx*>(data);
        PackageMeta m = c->meta;
        auto finishCb = c->onFinished;
        gtk_widget_destroy(GTK_WIDGET(d));

        if (response == GTK_RESPONSE_ACCEPT) {
            if (m.isTemplate) {
                // Copy template into void-packages and compile
                std::string vpDir = Config::instance().voidPackagesDir();
                std::string targetDir = vpDir + "/srcpkgs/" + m.pkgName;
                std::filesystem::create_directories(targetDir);
                try {
                    std::filesystem::copy_file(
                        m.filePath,
                        targetDir + "/template",
                        std::filesystem::copy_options::overwrite_existing
                    );
                } catch (...) {}

                Package p;
                p.source = PkgSource::Source;
                p.id = m.pkgName;
                p.name = m.pkgName;
                p.version = m.version;
                p.summary = m.summary;
                Transaction::install(p, [finishCb](bool ok, const std::string &/*msg*/) {
                    if (finishCb) finishCb(ok);
                });
            } else {
                // Install .xbps local binary package
                Transaction::installLocalPackageFile(m.filePath, m.pkgName, [finishCb](bool ok, const std::string &/*msg*/) {
                    if (finishCb) finishCb(ok);
                });
            }
        } else {
            if (finishCb) finishCb(false);
        }
    }), ctx, [](gpointer d, GClosure*) { delete static_cast<DialogCtx*>(d); }, (GConnectFlags)0);

    gtk_widget_show_all(dlg);
}
