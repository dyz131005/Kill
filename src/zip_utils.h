#ifndef ZIP_UTILS_H
#define ZIP_UTILS_H

#include <QString>
#include <QByteArray>

namespace ZipUtils {

// Extract a .zip / .jar archive into destinationDir using Qt's built-in
// QZipReader (zlib based). This avoids depending on external tools such as
// `tar` or `powershell Expand-Archive`, which are unavailable or unreliable
// on Windows 7 RTM. Returns true on success; on failure *error (if provided)
// receives a human readable reason.
bool extractArchive(const QString &archivePath,
                    const QString &destinationDir,
                    QString *error = nullptr);

// Read a single file's raw bytes from inside an archive (e.g. install_profile.json
// from a Forge/NeoForge installer jar). Returns true and fills *outBytes on success.
bool readFileFromArchive(const QString &archivePath,
                         const QString &entryName,
                         QByteArray *outBytes,
                         QString *error = nullptr);

// Extract every entry under a given prefix (e.g. "maven/") from the archive
// into destinationDir, preserving the path structure rooted at the prefix.
// This mirrors HMCL's ForgeNewInstallTask: the Forge/NeoForge installer ships
// its own build-time libraries (neoform, client, etc.) inside the jar under
// "maven/", so they never need to be fetched from the blocked maven host.
// Returns the number of files extracted (0 is valid if the prefix is absent).
int extractSubdirFromArchive(const QString &archivePath,
                             const QString &prefix,
                             const QString &destinationDir,
                             QString *error = nullptr);

} // namespace ZipUtils

#endif // ZIP_UTILS_H
