#include "actions.h"
#include "apps.h"
#include "collector.h"
#include "inhibitors.h"
#include "inspector.h"
#include "painteditems.h"
#include "prefs.h"
#include "processmodel.h"
#include "protectedlist.h"
#include "systeminfo.h"
#include "theme.h"
#include "toplevels.h"

#include <QCommandLineParser>
#include <QFont>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>

#ifdef __GLIBC__
#include <malloc.h>
#endif

int main(int argc, char **argv)
{
#ifdef __GLIBC__
    // One malloc arena instead of one per thread: the collector thread
    // allocates little, and separate arenas only keep freed memory apart.
    mallopt(M_ARENA_MAX, 1);
#endif
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("omactivity"));
    QCoreApplication::setApplicationName(QStringLiteral("omactivity"));
    QCoreApplication::setApplicationVersion(QStringLiteral(OMACTIVITY_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("omactivity"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Touch-first task manager for Omarchy"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption tabOpt(QStringLiteral("tab"), QStringLiteral("Open on this tab: cpu, memory, energy, disk or network."), QStringLiteral("tab"));
    const QCommandLineOption grabOpt(QStringLiteral("grab"), QStringLiteral("Development: save a picture of the window to <file> and exit."), QStringLiteral("file"));
    const QCommandLineOption sizeOpt(QStringLiteral("size"), QStringLiteral("Development: window size, as WIDTHxHEIGHT."), QStringLiteral("size"));
    const QCommandLineOption showOpt(QStringLiteral("show"), QStringLiteral("Development: state to open in, for --grab."), QStringLiteral("state"));
    parser.addOptions({tabOpt, grabOpt, sizeOpt, showOpt});
    parser.process(app);

    // A recorded tree can stand in for the live one (tests, screenshots).
    const QString procRoot = qEnvironmentVariable("OMACTIVITY_PROC", QStringLiteral("/proc"));
    const QString sysRoot = qEnvironmentVariable("OMACTIVITY_SYS", QStringLiteral("/sys"));

    // Folio's type: the monospace family at 13 px, sizes around it in QML.
    QFont font(QStringLiteral("monospace"));
    font.setPixelSize(13);
    app.setFont(font);

    Theme theme;
    Prefs prefs;
    ProtectedList protectedList;
    protectedList.load(qEnvironmentVariable("OMACTIVITY_PROTECTED", ProtectedList::defaultPath()));
    AppIndex apps;
    Collector collector(procRoot, sysRoot);
    ProcessModel processes(&apps, &protectedList);
    SystemInfo system;
    Toplevels toplevels;
    Inspector inspector(&processes, procRoot);
    Actions actions(&processes, &toplevels, procRoot);

    if (parser.isSet(tabOpt))
        processes.setTab(parser.value(tabOpt));

    QObject::connect(&collector, &Collector::snapshot, &processes, &ProcessModel::setSnapshot);
    QObject::connect(&collector, &Collector::snapshot, &system, &SystemInfo::setSnapshot);
    QObject::connect(&toplevels, &Toplevels::changed, &processes, &ProcessModel::setWindowAppIds);
    // smaps_rollup is costly for the kernel, so only the Memory tab asks for it.
    // The same goes for per-process disk counters (Disk tab) and for the
    // list of who keeps the device awake (Energy tab).
    Inhibitors inhibitors;
    QObject::connect(&inhibitors, &Inhibitors::changed, &processes, &ProcessModel::setSleepBlockers);
    auto memoryDetail = [&] {
        collector.setMemoryDetail(processes.tab() == QLatin1String("memory"));
        collector.setIoDetail(processes.tab() == QLatin1String("disk"));
        inhibitors.setActive(processes.tab() == QLatin1String("energy"));
    };
    QObject::connect(&processes, &ProcessModel::tabChanged, &collector, [&] {
        memoryDetail();
        collector.refreshNow();
    });

    // Listening ports are only looked up while something shows them.
    QObject::connect(&processes, &ProcessModel::wantsPortsChanged, &collector, [&] {
        collector.setPortDetail(processes.wantsPorts());
        collector.refreshNow();
    });
    collector.setPortDetail(processes.wantsPorts());

    // The list is filled before the window is first drawn.
    processes.setSnapshot(collector.first());
    system.setSnapshot(collector.first());
    memoryDetail();
    collector.start();

    qmlRegisterType<IconItem>("Omactivity", 1, 0, "IconItem");
    qmlRegisterType<LinesItem>("Omactivity", 1, 0, "LinesItem");
    QQmlApplicationEngine engine;
    QQmlContext *ctx = engine.rootContext();
    ctx->setContextProperty(QStringLiteral("theme"), &theme);
    ctx->setContextProperty(QStringLiteral("prefs"), &prefs);
    ctx->setContextProperty(QStringLiteral("collector"), &collector);
    ctx->setContextProperty(QStringLiteral("processes"), &processes);
    ctx->setContextProperty(QStringLiteral("system"), &system);
    ctx->setContextProperty(QStringLiteral("inspector"), &inspector);
    ctx->setContextProperty(QStringLiteral("actions"), &actions);
    ctx->setContextProperty(QStringLiteral("devShow"), parser.value(showOpt));
    int devWidth = 0, devHeight = 0;
    if (parser.isSet(sizeOpt)) {
        const QStringList wh = parser.value(sizeOpt).split(QLatin1Char('x'));
        devWidth = wh.value(0).toInt();
        devHeight = wh.value(1).toInt();
    }
    ctx->setContextProperty(QStringLiteral("devWidth"), devWidth);
    ctx->setContextProperty(QStringLiteral("devHeight"), devHeight);

    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

#ifdef __GLIBC__
    // Starting up leaves freed memory behind; give it back once the first
    // frames are out.
    QTimer::singleShot(3000, &app, [&engine] {
        engine.collectGarbage();
        malloc_trim(0);
    });
#endif

    if (parser.isSet(grabOpt)) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        const QString file = parser.value(grabOpt);
        QTimer::singleShot(3500, &app, [window, file] {
            const QImage image = window ? window->grabWindow() : QImage();
            const bool ok = !image.isNull() && image.save(file);
            if (!ok)
                qWarning("--grab: could not save %s (window %s, image %dx%d)", qPrintable(file), window ? "ok" : "missing",
                         image.width(), image.height());
            QCoreApplication::exit(ok ? 0 : 1);
        });
    }
    return app.exec();
}
