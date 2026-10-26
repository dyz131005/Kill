#ifndef ZIP_UTILS_H
#define ZIP_UTILS_H

#include <QString>

namespace ZipUtils {

// Extract a .zip / .jar archive into destinationDir using Qt's built-in
// QZipReader (zlib based). This avoids depending on external tools such as
// `tar` or `powershell Expand-Archive`, which are unavailable or unreliable
// on Windows 7 RTM. Returns true on success; on failure *error (if provided)
// receives a human readable reason.
bool extractArchive(const QString &archivePath,
                    const QString &destinationDir,
                    QString *error = nullptr);

} // namespace ZipUtils

#endif // ZIP_UTILS_H
