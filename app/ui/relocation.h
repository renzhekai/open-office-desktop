#pragma once

#include <QString>
#include <QStringList>

namespace openoffice {

// 设置 → 迁移: the whole program folder (program, editor profile, downloaded
// models) moves to another place. The program cannot move files it is running
// from, so a small PowerShell helper does it after the program has exited: it
// waits for every process started from the folder, moves it (a rename on the
// same drive, a verified copy otherwise), points shortcuts and the uninstall
// entry at the new place and starts the program there again. When anything
// fails the old folder stays as it was and the program starts from it.

struct RelocationPlan {
    QString source;      // the program folder now
    QString target;      // where it goes: the chosen folder plus the program folder's name
    bool sameVolume = false;
    bool valid() const { return !source.isEmpty() && !target.isEmpty(); }
};

// Checks a folder the person chose; on failure returns an invalid plan and a reason in Chinese.
RelocationPlan planRelocation(const QString& source, const QString& chosenFolder, QString* error);
qint64 folderBytes(const QString& folder);

// For tests: run the helper on other folders without starting the program afterwards.
struct RelocationOptions {
    QStringList shortcutFolders; // empty: the person's Start menu programs and desktop
    QString uninstallKey;        // empty: HKCU's uninstall entries
    bool launch = true;
};

// The helper as a command: powershell.exe and its arguments. The script goes in
// as -EncodedCommand, so script execution policies do not block it.
QStringList relocationCommand(const RelocationPlan& plan, qint64 processId, const QString& logPath,
                              const RelocationOptions& options = {});
// Starts the helper detached and windowless; it waits for this process to end.
bool startRelocation(const RelocationPlan& plan, QString* error);

// What the helper reported, read by the program it started again.
struct RelocationOutcome {
    bool known = false;
    bool moved = false;
    QString code;   // moved, moved-leftover, or why it failed
    QString source;
    QString target;
};
RelocationOutcome readRelocationLog(const QString& logPath);
QString relocationMessage(const RelocationOutcome& outcome);

} // namespace openoffice
