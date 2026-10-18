// ════════════════════════════════════════════════════════════════════════
// main —— 双模式入口：
//   undoredo_demo.exe              交互 GUI（画布编辑器）
//   undoredo_demo.exe --selftest   offscreen 无头电池（退出码=失败数）
//
// 可序列化命令的工厂注册在应用装配层（SelfTest.cpp::registerDemoPayloads）：
// 命令类型 -> creator 绑定场景指针收不透明 payload 文本，回放时按 id/快照重建。
// ════════════════════════════════════════════════════════════════════════
#include <QApplication>
#include <QProcessEnvironment>

#include "app/MainWindow.h"
#include "app/SelfTest.h"

int main(int argc, char* argv[])
{
    const bool selftest = [&] {
        for (int i = 1; i < argc; ++i)
            if (qstrcmp(argv[i], "--selftest") == 0)
                return true;
        return false;
    }();

    // 自检模式默认无头（外部已设 QT_QPA_PLATFORM 则尊重外部值）
    if (selftest && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("UndoRedoDemo"));
    QApplication::setOrganizationName(QStringLiteral("QtCodes"));

    if (selftest)
        return cd::runCanvasSelfTest();

    cd::MainWindow w;
    w.show();
    return QApplication::exec();
}
