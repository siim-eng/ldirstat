#pragma once

class QAction;
class QString;

namespace ldirstat {

class GraphWidget;
class MainWindow;

class MainWindowBuilder {
public:
    static void build(MainWindow *window);

private:
    struct GraphTypeActions {
        QAction *flameGraph = nullptr;
        QAction *treeMapHeaders = nullptr;
        QAction *treeMapPacked = nullptr;
    };

    static void configureWindow(MainWindow *window);
    static void buildToolbar(MainWindow *window);
    static void buildAnalysisWidgets(MainWindow *window);
    static void buildScanPollTimer(MainWindow *window);
    static void buildCentralView(MainWindow *window);
    static GraphTypeActions buildGraphTypeMenu(MainWindow *window);
    static void buildHelpMenu(MainWindow *window);
    static void connectCoreSignals(MainWindow *window);
    static void connectGraphTypeSignals(MainWindow *window, const GraphTypeActions &actions);
    static void connectVisibilitySignals(MainWindow *window);
    static void updateGraphTypeButtonVisibility(MainWindow *window);
    static void activateGraphWidget(MainWindow *window, GraphWidget *widget, const QString &label);
};

} // namespace ldirstat
