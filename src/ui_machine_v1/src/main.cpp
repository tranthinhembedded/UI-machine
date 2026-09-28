#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFont>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("TNM Vision"));
    QApplication::setOrganizationName(QStringLiteral("TNM"));
    QApplication::setFont(QFont(QStringLiteral("Segoe UI"), 10));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("TNM Vision - kiểm tra bề mặt vải bằng PP7-Fast"));
    parser.addHelpOption();
    QCommandLineOption windowed(QStringList{QStringLiteral("w"),
                                             QStringLiteral("windowed")},
                                QStringLiteral("Khởi động ở chế độ cửa sổ"));
    parser.addOption(windowed);
    parser.process(app);

    MainWindow window;
    if (parser.isSet(windowed))
        window.show();
    else
        window.showFullScreen();
    return app.exec();
}
