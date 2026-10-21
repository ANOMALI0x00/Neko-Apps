#include "ReposView.h"
#include "Transaction.h"
#include "Proc.h"
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <thread>
#include <filesystem>
#include <cmath>

namespace {

struct MirrorInfo {
    std::string name;
    std::string url;
    std::string region;
    std::string location;
    int tier;
};

static std::vector<MirrorInfo> getAllMirrors() {
    return {
        {"repo-fastly.voidlinux.org", "https://repo-fastly.voidlinux.org/", "Global", "Fastly Global CDN (Recomendado)", 1},
        {"repo-default.voidlinux.org", "https://repo-default.voidlinux.org/", "Global", "Default (Auto-selecciona mejor Tier 1)", 1},
        {"mirrors.cicku.me/voidlinux", "https://mirrors.cicku.me/voidlinux/", "Global", "Cloudflare Global CDN", 1},
        {"repo-fi.voidlinux.org", "https://repo-fi.voidlinux.org/", "Europa", "Helsinki, Finlandia", 1},
        {"repo-de.voidlinux.org", "https://repo-de.voidlinux.org/", "Europa", "Frankfurt, Alemania", 1},
        {"mirrors.summithq.com/voidlinux", "https://mirrors.summithq.com/voidlinux/", "Norteamérica", "Chicago, EE. UU.", 1},

        // Tier 2 - América
        {"void.voidbr.org/voidlinux", "https://void.voidbr.org/voidlinux/", "Sudamérica", "Mirante do Paranapanema/SP, Brasil", 2},
        {"void.voidlinux.com.br/voidlinux", "https://void.voidlinux.com.br/voidlinux/", "Sudamérica", "Cacoal, Brasil", 2},
        {"mirror.linux.ec/voidlinux", "https://mirror.linux.ec/voidlinux/", "Sudamérica", "Quito, Ecuador", 2},
        {"mirror.vofr.net/voidlinux", "https://mirror.vofr.net/voidlinux/", "Norteamérica", "Virginia, EE. UU.", 2},
        {"mirrors.lug.mtu.edu/voidlinux", "https://mirrors.lug.mtu.edu/voidlinux/", "Norteamérica", "Michigan, EE. UU.", 2},

        // Tier 2 - Europa
        {"mirrors.dotsrc.org/voidlinux", "https://mirrors.dotsrc.org/voidlinux/", "Europa", "Dinamarca", 2},
        {"voidlinux.mirror.garr.it", "https://voidlinux.mirror.garr.it/", "Europa", "Italia", 2},
        {"void.sakamoto.pl", "https://void.sakamoto.pl/", "Europa", "Varsovia, Polonia", 2},
        {"mirror.puzzle.ch/voidlinux", "https://mirror.puzzle.ch/voidlinux/", "Europa", "Berna, Suiza", 2},
        {"mirror.accum.se/mirror/voidlinux", "https://mirror.accum.se/mirror/voidlinux/", "Europa", "Suecia", 2},
        {"ftp.cc.uoc.gr/mirrors/linux/voidlinux", "https://ftp.cc.uoc.gr/mirrors/linux/voidlinux/", "Europa", "Grecia", 2},

        // Tier 2 - Asia y Oceanía
        {"repo.jing.rocks/voidlinux", "https://repo.jing.rocks/voidlinux/", "Asia", "Tokio, Japón", 2},
        {"mirror.freedif.org/voidlinux", "https://mirror.freedif.org/voidlinux/", "Asia", "Singapur", 2},
        {"mirror.aarnet.edu.au/pub/voidlinux", "https://mirror.aarnet.edu.au/pub/voidlinux/", "Oceanía", "Canberra, Australia", 2}
    };
}

static std::string getCurrentActiveMirror() {
    const std::vector<std::string> confPaths = {
        "/etc/xbps.d/00-repository-main.conf",
        "/etc/xbps.d/1-repository-neko.conf",
        "/usr/share/xbps.d/00-repository-main.conf"
    };
    for (const auto &p : confPaths) {
        if (!std::filesystem::exists(p)) continue;
        std::ifstream f(p);
        if (!f.is_open()) continue;
        std::string line;
        while (std::getline(f, line)) {
            size_t pos = line.find("repository=");
            if (pos != std::string::npos) {
                std::string url = line.substr(pos + 11);
                // Trim whitespace and trailing slashes /current
                while (!url.empty() && (url.back() == ' ' || url.back() == '\t' || url.back() == '\r')) url.pop_back();
                return url;
            }
        }
    }
    return "https://repo-fastly.voidlinux.org/current";
}

struct MirrorRowWidgets {
    MirrorInfo mirror;
    GtkWidget *btnApply;
    GtkWidget *iconApply;
    GtkWidget *lblApply;
    GtkWidget *lblPing;
    GtkWidget *btnTest;
};

} // namespace

GtkWidget* ReposView::create(std::function<void()> onRepoChanged) {
    GtkWidget *scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    GtkWidget *rootBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_container_set_border_width(GTK_CONTAINER(rootBox), 24);
    gtk_container_add(GTK_CONTAINER(scroll), rootBox);

    // 1. Hero Header
    GtkWidget *headPanel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(headPanel), "neko-box-panel");
    gtk_box_pack_start(GTK_BOX(rootBox), headPanel, FALSE, FALSE, 0);

    GtkWidget *lblTitle = gtk_label_new("Espejos y Repositorios Oficiales (XBPS)");
    gtk_widget_set_halign(lblTitle, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblTitle), "neko-hero-title");
    gtk_box_pack_start(GTK_BOX(headPanel), lblTitle, FALSE, FALSE, 0);

    GtkWidget *lblSub = gtk_label_new("Selecciona el servidor espejo (mirror) oficial de Void Linux más rápido para tu conexión y región geográfica.");
    gtk_widget_set_halign(lblSub, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblSub), "neko-card-summary");
    gtk_box_pack_start(GTK_BOX(headPanel), lblSub, FALSE, FALSE, 0);

    // Active Mirror Info Box
    std::string currentActive = getCurrentActiveMirror();
    GtkWidget *activeBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(activeBox), "neko-item-row");
    gtk_box_pack_start(GTK_BOX(rootBox), activeBox, FALSE, FALSE, 0);

    GtkWidget *actIcon = gtk_image_new_from_icon_name("object-select-symbolic", GTK_ICON_SIZE_MENU);
    gtk_box_pack_start(GTK_BOX(activeBox), actIcon, FALSE, FALSE, 0);

    GtkWidget *actLbl = gtk_label_new(nullptr);
    std::string actText = "<b>Espejo actual en uso:</b> <tt>" + currentActive + "</tt>";
    gtk_label_set_markup(GTK_LABEL(actLbl), actText.c_str());
    gtk_widget_set_halign(actLbl, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(activeBox), actLbl, TRUE, TRUE, 0);

    GtkWidget *btnTestAll = gtk_button_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(btnTestAll), "neko-btn-secondary");
    gtk_widget_set_tooltip_text(btnTestAll, "Medir la latencia de todos los servidores espejo");
    GtkWidget *taBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *taIcon = gtk_image_new_from_icon_name("view-refresh-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget *taLbl = gtk_label_new("Probar todos");
    gtk_box_pack_start(GTK_BOX(taBox), taIcon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(taBox), taLbl, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(btnTestAll), taBox);
    gtk_box_pack_start(GTK_BOX(activeBox), btnTestAll, FALSE, FALSE, 0);

    auto allMirrors = getAllMirrors();
    auto rowList = std::make_shared<std::vector<MirrorRowWidgets>>();

    int currentTier = 0;
    GtkWidget *currentPanel = nullptr;

    for (const auto &m : allMirrors) {
        if (m.tier != currentTier) {
            currentTier = m.tier;
            currentPanel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
            gtk_style_context_add_class(gtk_widget_get_style_context(currentPanel), "neko-box-panel");
            gtk_box_pack_start(GTK_BOX(rootBox), currentPanel, FALSE, FALSE, 0);

            const char *tierTitle = (currentTier == 1)
                ? "Servidores Oficiales Tier 1 & Redes CDN Globales"
                : "Servidores Comunitarios Tier 2 (Por Región)";
            GtkWidget *lblT = gtk_label_new(tierTitle);
            gtk_widget_set_halign(lblT, GTK_ALIGN_START);
            gtk_style_context_add_class(gtk_widget_get_style_context(lblT), "neko-card-title");
            gtk_box_pack_start(GTK_BOX(currentPanel), lblT, FALSE, FALSE, 0);
        }

        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_style_context_add_class(gtk_widget_get_style_context(row), "neko-item-row");

        GtkWidget *icon = gtk_image_new_from_icon_name("network-server-symbolic", GTK_ICON_SIZE_BUTTON);
        gtk_box_pack_start(GTK_BOX(row), icon, FALSE, FALSE, 0);

        GtkWidget *infoBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_box_pack_start(GTK_BOX(row), infoBox, TRUE, TRUE, 0);

        GtkWidget *lblName = gtk_label_new(m.name.c_str());
        gtk_widget_set_halign(lblName, GTK_ALIGN_START);
        gtk_style_context_add_class(gtk_widget_get_style_context(lblName), "neko-card-title");
        gtk_box_pack_start(GTK_BOX(infoBox), lblName, FALSE, FALSE, 0);

        std::string sub = m.location + "  •  " + m.region + "  •  " + m.url;
        GtkWidget *lblDesc = gtk_label_new(sub.c_str());
        gtk_widget_set_halign(lblDesc, GTK_ALIGN_START);
        gtk_label_set_ellipsize(GTK_LABEL(lblDesc), PANGO_ELLIPSIZE_END);
        gtk_style_context_add_class(gtk_widget_get_style_context(lblDesc), "neko-card-summary");
        gtk_box_pack_start(GTK_BOX(infoBox), lblDesc, FALSE, FALSE, 0);

        // Ping / Latency Label
        GtkWidget *lblPing = gtk_label_new("");
        gtk_widget_set_valign(lblPing, GTK_ALIGN_CENTER);
        gtk_style_context_add_class(gtk_widget_get_style_context(lblPing), "neko-card-summary");
        gtk_box_pack_start(GTK_BOX(row), lblPing, FALSE, FALSE, 4);

        // Test button
        GtkWidget *btnTest = gtk_button_new();
        gtk_style_context_add_class(gtk_widget_get_style_context(btnTest), "neko-btn-secondary");
        gtk_widget_set_valign(btnTest, GTK_ALIGN_CENTER);
        gtk_widget_set_tooltip_text(btnTest, "Medir tiempo de respuesta y latencia");

        GtkWidget *tBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget *tIcon = gtk_image_new_from_icon_name("network-transmit-receive-symbolic", GTK_ICON_SIZE_BUTTON);
        GtkWidget *tLbl = gtk_label_new("Probar");
        gtk_box_pack_start(GTK_BOX(tBox), tIcon, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(tBox), tLbl, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(btnTest), tBox);

        // Apply button
        bool isActive = (currentActive.find(m.name) != std::string::npos);

        GtkWidget *btnApply = gtk_button_new();
        gtk_widget_set_valign(btnApply, GTK_ALIGN_CENTER);
        GtkWidget *aBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget *aIcon = gtk_image_new_from_icon_name(isActive ? "object-select-symbolic" : "view-refresh-symbolic", GTK_ICON_SIZE_BUTTON);
        GtkWidget *aLbl = gtk_label_new(isActive ? "✓ En uso" : "Aplicar");
        gtk_box_pack_start(GTK_BOX(aBox), aIcon, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(aBox), aLbl, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(btnApply), aBox);

        if (isActive) {
            gtk_style_context_add_class(gtk_widget_get_style_context(btnApply), "neko-btn-secondary");
            gtk_widget_set_sensitive(btnApply, FALSE);
        } else {
            gtk_style_context_add_class(gtk_widget_get_style_context(btnApply), "neko-btn-primary");
        }

        rowList->push_back({m, btnApply, aIcon, aLbl, lblPing, btnTest});

        // Test Callback
        struct TestData {
            MirrorInfo mirror;
            GtkWidget *lblPing;
            GtkWidget *btnTest;
            GtkWidget *tLbl;
            std::shared_ptr<bool> alive;
        };
        auto aliveFlag = std::make_shared<bool>(true);
        auto *td = new TestData{m, lblPing, btnTest, tLbl, aliveFlag};
        g_signal_connect_data(btnTest, "clicked", G_CALLBACK(+[](GtkButton*, gpointer d) {
            auto *data = static_cast<TestData*>(d);
            gtk_widget_set_sensitive(data->btnTest, FALSE);
            gtk_label_set_text(GTK_LABEL(data->tLbl), "...");
            gtk_label_set_markup(GTK_LABEL(data->lblPing), "<i>probando...</i>");

            std::string testUrl = data->mirror.url;
            while (!testUrl.empty() && testUrl.back() == '/') testUrl.pop_back();
            auto alive = data->alive;

            std::thread([data, testUrl, alive]() {
                auto t0 = std::chrono::steady_clock::now();
                std::string cmd = "curl -s -L --connect-timeout 4 --max-time 6 -o /dev/null -w \"%{time_total}\" \"" + testUrl + "/\" 2>/dev/null || echo \"FAIL\"";
                std::string out = Proc::capture("bash", {"-c", cmd});
                auto t1 = std::chrono::steady_clock::now();
                int wallMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

                while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) out.pop_back();

                struct ResPayload {
                    TestData *data;
                    std::shared_ptr<bool> alive;
                    std::string out;
                    int wallMs;
                };

                g_idle_add(+[](gpointer p) -> gboolean {
                    auto *res = static_cast<ResPayload*>(p);
                    if (!res->alive || !*(res->alive)) {
                        delete res;
                        return G_SOURCE_REMOVE;
                    }
                    auto *d = res->data;
                    std::string val = res->out;
                    int wallMs = res->wallMs;

                    gtk_widget_set_sensitive(d->btnTest, TRUE);
                    gtk_label_set_text(GTK_LABEL(d->tLbl), "Probar");

                    if (!val.empty() && val != "FAIL") {
                        try {
                            for (char &c : val) if (c == ',') c = '.';
                            double secs = std::stod(val);
                            int ms = static_cast<int>(std::round(secs * 1000.0));
                            if (ms <= 0 && wallMs > 0) ms = wallMs;
                            if (ms < 1) ms = 1;

                            std::string color = "#8da383"; // green
                            if (ms >= 250) {
                                color = "#c27979"; // red
                            } else if (ms >= 120) {
                                color = "#c9aa70"; // yellow
                            }

                            std::string markup = "<span foreground=\"" + color + "\"><b>" + std::to_string(ms) + " ms</b></span>";
                            gtk_label_set_markup(GTK_LABEL(d->lblPing), markup.c_str());
                        } catch (...) {
                            if (wallMs > 0) {
                                int ms = (wallMs < 1) ? 1 : wallMs;
                                std::string color = (ms >= 250) ? "#c27979" : ((ms >= 120) ? "#c9aa70" : "#8da383");
                                std::string markup = "<span foreground=\"" + color + "\"><b>" + std::to_string(ms) + " ms</b></span>";
                                gtk_label_set_markup(GTK_LABEL(d->lblPing), markup.c_str());
                            } else {
                                gtk_label_set_markup(GTK_LABEL(d->lblPing), "<span foreground=\"#c27979\">Sin respuesta</span>");
                            }
                        }
                    } else {
                        gtk_label_set_markup(GTK_LABEL(d->lblPing), "<span foreground=\"#c27979\">Sin respuesta</span>");
                    }

                    delete res;
                    return G_SOURCE_REMOVE;
                }, new ResPayload{data, alive, out, wallMs});
            }).detach();
        }), td, [](gpointer d, GClosure*) {
            auto *tdPtr = static_cast<TestData*>(d);
            if (tdPtr->alive) *(tdPtr->alive) = false;
            delete tdPtr;
        }, (GConnectFlags)0);

        // Apply Callback
        struct ApplyData {
            MirrorInfo mirror;
            std::shared_ptr<std::vector<MirrorRowWidgets>> rows;
            GtkWidget *actLbl;
            std::function<void()> onRepoChanged;
        };
        auto *ad = new ApplyData{m, rowList, actLbl, onRepoChanged};
        g_signal_connect_data(btnApply, "clicked", G_CALLBACK(+[](GtkButton *b, gpointer d) {
            if (Transaction::isBusy()) return;
            auto *data = static_cast<ApplyData*>(d);
            GtkWidget *btn = GTK_WIDGET(b);
            gtk_widget_set_sensitive(btn, FALSE);

            std::string targetUrl = data->mirror.url;
            while (!targetUrl.empty() && targetUrl.back() == '/') targetUrl.pop_back();

            std::string actionTitle = "Cambiando espejo a " + data->mirror.name;

            Transaction::setMirror(targetUrl, [data, targetUrl](bool success, const std::string &msg) {
                if (success) {
                    std::string updatedAct = "<b>Espejo actual en uso:</b> <tt>" + targetUrl + "/current</tt>";
                    gtk_label_set_markup(GTK_LABEL(data->actLbl), updatedAct.c_str());

                    for (auto &rw : *data->rows) {
                        bool isNowActive = (targetUrl.find(rw.mirror.name) != std::string::npos);
                        GtkStyleContext *sc = gtk_widget_get_style_context(rw.btnApply);
                        if (isNowActive) {
                            gtk_label_set_text(GTK_LABEL(rw.lblApply), "✓ En uso");
                            gtk_image_set_from_icon_name(GTK_IMAGE(rw.iconApply), "object-select-symbolic", GTK_ICON_SIZE_BUTTON);
                            gtk_style_context_remove_class(sc, "neko-btn-primary");
                            gtk_style_context_add_class(sc, "neko-btn-secondary");
                            gtk_widget_set_sensitive(rw.btnApply, FALSE);
                        } else {
                            gtk_label_set_text(GTK_LABEL(rw.lblApply), "Aplicar");
                            gtk_image_set_from_icon_name(GTK_IMAGE(rw.iconApply), "view-refresh-symbolic", GTK_ICON_SIZE_BUTTON);
                            gtk_style_context_remove_class(sc, "neko-btn-secondary");
                            gtk_style_context_add_class(sc, "neko-btn-primary");
                            gtk_widget_set_sensitive(rw.btnApply, TRUE);
                        }
                    }

                    if (data->onRepoChanged) data->onRepoChanged();
                } else {
                    for (auto &rw : *data->rows) {
                        bool isCur = (rw.mirror.name == data->mirror.name);
                        if (isCur) {
                            gtk_widget_set_sensitive(rw.btnApply, TRUE);
                        }
                    }
                    GtkWidget *toplevel = gtk_widget_get_toplevel(data->actLbl);
                    GtkWidget *dlg = gtk_message_dialog_new(
                        GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : nullptr,
                        GTK_DIALOG_MODAL,
                        GTK_MESSAGE_ERROR,
                        GTK_BUTTONS_OK,
                        "Error al aplicar el servidor espejo:\n\n%s",
                        msg.c_str()
                    );
                    g_signal_connect(dlg, "response", G_CALLBACK(gtk_widget_destroy), nullptr);
                    gtk_widget_show_all(dlg);
                }
            });
        }), ad, [](gpointer d, GClosure*) { delete static_cast<ApplyData*>(d); }, (GConnectFlags)0);

        gtk_box_pack_start(GTK_BOX(row), btnTest, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), btnApply, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(currentPanel), row, FALSE, FALSE, 0);
    }

    struct TestAllData {
        std::shared_ptr<std::vector<MirrorRowWidgets>> rows;
        GtkWidget *btn;
    };
    auto *tad = new TestAllData{rowList, btnTestAll};
    g_signal_connect_data(btnTestAll, "clicked", G_CALLBACK(+[](GtkButton*, gpointer d) {
        auto *data = static_cast<TestAllData*>(d);
        for (const auto &r : *data->rows) {
            if (gtk_widget_get_sensitive(r.btnTest)) {
                g_signal_emit_by_name(r.btnTest, "clicked");
            }
        }
    }), tad, [](gpointer d, GClosure*) { delete static_cast<TestAllData*>(d); }, (GConnectFlags)0);

    return scroll;
}
