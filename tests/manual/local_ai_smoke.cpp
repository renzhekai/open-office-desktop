// Manual check of the built-in AI end to end (not part of CTest): starts the
// real engine with a real model through LocalAssistant, runs one office task
// on a text file and prints the raw answer, what the program makes of it, and
// the timing. Development only.
//
//   set OPEN_OFFICE_AI_ENGINE=D:\open office\dependencies\llama.cpp-b12202
//   set OPEN_OFFICE_MODEL_DIR=D:\open office\dependencies\local-models\download
//   local_ai_smoke proofread document.txt
#include "assistant/assistant_tasks.h"
#include "assistant/local_assistant.h"
#include "assistant/markdown_html.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>
#include <QTimer>

#include <cstdio>

using namespace openoffice;

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    qputenv("OPEN_OFFICE_AI_BACKEND", "builtin");
    const QStringList arguments = app.arguments();
    if (arguments.size() < 3) {
        std::fprintf(stderr, "usage: local_ai_smoke <task id> <utf-8 text file>\n");
        return 2;
    }
    const TaskSpec* spec = findTask(arguments.at(1));
    QFile file(arguments.at(2));
    if (!spec || !file.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "unknown task or unreadable file\n");
        return 2;
    }
    const QString text = QString::fromUtf8(file.readAll());
    QTextStream out(stdout);
    out.setEncoding(QStringConverter::Utf8);

    LocalAssistant assistant;
    assistant.refresh();
    out << "state " << int(assistant.state()) << " model " << assistant.model() << " engine " << assistant.engineText() << "\n";
    out.flush();
    if (assistant.state() != LocalAiState::Ready) return 1;
    QElapsedTimer clock;
    clock.start();
    QObject::connect(&assistant, &LocalAssistant::taskFinished, &app,
                     [&](const QString&, const QString& answer, bool ok, const QString& error) {
        out << "finished after " << clock.elapsed() << " ms, ok " << ok << " error " << error << "\n";
        out << "speed " << assistant.lastSpeed() << " tokens/s\n--- raw answer ---\n" << answer << "\n--- program view ---\n";
        if (spec->output == TaskOutput::Corrections) {
            for (const Correction& fix : parseCorrections(answer, text))
                out << fix.original << " -> " << fix.corrected << " (" << fix.reason << ")\n";
        } else if (spec->output == TaskOutput::Rewrite) {
            out << cleanRewrite(answer) << "\n";
        } else {
            out << markdownToHtml(answer) << "\n";
        }
        out.flush();
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
    });
    TaskInput input;
    if (spec->scope == TaskScope::Writing) input.detail = text;
    else input.text = text;
    if (arguments.size() > 3) input.detail = arguments.at(3); // what the person asked, e.g. "详细点" for rework
    input.documentName = QStringLiteral("smoke.docx");
    if (!assistant.runTask(*spec, input)) return 1;
    QTimer::singleShot(300000, &app, [&] {
        out << "no answer within 5 minutes\n";
        out.flush();
        app.exit(1);
    });
    return app.exec();
}
