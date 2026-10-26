#include "zip_utils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <QtCore/private/qzipreader_p.h>

namespace ZipUtils {

bool extractArchive(const QString &archivePath,
                    const QString &destinationDir,
                    QString *error)
{
    if (error) {
        error->clear();
    }

    if (!QFileInfo::exists(archivePath)) {
        if (error) {
            *error = QString("压缩包不存在: %1").arg(QDir::toNativeSeparators(archivePath));
        }
        return false;
    }

    if (!QDir().mkpath(destinationDir)) {
        if (error) {
            *error = QString("无法创建目标目录: %1").arg(QDir::toNativeSeparators(destinationDir));
        }
        return false;
    }

    QZipReader reader(archivePath);
    if (!reader.isReadable()) {
        if (error) {
            *error = QString("无法读取压缩包: %1 (状态=%2)")
                         .arg(QDir::toNativeSeparators(archivePath))
                         .arg(static_cast<int>(reader.status()));
        }
        return false;
    }

    const QList<QZipReader::FileInfo> entries = reader.fileInfoList();
    if (entries.isEmpty() && reader.count() == 0) {
        // Nothing to extract; treat an empty archive as success.
        reader.close();
        return true;
    }

    const QDir baseDir(destinationDir);
    for (const QZipReader::FileInfo &entry : entries) {
        // Normalise the entry path and guard against path traversal.
        QString relative = entry.filePath;
        relative.replace('\\', '/');
        while (relative.startsWith('/')) {
            relative.remove(0, 1);
        }
        if (relative.isEmpty() || relative.contains("..")) {
            continue;
        }

        const QString targetPath = baseDir.filePath(relative);

        if (entry.isDir) {
            QDir().mkpath(targetPath);
            continue;
        }

        // Only extract regular files; skip symlinks for safety.
        if (!entry.isFile) {
            continue;
        }

        QDir().mkpath(QFileInfo(targetPath).absolutePath());

        const QByteArray data = reader.fileData(entry.filePath);
        QFile out(targetPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (error) {
                *error = QString("无法写入文件: %1")
                             .arg(QDir::toNativeSeparators(targetPath));
            }
            reader.close();
            return false;
        }
        const qint64 written = out.write(data);
        out.close();
        if (written != data.size()) {
            if (error) {
                *error = QString("写入文件不完整: %1")
                             .arg(QDir::toNativeSeparators(targetPath));
            }
            reader.close();
            return false;
        }
    }

    reader.close();
    return true;
}

} // namespace ZipUtils
