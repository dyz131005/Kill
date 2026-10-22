#pragma once

#include <QIcon>
#include <QJsonArray>
#include <QJsonObject>
#include <QMainWindow>

#include "launcher_core.h"

class QButtonGroup;
class QCheckBox;
class QClipboard;
class QComboBox;
class QDialog;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPropertyAnimation;
class QPushButton;
class QRadioButton;
class QScrollArea;
class QSpinBox;
class QSplitter;
class QStackedWidget;
class QTabWidget;
class QTableWidget;
class QTextEdit;
class QToolButton;
class QVBoxLayout;
class QNetworkAccessManager;
class QNetworkReply;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    void buildUi();
    QWidget *buildTopBar();
    QWidget *buildHomeTab();
    QWidget *buildAccountsTab();
    QWidget *buildDownloadTab();
    QWidget *buildDownloadVersionsPage();
    QWidget *buildDownloadModsPage();
    QWidget *buildDownloadModpacksPage();
    QWidget *buildDownloadResourcepacksPage();
    QWidget *buildDownloadDatapacksPage();
    QWidget *buildVersionManagementTab();
    QWidget *buildInstallLoaderTab();
    QWidget *buildSettingsTab();
    QWidget *buildLogsTab();
    QWidget *buildAboutPage();
    QWidget *buildAnnouncementBar();
    void showAboutPage();
    void leaveAboutPage();

    void applyTheme(bool dark);
    void toggleTheme();
    void animateButton(QPushButton *button);
    void animateExpand(QWidget *widget, bool expand, int durationMs = 200);

    static QString groupBoxStyle();
    static QString labelStyle();

    void loadConfigToUi();
    bool saveUiToConfig();
    void refreshAccountsUi();
    void refreshInstalledVersionsUi();
    void refreshRemoteVersionsUi();
    void syncSelectedVersion(const QString &versionId, QComboBox *source = nullptr);
    QString currentManagedVersion() const;
    void updateVersionProfileEditorState();
    void loadVersionProfileToUi();
    bool saveCurrentVersionProfile();
    void refreshVersionManagementUi();
    void refreshVersionModsUi();
    void refreshVersionWorldsUi();
    void refreshInstalledLoadersUi();
    void refreshAvailableVersionLoaders();
    void appendLog(const QString &message);
    QString logFilePath() const;
    void writeLogLine(const QString &line);
    void openLogDirectory();
    void copyCurrentLogPath();
    void populateResultsTable(const QJsonArray &array, const QString &source);
    void populateModSearchResults(const QJsonArray &array, const QString &source, QVBoxLayout *resultsLayout);
    void refreshDownloadVersions();
    void populateDownloadVersionsTable();
    void populateDownloadVersionList();
    QWidget *buildVersionInstallPage();
    void showVersionInstallPage(const QJsonObject &versionObj);
    void leaveVersionInstallPage();
    void performVersionInstall();
    void downloadSelectedVersion();
    static QIcon versionIconFor(const QString &versionId, const QString &versionType);
    static QString versionTypeDisplayName(const QString &type);
    void refreshLoaderVersions();
    void installSelectedLoader();
    void showResourceDetails();
    void showModDetailsDialog(const QJsonObject &payload, const QString &source, const QString &targetType);
    void showDownloadLocationDialog(const QJsonObject &versionObj, const QJsonObject &fileObj, const QString &targetType, QWidget *parent = nullptr);
    void showVersionInstallDialog(const QJsonObject &versionObj, QWidget *parent = nullptr);
    void downloadModToLocation(const QJsonObject &versionObj, const QJsonObject &fileObj, const QString &targetType, int locationMode, const QString &customDir, QWidget *parent = nullptr);
    void downloadJava();
    void runMicrosoftDeviceLoginFlow();

    LauncherCore m_core;
    QJsonArray m_remoteVersions;

    QComboBox *m_homeVersionCombo = nullptr;
    QComboBox *m_homeAccountCombo = nullptr;
    QComboBox *m_homeVersionTypeCombo = nullptr;

    QStackedWidget *m_homeStackedWidget = nullptr;
    QButtonGroup *m_homeNavGroup = nullptr;
    QList<QPushButton *> m_homeNavButtons;

    QStackedWidget *m_manageStackedWidget = nullptr;
    QButtonGroup *m_manageNavGroup = nullptr;
    QList<QPushButton *> m_manageNavButtons;

    QTabWidget *m_mainTabWidget = nullptr;
    QStackedWidget *m_mainStackedWidget = nullptr;
    QWidget *m_mainTabsPage = nullptr;
    QWidget *m_aboutPage = nullptr;
    QPushButton *m_aboutBackButton = nullptr;
    QPushButton *m_aboutEntryButton = nullptr;
    QWidget *m_announcementBar = nullptr;
    QLabel *m_announcementLabel = nullptr;
    QPushButton *m_announcementCloseButton = nullptr;
    QPlainTextEdit *m_aboutUpdateLogEdit = nullptr;
    int m_mainTabBeforeAbout = 0;
    QPushButton *m_launchButton = nullptr;

    QLineEdit *m_offlineNameEdit = nullptr;
    QPushButton *m_addOfflineButton = nullptr;
    QPushButton *m_openMicrosoftDeviceButton = nullptr;
    QComboBox *m_accountsOverviewCombo = nullptr;

    QLineEdit *m_resourceQueryEdit = nullptr;
    QLineEdit *m_resourceGameVersionEdit = nullptr;
    QComboBox *m_resourceSourceCombo = nullptr;
    QComboBox *m_resourceTargetTypeCombo = nullptr;
    QComboBox *m_resourceLoaderCombo = nullptr;
    QTableWidget *m_resourceResultsTable = nullptr;
    QPushButton *m_resourceSearchButton = nullptr;
    QPushButton *m_resourceDownloadButton = nullptr;
    QVBoxLayout *m_modResultsLayout = nullptr;
    QScrollArea *m_modResultsScroll = nullptr;

    QStackedWidget *m_modsSubStack = nullptr;
    QWidget *m_modsSearchPage = nullptr;
    QWidget *m_modsDetailPage = nullptr;
    QPushButton *m_modsBackButton = nullptr;

    QLineEdit *m_minecraftDirEdit = nullptr;
    QLineEdit *m_javaPathEdit = nullptr;
    QLineEdit *m_javaArgsEdit = nullptr;
    QLineEdit *m_extraGameArgsEdit = nullptr;
    QLineEdit *m_clientIdEdit = nullptr;
    QLineEdit *m_curseForgeKeyEdit = nullptr;

    QProgressBar *m_progressBar = nullptr;
    QLabel *m_statusLabel = nullptr;
    QRadioButton *m_mirrorAutoRadio = nullptr;
    QRadioButton *m_mirrorOnRadio = nullptr;
    QRadioButton *m_mirrorOffRadio = nullptr;
    QSpinBox *m_minMemorySpin = nullptr;
    QSpinBox *m_maxMemorySpin = nullptr;
    QCheckBox *m_verifyFilesCheck = nullptr;
    QCheckBox *m_versionIsolationCheck = nullptr;

    QPlainTextEdit *m_logsEdit = nullptr;
    QString m_logFilePath;

    QComboBox *m_downloadVersionTypeCombo = nullptr;
    QLineEdit *m_downloadVersionSearchEdit = nullptr;
    QListWidget *m_downloadVersionList = nullptr;
    QPushButton *m_refreshDownloadVersionsButton = nullptr;
    QPushButton *m_downloadSelectedVersionButton = nullptr;

    // HMCL 风格：版本页内嵌安装向导（在主窗口内切换，不用独立窗口）
    QStackedWidget *m_versionFlowStack = nullptr;
    QStackedWidget *m_versionCenterStack = nullptr;
    QWidget *m_versionListPage = nullptr;
    QWidget *m_versionInstallPage = nullptr;
    QLabel *m_versionInstallIcon = nullptr;
    QLabel *m_versionInstallTitle = nullptr;
    QLabel *m_versionInstallType = nullptr;
    QLabel *m_versionInstallReleaseTime = nullptr;
    QLineEdit *m_versionInstallNameEdit = nullptr;
    QPushButton *m_versionInstallConfirmButton = nullptr;
    QJsonObject m_selectedDownloadVersion;
    QJsonObject m_installingVersion;

    QJsonArray m_allDownloadVersions;

    QComboBox *m_manageVersionCombo = nullptr;
    QCheckBox *m_manageIsolationCheck = nullptr;
    QLineEdit *m_manageJavaPathEdit = nullptr;
    QLineEdit *m_manageJavaArgsEdit = nullptr;
    QLineEdit *m_manageGameArgsEdit = nullptr;
    QSpinBox *m_manageMinMemorySpin = nullptr;
    QSpinBox *m_manageMaxMemorySpin = nullptr;
    QLabel *m_manageInstanceDirLabel = nullptr;
    QLabel *m_manageModsDirLabel = nullptr;
    QLabel *m_manageWorldsDirLabel = nullptr;
    QLabel *m_manageBaseVersionLabel = nullptr;
    QTableWidget *m_manageModsTable = nullptr;
    QTableWidget *m_manageWorldsTable = nullptr;
    QTableWidget *m_manageInstalledLoadersTable = nullptr;
    QTableWidget *m_manageAvailableLoadersTable = nullptr;
    QComboBox *m_manageLoaderTypeCombo = nullptr;
    QPushButton *m_manageFetchLoaderVersionsButton = nullptr;
    QPushButton *m_manageInstallLoaderButton = nullptr;
    QPushButton *m_manageReinstallLoaderButton = nullptr;
    QPushButton *m_manageUninstallLoaderButton = nullptr;

    QComboBox *m_loaderGameVersionCombo = nullptr;
    QComboBox *m_loaderTypeCombo = nullptr;
    QPushButton *m_refreshLoadersButton = nullptr;
    QTableWidget *m_loaderVersionsTable = nullptr;
    QPushButton *m_installLoaderButton = nullptr;
    QJsonArray m_allLoaderVersions;

    QPushButton *m_themeToggleButton = nullptr;
    bool m_isDarkTheme = false;

    QStackedWidget *m_downloadStackedWidget = nullptr;
    QButtonGroup *m_downloadNavGroup = nullptr;
    QFrame *m_downloadNavIndicator = nullptr;
    QList<QPushButton *> m_downloadNavButtons;

    QHash<QString, QPixmap> m_iconCache;
    QNetworkAccessManager *m_networkManager = nullptr;
};
