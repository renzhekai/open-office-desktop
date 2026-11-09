#include "relocation.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStorageInfo>
#include <QTemporaryDir>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace openoffice {
namespace {

// Windows PowerShell 5.1 (on every Windows 10/11). ASCII only: what the person
// reads is written by the program from the RESULT code. The program prepends
// $ProcessId, $Source, $Target, $Log, $Launch, $ShortcutFolders, $UninstallKey.
const char* const kScript = R"PS(
$ErrorActionPreference = 'Stop'
$exeName = 'OpenOffice.exe'
function Say([string]$line) { Add-Content -LiteralPath $Log -Value $line -Encoding UTF8 }
function Normal([string]$path) { [IO.Path]::GetFullPath($path).TrimEnd('\') }
$Source = Normal $Source
$Target = Normal $Target
$pattern = [regex]::Escape($Source) + '(?=\\|"|,|$)'
function Swap([string]$text) {
    if (-not $text) { return $text }
    return [regex]::Replace($text, $pattern, $Target.Replace('$', '$$'), 'IgnoreCase')
}
function Finish([string]$code) {
    Say ('RESULT ' + $code)
    # Still running: the program is open anyway, a second copy would only confuse.
    if ($Launch -and $code -ne 'still-running') {
        $folder = $Source
        if ($code -like 'moved*') { $folder = $Target }
        $program = Join-Path $folder $exeName
        if (Test-Path -LiteralPath $program) {
            Start-Process -FilePath $program -WorkingDirectory $folder -ArgumentList ('--relocation-log "' + $Log + '"')
        }
    }
    exit 0
}
function Busy {
    @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
        $_.Path -and $_.Path.StartsWith($Source + '\', [StringComparison]::OrdinalIgnoreCase) })
}
try {
    Say ('SOURCE ' + $Source)
    Say ('TARGET ' + $Target)
    # The program, then everything it started from its folder (editor helpers, the AI engine).
    $deadline = (Get-Date).AddMinutes(2)
    while ((Get-Process -Id $ProcessId -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 300 }
    while ((Busy).Count -gt 0 -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
    if ((Get-Process -Id $ProcessId -ErrorAction SilentlyContinue) -or (Busy).Count -gt 0) { Finish 'still-running' }
    if (-not (Test-Path -LiteralPath (Join-Path $Source $exeName))) { Finish 'source-missing' }
    if (Test-Path -LiteralPath $Target) { Finish 'target-exists' }
    if (-not (Test-Path -LiteralPath (Split-Path -Parent $Target))) { Finish 'target-parent-missing' }

    $leftover = $false
    $sameVolume = [string]::Equals([IO.Path]::GetPathRoot($Source), [IO.Path]::GetPathRoot($Target),
                                   [StringComparison]::OrdinalIgnoreCase)
    if ($sameVolume) {
        try { [IO.Directory]::Move($Source, $Target) }
        catch { Say ('ERROR ' + $_.Exception.Message); Finish 'move-failed' }
        Say 'MOVED rename'
    } else {
        $files = @(Get-ChildItem -LiteralPath $Source -Recurse -Force -File)
        $bytes = [long]($files | Measure-Object -Property Length -Sum).Sum
        Say ('COPY ' + $files.Count + ' files ' + $bytes + ' bytes')
        & robocopy.exe $Source $Target /E /COPY:DAT /DCOPY:DAT /XJ /R:2 /W:1 /NP /NFL /NDL /NJH /NJS | Out-Null
        $robocopy = $LASTEXITCODE
        $copies = @(Get-ChildItem -LiteralPath $Target -Recurse -Force -File -ErrorAction SilentlyContinue)
        $copied = [long]($copies | Measure-Object -Property Length -Sum).Sum
        if ($robocopy -ge 8 -or $copies.Count -ne $files.Count -or $copied -ne $bytes) {
            Say ('ERROR robocopy ' + $robocopy + ', copied ' + $copies.Count + ' files ' + $copied + ' bytes')
            # Only the copy this run made; the original is untouched.
            if (Test-Path -LiteralPath $Target) { Remove-Item -LiteralPath $Target -Recurse -Force -ErrorAction SilentlyContinue }
            Finish 'copy-failed'
        }
        Say 'MOVED copy'
        try { Remove-Item -LiteralPath $Source -Recurse -Force } catch { Say ('WARN ' + $_.Exception.Message) }
        if (Test-Path -LiteralPath $Source) { & cmd.exe /c rd /s /q "$Source" 2>$null }
        if (Test-Path -LiteralPath $Source) { $leftover = $true; Say 'WARN old folder left' }
    }

    $folders = @([Environment]::GetFolderPath('Programs'), [Environment]::GetFolderPath('DesktopDirectory'))
    if ($ShortcutFolders.Count -gt 0) { $folders = $ShortcutFolders }
    $shell = New-Object -ComObject WScript.Shell
    foreach ($folder in $folders) {
        if (-not $folder -or -not (Test-Path -LiteralPath $folder)) { continue }
        foreach ($file in @(Get-ChildItem -LiteralPath $folder -Recurse -Filter '*.lnk' -File -ErrorAction SilentlyContinue)) {
            try {
                $link = $shell.CreateShortcut($file.FullName)
                $moved = Swap $link.TargetPath
                if ($moved -ceq $link.TargetPath) { continue }
                $link.TargetPath = $moved
                $link.WorkingDirectory = Swap $link.WorkingDirectory
                $link.IconLocation = Swap $link.IconLocation
                $link.Save()
                Say ('LINK ' + $file.FullName)
            } catch { Say ('WARN link ' + $file.FullName + ': ' + $_.Exception.Message) }
        }
    }

    if (Test-Path -LiteralPath $UninstallKey) {
        foreach ($key in @(Get-ChildItem -LiteralPath $UninstallKey -ErrorAction SilentlyContinue)) {
            $values = Get-ItemProperty -LiteralPath $key.PSPath
            $location = $values.'Inno Setup: App Path'
            if (-not $location) { $location = $values.InstallLocation }
            if (-not $location) { continue }
            if (-not [string]::Equals((Normal $location), $Source, [StringComparison]::OrdinalIgnoreCase)) { continue }
            foreach ($name in @('Inno Setup: App Path', 'InstallLocation', 'DisplayIcon', 'UninstallString',
                                'QuietUninstallString', 'ModifyPath')) {
                $value = $values.$name
                if ($value -is [string] -and $value) { Set-ItemProperty -LiteralPath $key.PSPath -Name $name -Value (Swap $value) }
            }
            Say ('UNINSTALL ' + $key.PSChildName)
        }
    }
    if ($leftover) { Finish 'moved-leftover' } else { Finish 'moved' }
} catch {
    Say ('ERROR ' + $_.Exception.Message)
    if (Test-Path -LiteralPath (Join-Path $Target $exeName)) { Finish 'moved-with-errors' } else { Finish 'failed' }
}
)PS";

QString quoted(const QString& text)
{
    QString escaped = text;
    escaped.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

QString native(const QString& path)
{
    return QDir::toNativeSeparators(path);
}

bool samePath(const QString& a, const QString& b)
{
    return a.compare(b, Qt::CaseInsensitive) == 0;
}

bool insidePath(const QString& path, const QString& folder)
{
    const QString prefix = folder.endsWith(QLatin1Char('/')) ? folder : folder + QLatin1Char('/');
    return path.startsWith(prefix, Qt::CaseInsensitive);
}

} // namespace

RelocationPlan planRelocation(const QString& source, const QString& chosenFolder, QString* error)
{
    const auto fail = [error](const QString& reason) {
        if (error) *error = reason;
        return RelocationPlan{};
    };
    const QString from = QDir::cleanPath(QFileInfo(source).absoluteFilePath());
    const QFileInfo chosen(chosenFolder);
    if (chosenFolder.isEmpty() || !chosen.isDir()) return fail(QStringLiteral("选的不是一个文件夹。"));
    const QString parent = QDir::cleanPath(chosen.absoluteFilePath());
    if (parent.startsWith(QStringLiteral("//")))
        return fail(QStringLiteral("不能搬到网络位置：编辑器和模型需要放在这台电脑的硬盘上。"));
    if (samePath(parent, from) || insidePath(parent, from))
        return fail(QStringLiteral("不能搬到软件自己的文件夹里面，请选别的位置。"));
    const QString target = QDir(parent).filePath(QFileInfo(from).fileName());
    if (samePath(target, from)) return fail(QStringLiteral("软件已经在这个位置了。"));
    if (QFileInfo::exists(target))
        return fail(QStringLiteral("%1 已经存在。请选别的位置，或先把那个文件夹改名。").arg(native(target)));
    const QTemporaryDir probe(QDir(parent).filePath(QStringLiteral(".openoffice-write-test-XXXXXX")));
    if (!probe.isValid())
        return fail(QStringLiteral("没有权限在 %1 里新建文件夹，请选别的位置。").arg(native(parent)));
    RelocationPlan plan;
    plan.source = from;
    plan.target = target;
    plan.sameVolume = samePath(QStorageInfo(from).rootPath(), QStorageInfo(parent).rootPath());
    return plan;
}

qint64 folderBytes(const QString& folder)
{
    qint64 total = 0;
    QDirIterator it(folder, QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

QStringList relocationCommand(const RelocationPlan& plan, qint64 processId, const QString& logPath,
                              const RelocationOptions& options)
{
    QStringList folders;
    for (const QString& folder : options.shortcutFolders) folders << quoted(native(folder));
    const QString uninstallKey = options.uninstallKey.isEmpty()
        ? QStringLiteral("HKCU:\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall") : options.uninstallKey;
    const QString script = QStringLiteral("$ProcessId = %1\n$Source = %2\n$Target = %3\n$Log = %4\n$Launch = %5\n"
                                          "$ShortcutFolders = @(%6)\n$UninstallKey = %7\n")
                               .arg(processId)
                               .arg(quoted(native(plan.source)), quoted(native(plan.target)), quoted(native(logPath)),
                                    options.launch ? QStringLiteral("$true") : QStringLiteral("$false"),
                                    folders.join(QLatin1Char(',')), quoted(uninstallKey))
        + QString::fromLatin1(kScript);
    // -EncodedCommand takes the script as base64 of UTF-16LE.
    const QByteArray utf16(reinterpret_cast<const char*>(script.utf16()), script.size() * 2);
    const QString powershell = QDir(qEnvironmentVariable("SystemRoot", QStringLiteral("C:\\Windows")))
                                   .filePath(QStringLiteral("System32/WindowsPowerShell/v1.0/powershell.exe"));
    return {native(powershell), QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
            QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"), QStringLiteral("-WindowStyle"),
            QStringLiteral("Hidden"), QStringLiteral("-EncodedCommand"), QString::fromLatin1(utf16.toBase64())};
}

bool startRelocation(const RelocationPlan& plan, QString* error)
{
    const QString log = QDir(QDir::tempPath()).filePath(
        QStringLiteral("openoffice-relocation-%1-%2.log")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")))
            .arg(QCoreApplication::applicationPid()));
    QStringList command = relocationCommand(plan, QCoreApplication::applicationPid(), log);
    QProcess helper;
    helper.setProgram(command.takeFirst());
    helper.setArguments(command);
#ifdef Q_OS_WIN
    // No console window; and out of any job this program runs in, so it outlives the program.
    for (const DWORD extra : {DWORD(CREATE_BREAKAWAY_FROM_JOB), DWORD(0)}) {
        helper.setCreateProcessArgumentsModifier([extra](QProcess::CreateProcessArguments* arguments) {
            arguments->flags |= CREATE_NO_WINDOW | extra;
        });
        if (helper.startDetached()) return true;
    }
#else
    if (helper.startDetached()) return true;
#endif
    if (error) *error = helper.errorString();
    return false;
}

RelocationOutcome readRelocationLog(const QString& logPath)
{
    RelocationOutcome outcome;
    QFile file(logPath);
    if (!file.open(QIODevice::ReadOnly)) return outcome;
    QByteArray bytes = file.readAll();
    if (bytes.startsWith("\xEF\xBB\xBF")) bytes.remove(0, 3);
    for (const QString& line : QString::fromUtf8(bytes).split(QLatin1Char('\n'))) {
        const QString text = line.trimmed();
        if (text.startsWith(QStringLiteral("SOURCE "))) outcome.source = text.mid(7);
        else if (text.startsWith(QStringLiteral("TARGET "))) outcome.target = text.mid(7);
        else if (text.startsWith(QStringLiteral("RESULT "))) {
            outcome.known = true;
            outcome.code = text.mid(7);
            outcome.moved = outcome.code.startsWith(QStringLiteral("moved"));
        }
    }
    return outcome;
}

QString relocationMessage(const RelocationOutcome& outcome)
{
    const QString to = native(outcome.target);
    const QString from = native(outcome.source);
    const QString stayed = QStringLiteral("软件还在原来的位置 %1。").arg(from);
    if (!outcome.known) return QStringLiteral("没有拿到迁移结果，请看看软件现在在哪个位置。");
    if (outcome.code == QStringLiteral("moved"))
        return QStringLiteral("Open Office 已经搬到 %1。下载的模型也一起搬过去了；桌面和开始菜单的快捷方式已改好。").arg(to);
    if (outcome.code == QStringLiteral("moved-leftover"))
        return QStringLiteral("Open Office 已经搬到 %1。原来的文件夹 %2 没能全部删掉（可能有文件正被占用），可以稍后手动删除。")
            .arg(to, from);
    if (outcome.code == QStringLiteral("moved-with-errors"))
        return QStringLiteral("Open Office 已经搬到 %1，但更新快捷方式或卸载信息时出了问题。如果快捷方式打不开，请到新位置打开 OpenOffice.exe。")
            .arg(to);
    if (outcome.code == QStringLiteral("still-running"))
        return QStringLiteral("没有搬：Open Office 的程序 2 分钟内没有完全退出。") + stayed;
    if (outcome.code == QStringLiteral("target-exists"))
        return QStringLiteral("没有搬：%1 已经存在。").arg(to) + stayed;
    if (outcome.code == QStringLiteral("move-failed"))
        return QStringLiteral("没有搬：有文件正被别的程序占用（例如杀毒软件，或打开着这个文件夹的窗口）。") + stayed;
    if (outcome.code == QStringLiteral("copy-failed"))
        return QStringLiteral("没有搬：复制没有完成（可能是空间不够或文件被占用），复制了一半的文件已经删除。") + stayed;
    return QStringLiteral("没有搬：出现了意外的错误。") + stayed;
}

} // namespace openoffice
